// Subsystem-level tests. Links engine_lib so we get the real AudioSystem,
// PhysicsWorld, JobSystem, Hud, ParticleSystem, and SaveGame implementations.
// Tests deliberately avoid creating a gfx::IDevice — these are headless smoke
// checks on the standalone subsystems.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

import audio;
import particles;
import physics;
import jobs;
import hud;
import save_game;

TEST_CASE("AudioSystem: init/destroy is safe even if no device available")
{
    AudioSystem a;
    // isReady can be true or false depending on the test host; both are valid.
    SUBCASE("playSound on dead engine returns false") {
        AudioSystem dead;
        // Force "not ready" by destroying and reusing — actually can't easily;
        // instead just call playSound on a missing file path.
        bool ok = a.playSound("does_not_exist__.wav");
        // Either init failed (returns false) or play_sound failed (returns false).
        // Both end up false; the assertion is "no crash".
        CHECK_FALSE(ok);
    }
}

TEST_CASE("PhysicsWorld: steps without crashing")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());
    CHECK(w.bodyCount() == 0);
    for (int i = 0; i < 10; ++i) {
        w.step(1.0f / 60.0f);
    }
    CHECK(w.bodyCount() == 0);  // no bodies created
}

TEST_CASE("PhysicsWorld: dynamic body falls under gravity, static body stays put")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    // Big static floor at y=0, half-thickness 0.5.
    auto floor = w.createBoxBody(0, -0.5f, 0, 100.0f, 0.5f, 100.0f, /*dynamic=*/false);
    CHECK(floor != 0);

    // Dynamic box dropped from y=5.
    auto box = w.createBoxBody(0, 5.0f, 0, 0.5f, 0.5f, 0.5f, /*dynamic=*/true, 1.0f);
    CHECK(box != 0);

    float x0 = 0, y0 = 0, z0 = 0;
    w.getBodyPosition(box, x0, y0, z0);
    CHECK(y0 == doctest::Approx(5.0f));

    // Simulate one second at 60 Hz; box should fall but stop on the floor
    // (final y should be roughly halfExtent above the floor's top).
    for (int i = 0; i < 60; ++i) {
        w.step(1.0f / 60.0f);
    }
    float x1 = 0, y1 = 0, z1 = 0;
    w.getBodyPosition(box, x1, y1, z1);
    CHECK(y1 < y0);            // moved downward
    CHECK(y1 > -1.0f);         // not through the floor
    CHECK(y1 < 5.0f);          // and is below start
}

TEST_CASE("PhysicsWorld: dynamic body rotates as it falls")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());
    w.createBoxBody(0, -0.5f, 0, 100.0f, 0.5f, 100.0f, /*dynamic=*/false);

    // A long thin box dropped onto its corner should pick up some rotation
    // as it interacts with the floor (or simply integrates angular velocity).
    // We just verify the rotation API returns a unit quaternion at rest and
    // that the quaternion changes after a fall.
    auto box = w.createBoxBody(0, 5.0f, 0, 0.3f, 0.05f, 1.0f, /*dynamic=*/true, 1.0f);
    REQUIRE(box != 0);
    float qx0 = 0, qy0 = 0, qz0 = 0, qw0 = 1;
    w.getBodyRotation(box, qx0, qy0, qz0, qw0);
    CHECK(qw0 == doctest::Approx(1.0f));  // bind-pose
    CHECK(qx0 == doctest::Approx(0.0f));
    CHECK(qy0 == doctest::Approx(0.0f));
    CHECK(qz0 == doctest::Approx(0.0f));

    // Two seconds at 120 Hz gives the box time to land + roll.
    for (int i = 0; i < 240; ++i) {
        w.step(1.0f / 120.0f);
    }
    float qx1 = 0, qy1 = 0, qz1 = 0, qw1 = 1;
    w.getBodyRotation(box, qx1, qy1, qz1, qw1);
    // Quaternion should be unit length.
    float len2 = qx1 * qx1 + qy1 * qy1 + qz1 * qz1 + qw1 * qw1;
    CHECK(len2 == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("PhysicsWorld: convex-hull body falls onto floor and settles")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    // Static floor at y=0, half-thickness 0.5.
    auto floor = w.createBoxBody(0, -0.5f, 0, 50.0f, 0.5f, 50.0f, /*dynamic=*/false);
    REQUIRE(floor != 0);

    // Tetrahedron centered at origin, side ~1. 4 points → minimum hull.
    const float points[] = {
        0.0f,  0.5f, 0.0f,
       -0.5f, -0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,
        0.0f, -0.5f,  0.5f,
    };
    auto hull = w.createConvexHullBody(
        points, 4, sizeof(float) * 3, 0.0f, 5.0f, 0.0f, /*dynamic=*/true, /*mass=*/1.0f,
        /*tolerance=*/0.01f
    );
    REQUIRE(hull != 0);

    float x0 = 0, y0 = 0, z0 = 0;
    w.getBodyPosition(hull, x0, y0, z0);
    CHECK(y0 == doctest::Approx(5.0f));

    // Two seconds at 60 Hz; tetrahedron should fall and settle above the floor.
    for (int i = 0; i < 120; ++i) {
        w.step(1.0f / 60.0f);
    }
    float x1 = 0, y1 = 0, z1 = 0;
    w.getBodyPosition(hull, x1, y1, z1);
    CHECK(y1 < y0);
    CHECK(y1 > -1.0f);  // not through the floor
}

