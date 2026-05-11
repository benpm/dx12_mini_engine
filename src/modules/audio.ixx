module;

#include <string>

export module audio;

// AudioSystem — thin wrapper around miniaudio's ma_engine. Owned by
// Application. Initialised in the ctor (best-effort; failure is non-fatal so
// headless test runs keep working when no audio device is available).
//
// We hold the engine as `void*` rather than a forward-declared `ma_engine*`
// so the module interface stays free of miniaudio's headers AND so the
// implementation TU can include miniaudio.h in the global module fragment
// without colliding with a duplicate forward declaration in module purview.
export class AudioSystem
{
   public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool isReady() const { return ready; }

    bool playSound(const std::string& path, float volume = 1.0f);
    void setListener(float px, float py, float pz, float fx, float fy, float fz);

   private:
    void* engine = nullptr;  // ma_engine* under the hood
    bool ready = false;
};
