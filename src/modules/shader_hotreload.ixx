module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

export module shader_hotreload;

export class ShaderCompiler
{
   public:
    bool init(const char* dxcPath, const char* shaderDir);

    // Register a shader file to watch (relative to shaderDir). Returns watch index.
    size_t watch(const char* filename, const char* target);

    // Poll for file changes and recompile. Returns true if any shader was recompiled.
    bool poll(float dt);

    // Get compiled bytecode (nullptr if not yet hot-reloaded).
    const void* data(size_t idx) const;
    size_t size(size_t idx) const;

    // Was this shader recompiled in the last poll()?
    bool wasRecompiled(size_t idx) const;

    bool available() const;

   private:
    struct Watch
    {
        std::filesystem::path path;
        std::string target;
        std::vector<uint8_t> bytecode;
        std::filesystem::file_time_type lastWrite{};
        bool recompiled = false;
    };

    std::string dxcPath_;
    std::string shaderDir_;
    std::vector<Watch> watches_;
    float pollTimer_ = 0.f;

    bool compile(Watch& w);
};