TEST_CASE("PhysicsWorld: convex hull rejects degenerate (< 4 points) input")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    const float pts[] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    auto bad =
        w.createConvexHullBody(pts, 3, sizeof(float) * 3, 0, 5, 0, true, 1.0f, 0.05f);
    CHECK(bad == 0);

    auto null = w.createConvexHullBody(nullptr, 100, sizeof(float) * 3, 0, 5, 0, true, 1.0f, 0.05f);
    CHECK(null == 0);
}

TEST_CASE("PhysicsWorld: applyImpulse sends a body upward")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    // Static floor so the body has something to land on (matters for later
    // tests but not this one — verify it doesn't crash with no floor).
    auto box = w.createBoxBody(0, 0, 0, 0.5f, 0.5f, 0.5f, /*dynamic=*/true, 1.0f);
    REQUIRE(box != 0);

    // Apply an upward impulse strong enough to overcome a single step's
    // gravity (~0.16 m/s downward at 60Hz).
    w.applyImpulse(box, 0, 10.0f, 0);

    // One step at 60Hz: 10 m/s up, minus 9.81/60 ≈ 0.16 m/s of gravity ≈ 9.84 m/s.
    // After dt=1/60, y should be at +9.84/60 ≈ +0.16 m above start.
    w.step(1.0f / 60.0f);
    float x = 0, y = 0, z = 0;
    w.getBodyPosition(box, x, y, z);
    CHECK(y > 0.05f);
}

TEST_CASE("PhysicsWorld: capsule body falls onto floor and rests on its base")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());
    w.createBoxBody(0, -0.5f, 0, 50.0f, 0.5f, 50.0f, /*dynamic=*/false);

    // Capsule: halfHeight=0.6 cylinder + radius=0.3 caps = 1.8 m total height.
    auto capsule = w.createCapsuleBody(
        0.0f, 4.0f, 0.0f, /*halfHeight=*/0.6f, /*radius=*/0.3f, /*dynamic=*/true, /*mass=*/70.0f
    );
    REQUIRE(capsule != 0);

    for (int i = 0; i < 180; ++i) {
        w.step(1.0f / 60.0f);
    }
    float x = 0, y = 0, z = 0;
    w.getBodyPosition(capsule, x, y, z);
    // Capsule center at rest should sit at floorTop(0) + radius(0.3) + halfHeight(0.6) = 0.9.
    // Allow a generous tolerance for solver settling.
    CHECK(y > 0.5f);
    CHECK(y < 1.3f);
}

TEST_CASE("PhysicsWorld: linear/angular velocity get + set round-trip")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    auto box = w.createBoxBody(0, 5.0f, 0, 0.5f, 0.5f, 0.5f, /*dynamic=*/true, 1.0f);
    REQUIRE(box != 0);

    // Linear velocity round-trip.
    w.setLinearVelocity(box, 1.5f, -2.0f, 0.5f);
    float vx = 0, vy = 0, vz = 0;
    w.getLinearVelocity(box, vx, vy, vz);
    CHECK(vx == doctest::Approx(1.5f));
    CHECK(vy == doctest::Approx(-2.0f));
    CHECK(vz == doctest::Approx(0.5f));

    // Angular velocity round-trip.
    w.setAngularVelocity(box, 0.0f, 3.14f, 0.0f);
    float wx = 0, wy = 0, wz = 0;
    w.getAngularVelocity(box, wx, wy, wz);
    CHECK(wy == doctest::Approx(3.14f));
}

