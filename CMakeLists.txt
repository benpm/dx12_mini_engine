cmake_minimum_required(VERSION 3.28)
project(dx12_experiments LANGUAGES CXX C)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Use static CRT for MSVC and Clang-cl (so no VCRUNTIME dlls are required)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -------------------------------
# Dependencies
# -------------------------------
find_package(directxtk12 CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)

include(FetchContent)

# Fetch gainput. GAINPUT_BUILD_SHARED must stay ON - gainput's CMakeLists.txt
# unconditionally calls target_link_libraries(gainput ...) on Windows regardless of
# whether the shared target was built, so disabling it causes a CMake error.
set(GAINPUT_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(GAINPUT_TESTS OFF CACHE BOOL "" FORCE)
set(GAINPUT_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    gainput
    GIT_REPOSITORY https://github.com/jkuhlmann/gainput.git
    GIT_TAG 2be0a50089eafcc6fccb66142180082e48f27f4c
)
FetchContent_MakeAvailable(gainput)
target_include_directories(gainputstatic PUBLIC ${gainput_SOURCE_DIR}/lib/include)
target_compile_definitions(gainputstatic PRIVATE _CRT_SECURE_NO_WARNINGS)
foreach(_t gainputstatic gainput)
    if(TARGET ${_t})
        target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${_t} PRIVATE -Wno-deprecated-declarations)
            if(_t STREQUAL "gainput")
                target_link_options(${_t} PRIVATE "LINKER:/ignore:importeddllmain")
            endif()
        endif()
    endif()
endforeach()

# Fetch imgui (no CMakeLists.txt — build as static lib manually)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.6
)
FetchContent_MakeAvailable(imgui)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC d3d12 dxgi)
target_compile_definitions(imgui_lib PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(imgui_lib PRIVATE -Wno-everything)
endif()

# Fetch tinyobjloader
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG afdd3fa785ac556d12e2e0d99f3bbf6239ab5f31
)
FetchContent_MakeAvailable(tinyobjloader)

# Fetch tinygltf (header-only, bundles stb_image)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.5
)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

# Fetch PerlinNoise (header-only)
FetchContent_Declare(
    perlin_noise
    GIT_REPOSITORY https://github.com/Reputeless/PerlinNoise.git
    GIT_TAG v3.0.0
)
FetchContent_MakeAvailable(perlin_noise)

# Fetch flecs ECS
set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_PIC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG v4.1.4
)
FetchContent_MakeAvailable(flecs)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(flecs_static PRIVATE -Wno-everything)
endif()

# Fetch glaze (JSON serialization, header-only)
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v5.2.1
    SYSTEM
)
FetchContent_MakeAvailable(glaze)

# -------------------------------
# Find DXC shader compiler
# -------------------------------
find_program(DXC_EXECUTABLE dxc
    HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
)
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "Could not find dxc.exe (DirectX Shader Compiler)")
endif()
message(STATUS "Found DXC: ${DXC_EXECUTABLE}")

# Compile HLSL shaders to .h
function(compile_shader SHADER_FILE SHADER_TYPE SHADER_MODEL OUTPUT_VAR VAR_NAME)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}_cso.h")
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${DXC_EXECUTABLE} -T ${SHADER_TYPE}_${SHADER_MODEL} -E main
        -Fh ${OUTPUT_FILE} -Vn ${VAR_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE} -> ${SHADER_NAME}_cso.h"
        VERBATIM
    )
    set(${OUTPUT_VAR} ${OUTPUT_FILE} PARENT_SCOPE)
endfunction()

