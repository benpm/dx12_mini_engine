// Separate TU for tinygltf so it doesn't mix with Windows.h/DirectX headers
// (stb_image conflicts with Windows macros when included in the same TU)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
