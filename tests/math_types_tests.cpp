#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "math_types.h"

TEST_CASE("vec2 dot and length are correct")
{
    const vec2 a{ 3.0f, 4.0f };
    const vec2 b{ 2.0f, -1.0f };

    CHECK(dot(a, b) == doctest::Approx(2.0f));
    CHECK(length(a) == doctest::Approx(5.0f));
}

TEST_CASE("vec3 normalize handles non-zero and zero vectors")
{
    const vec3 v{ 0.0f, 3.0f, 4.0f };
    const vec3 n = normalize(v);

    CHECK(n.x == doctest::Approx(0.0f));
    CHECK(n.y == doctest::Approx(0.6f));
    CHECK(n.z == doctest::Approx(0.8f));

    const vec3 zero{ 0.0f, 0.0f, 0.0f };
    const vec3 nz = normalize(zero);
    CHECK(nz.x == doctest::Approx(0.0f));
    CHECK(nz.y == doctest::Approx(0.0f));
    CHECK(nz.z == doctest::Approx(0.0f));
}

TEST_CASE("vec3 cross product follows right-hand rule")
{
    const vec3 xAxis{ 1.0f, 0.0f, 0.0f };
    const vec3 yAxis{ 0.0f, 1.0f, 0.0f };
    const vec3 zAxis = cross(xAxis, yAxis);

    CHECK(zAxis.x == doctest::Approx(0.0f));
    CHECK(zAxis.y == doctest::Approx(0.0f));
    CHECK(zAxis.z == doctest::Approx(1.0f));
}

TEST_CASE("translate matrix writes translation components")
{
    const mat4 t = translate(1.5f, -2.0f, 3.25f);

    CHECK(t._11 == doctest::Approx(1.0f));
    CHECK(t._22 == doctest::Approx(1.0f));
    CHECK(t._33 == doctest::Approx(1.0f));
    CHECK(t._44 == doctest::Approx(1.0f));
    CHECK(t._41 == doctest::Approx(1.5f));
    CHECK(t._42 == doctest::Approx(-2.0f));
    CHECK(t._43 == doctest::Approx(3.25f));
}

TEST_CASE("uniform scale matrix scales diagonal terms")
{
    const mat4 s = scale(2.5f);

    CHECK(s._11 == doctest::Approx(2.5f));
    CHECK(s._22 == doctest::Approx(2.5f));
    CHECK(s._33 == doctest::Approx(2.5f));
    CHECK(s._44 == doctest::Approx(1.0f));
}