# -------------------------------
# Targets
# -------------------------------
add_executable(main
    src/main.cpp
    src/application.cpp
    src/application_ui.cpp
    src/application_render.cpp
    src/application_setup.cpp
    src/billboard.cpp
    src/scene.cpp
    src/bloom.cpp
    src/imgui_layer.cpp
    src/shader_hotreload.cpp
    src/gltf_impl.cpp
    src/command_queue.cpp
    src/window.cpp
    src/logging.cpp
    src/object_picking.cpp
    src/terrain.cpp
    src/glaze_impl.cpp
    src/scene_file.cpp
    src/application_scene.cpp
    src/ssao.cpp
    resources.rc
)
target_sources(main
    PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
    FILES
    src/modules/logging.ixx
    src/modules/common.ixx
    src/modules/input.ixx
    src/modules/camera.ixx
    src/modules/command_queue.ixx
    src/modules/math.ixx
    src/modules/ecs_components.ixx
    src/modules/scene.ixx
    src/modules/bloom.ixx
    src/modules/billboard.ixx
    src/modules/imgui_layer.ixx
    src/modules/shader_hotreload.ixx
    src/modules/object_picking.ixx
    src/modules/terrain.ixx
    src/modules/scene_file.ixx
    src/modules/ssao.ixx
    src/modules/application.ixx
    src/modules/window.ixx
)
target_compile_features(main PRIVATE cxx_std_23)
set_target_properties(main PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(main SYSTEM PRIVATE ${tinygltf_SOURCE_DIR} ${perlin_noise_SOURCE_DIR})

# -------------------------------
# Shader compilation
# -------------------------------
compile_shader(src/vertex_shader.hlsl vs 6_0 VS_HDR g_vertex_shader)
compile_shader(src/pixel_shader.hlsl ps 6_0 PS_HDR g_pixel_shader)
compile_shader(src/fullscreen_vs.hlsl vs 6_0 FULLSCREEN_VS_HDR g_fullscreen_vs)
compile_shader(src/bloom_prefilter_ps.hlsl ps 6_0 BLOOM_PREFILTER_HDR g_bloom_prefilter_ps)
compile_shader(src/bloom_downsample_ps.hlsl ps 6_0 BLOOM_DOWNSAMPLE_HDR g_bloom_downsample_ps)
compile_shader(src/bloom_upsample_ps.hlsl ps 6_0 BLOOM_UPSAMPLE_HDR g_bloom_upsample_ps)
compile_shader(src/bloom_composite_ps.hlsl ps 6_0 BLOOM_COMPOSITE_HDR g_bloom_composite_ps)
compile_shader(src/billboard_vs.hlsl vs 6_0 BILLBOARD_VS_HDR g_billboard_vs)
compile_shader(src/billboard_ps.hlsl ps 6_0 BILLBOARD_PS_HDR g_billboard_ps)
compile_shader(src/id_ps.hlsl ps 6_0 ID_PS_HDR g_id_ps)
compile_shader(src/outline_vs.hlsl vs 6_0 OUTLINE_VS_HDR g_outline_vs)
compile_shader(src/outline_ps.hlsl ps 6_0 OUTLINE_PS_HDR g_outline_ps)
compile_shader(src/normal_ps.hlsl ps 6_0 NORMAL_PS_HDR g_normal_ps)
compile_shader(src/ssao_ps.hlsl ps 6_0 SSAO_PS_HDR g_ssao_ps)
compile_shader(src/ssao_blur_ps.hlsl ps 6_0 SSAO_BLUR_PS_HDR g_ssao_blur_ps)
add_custom_target(shaders DEPENDS
    ${VS_HDR} ${PS_HDR}
    ${FULLSCREEN_VS_HDR}
    ${BLOOM_PREFILTER_HDR} ${BLOOM_DOWNSAMPLE_HDR}
    ${BLOOM_UPSAMPLE_HDR} ${BLOOM_COMPOSITE_HDR}
    ${BILLBOARD_VS_HDR} ${BILLBOARD_PS_HDR}
    ${ID_PS_HDR}
    ${OUTLINE_VS_HDR} ${OUTLINE_PS_HDR}
    ${NORMAL_PS_HDR} ${SSAO_PS_HDR} ${SSAO_BLUR_PS_HDR}
)
add_dependencies(main shaders)

# -------------------------------
# Compiler options (base + per-config)
# -------------------------------
target_include_directories(main PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    message(STATUS "Clang detected, configuring compile flags")
    target_compile_options(main PRIVATE
        -Wall -Wno-c++98-compat -fcolor-diagnostics
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(STATUS "MSVC detected, configuring compile flags")
    target_compile_options(main PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
    )
else()
    message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
    target_compile_options(main PRIVATE -Wall)
endif()

# -------------------------------
# Platform & configuration defines
# -------------------------------
target_compile_definitions(main PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    DXC_PATH="${DXC_EXECUTABLE}"
    SHADER_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/models"
    SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/scenes"
)

target_compile_definitions(main PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
)

# -------------------------------
# Outputs & linking
# -------------------------------
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(main PRIVATE "LINKER:/SUBSYSTEM:WINDOWS" "LINKER:/ignore:importeddllmain")
else()
    target_link_options(main PRIVATE /SUBSYSTEM:WINDOWS)
endif()

# Copy Roboto-Medium.ttf (bundled with imgui) next to the executable
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf
    $<TARGET_FILE_DIR:main>/Roboto-Medium.ttf
    COMMENT "Copying Roboto-Medium.ttf to output directory"
)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/light.png
    $<TARGET_FILE_DIR:main>/light.png
    COMMENT "Copying light sprite to output directory"
)

target_link_libraries(main PRIVATE
    d3d12.lib dxgi.lib dxguid.lib uuid.lib
    d3dcompiler.lib
    kernel32.lib user32.lib
    comdlg32.lib advapi32.lib shell32.lib
    ole32.lib oleaut32.lib
    runtimeobject.lib
    Microsoft::DirectXTK12
    spdlog::spdlog
    gainputstatic
    tinyobjloader
    imgui_lib
    flecs::flecs_static
    glaze::glaze
    Microsoft::DirectX-Headers
)
