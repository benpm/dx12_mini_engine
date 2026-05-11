// miniaudio implementation lives in this TU so the heavy single-header is
// compiled only once. Defining MA_IMPLEMENTATION before the include emits the
// implementation; everywhere else only the API is visible (we forward-declare
// ma_engine in audio.ixx to avoid spreading the include).
module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#define MA_IMPLEMENTATION
#define MA_NO_ENCODING 1
#define MA_NO_GENERATION 1
#include <miniaudio.h>
#include <spdlog/spdlog.h>

#include <string>

module audio;

namespace
{
    inline ma_engine* asEngine(void* p)
    {
        return static_cast<ma_engine*>(p);
    }
}  // namespace

// C API consumed by non-module TUs (e.g. lua_scripting_impl.cpp).
extern "C" int engine_audio_play_sound(void* audioPtr, const char* path, float volume)
{
    auto* a = static_cast<AudioSystem*>(audioPtr);
    if (!a || !path) {
        return 0;
    }
    return a->playSound(path, volume) ? 1 : 0;
}

AudioSystem::AudioSystem()
{
    auto* eng = new ma_engine{};
    ma_engine_config cfg = ma_engine_config_init();
    ma_result rc = ma_engine_init(&cfg, eng);
    if (rc != MA_SUCCESS) {
        spdlog::warn("AudioSystem: ma_engine_init failed (rc={}); audio disabled", int(rc));
        delete eng;
        engine = nullptr;
        ready = false;
        return;
    }
    engine = eng;
    ready = true;
    spdlog::info("AudioSystem: miniaudio engine initialised");
}

AudioSystem::~AudioSystem()
{
    if (engine) {
        ma_engine_uninit(asEngine(engine));
        delete asEngine(engine);
        engine = nullptr;
    }
}

bool AudioSystem::playSound(const std::string& path, float volume)
{
    if (!ready || !engine) {
        return false;
    }
    ma_result rc = ma_engine_play_sound(asEngine(engine), path.c_str(), nullptr);
    if (rc != MA_SUCCESS) {
        spdlog::warn("AudioSystem: failed to play '{}' (rc={})", path, int(rc));
        return false;
    }
    if (volume != 1.0f) {
        ma_engine_set_volume(asEngine(engine), volume);
    }
    return true;
}

void AudioSystem::setListener(float px, float py, float pz, float fx, float fy, float fz)
{
    if (!ready || !engine) {
        return;
    }
    ma_engine_listener_set_position(asEngine(engine), 0, px, py, pz);
    ma_engine_listener_set_direction(asEngine(engine), 0, fx, fy, fz);
}