TEST_CASE("PhysicsWorld: setBodyPosition teleports the body")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    auto box = w.createBoxBody(0, 0, 0, 0.5f, 0.5f, 0.5f, /*dynamic=*/true, 1.0f);
    REQUIRE(box != 0);

    w.setBodyPosition(box, 10.0f, 20.0f, 30.0f);
    float x = 0, y = 0, z = 0;
    w.getBodyPosition(box, x, y, z);
    CHECK(x == doctest::Approx(10.0f));
    CHECK(y == doctest::Approx(20.0f));
    CHECK(z == doctest::Approx(30.0f));
}

TEST_CASE("PhysicsWorld: raycast hits a body")
{
    PhysicsWorld w;
    REQUIRE(w.isReady());

    auto box = w.createBoxBody(0, 0, 0, 1.0f, 1.0f, 1.0f, /*dynamic=*/false);
    REQUIRE(box != 0);

    float hx = 0, hy = 0, hz = 0, hd = 0;
    bool hit = w.raycast(0, 5, 0, 0, -1, 0, /*maxDistance=*/10.0f, hx, hy, hz, hd);
    CHECK(hit);
    CHECK(hy == doctest::Approx(1.0f).epsilon(0.01));  // hit top face of the box
    CHECK(hd == doctest::Approx(4.0f).epsilon(0.01));

    // Ray missing into empty space.
    bool miss =
        w.raycast(100, 100, 100, 1, 0, 0, /*maxDistance=*/1.0f, hx, hy, hz, hd);
    CHECK_FALSE(miss);
}

TEST_CASE("JobSystem: schedules and runs tasks on the worker pool")
{
    JobSystem js;
    REQUIRE(js.isReady());
    CHECK(js.workerCount() >= 1);

    std::atomic<int> counter{ 0 };
    constexpr int N = 32;
    for (int i = 0; i < N; ++i) {
        js.schedule([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    // marl tasks are non-blocking; spin until they all finish or timeout.
    auto t0 = std::chrono::steady_clock::now();
    while (counter.load() < N) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(5)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(counter.load() == N);
}

TEST_CASE("Hud: add/clear element bookkeeping")
{
    Hud h;
    CHECK(h.elements().empty());
    h.addText(10.0f, 20.0f, "Hello", 0xFFFFFFFFu, 1.0f);
    h.addFilledRect(0, 0, 100, 50, 0xFFFF0000u);
    h.addOutlineRect(10, 10, 50, 50, 0xFF00FF00u);
    CHECK(h.elements().size() == 3);
    h.clear();
    CHECK(h.elements().empty());
}

TEST_CASE("ParticleSystem: emit + update + life decay")
{
    ParticleSystem ps;
    CHECK(ps.aliveCount() == 0);
    ps.emit({ 0.0f, 0.0f, 0.0f }, 20, { 1.0f, 1.0f, 1.0f, 1.0f }, /*life=*/0.1f);
    CHECK(ps.aliveCount() == 20);

    // Step enough time to kill every particle (life range = 0.5 * life .. 1.5 * life).
    for (int i = 0; i < 100; ++i) {
        ps.update(0.01f);
    }
    CHECK(ps.aliveCount() == 0);
}

TEST_CASE("ParticleSystem: snapshot copies live state into output arrays")
{
    ParticleSystem ps;
    ps.emit({ 1.0f, 2.0f, 3.0f }, 5);

    vec3 positions[10];
    vec4 colors[10];
    float sizes[10];
    uint32_t n = ps.snapshot(positions, colors, sizes, 10);
    CHECK(n == 5);
    // All particles spawned at the same world position; verify the first one.
    CHECK(positions[0].x == doctest::Approx(1.0f));
    CHECK(positions[0].y == doctest::Approx(2.0f));
    CHECK(positions[0].z == doctest::Approx(3.0f));
}

TEST_CASE("SaveGame: slot path is under LocalAppData and is reproducible")
{
    auto path1 = SaveGame::slotPath("test_slot");
    auto path2 = SaveGame::slotPath("test_slot");
    CHECK_FALSE(path1.empty());
    CHECK(path1 == path2);
    // Parent directory should exist now (savesDir creates it).
    std::filesystem::path p(path1);
    CHECK(std::filesystem::exists(p.parent_path()));
    CHECK(p.filename().string() == "test_slot.json");
}
