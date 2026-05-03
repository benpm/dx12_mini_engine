cmake_minimum_required(VERSION 3.28)
project(dx12_experiments LANGUAGES CXX C)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
include(CTest)

option(USE_BINDLESS "Use bindless descriptor heap and root signature" ON)

#Use static CRT for MSVC and Clang - cl(so no VCRUNTIME dlls are required)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#Dependencies
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
find_package(directxtk12 CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)

include(FetchContent)

#Fetch gainput.GAINPUT_BUILD_SHARED must stay ON - gainput's CMakeLists.txt
#unconditionally calls target_link_libraries(gainput...) on Windows regardless of
#whether the shared target was built, so disabling it causes a CMake error.
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

#Fetch imgui(no CMakeLists.txt — build as static lib manually)
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

#Fetch tinyobjloader
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG afdd3fa785ac556d12e2e0d99f3bbf6239ab5f31
)
FetchContent_MakeAvailable(tinyobjloader)

#Fetch tinygltf(header - only, bundles stb_image)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.5
)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

#Fetch PerlinNoise(header - only)
FetchContent_Declare(
    perlin_noise
    GIT_REPOSITORY https://github.com/Reputeless/PerlinNoise.git
    GIT_TAG v3.0.0
)
FetchContent_MakeAvailable(perlin_noise)

#Fetch flecs ECS
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

#Fetch glaze(JSON serialization, header - only)
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v5.2.1
    SYSTEM
)
FetchContent_MakeAvailable(glaze)

#Fetch Lua 5.4(small C library, no CMakeLists.txt — build as static lib manually)
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.7
)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "(lua|luac|onelua)\\.c$")
add_library(lua_lib STATIC ${LUA_SOURCES})
target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR})
target_compile_definitions(lua_lib PRIVATE LUA_USE_WINDOWS)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lua_lib PRIVATE -Wno-everything)
endif()

# Fetch Tracy profiler (on-demand: zero overhead when viewer not connected)
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.13.1
)
FetchContent_MakeAvailable(tracy)

# Download Sponza GLB from KhronosGroup sample assets
set(SPONZA_URL "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/Sponza/glTF-Binary/Sponza.glb")
set(SPONZA_DEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/external_scenes/Sponza/Sponza.glb")

if(NOT EXISTS ${SPONZA_DEST})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/external_scenes/Sponza")
    message(STATUS "Downloading Sponza GLB from ${SPONZA_URL}...")
    file(DOWNLOAD ${SPONZA_URL} ${SPONZA_DEST} SHOW_PROGRESS)
endif()

# Additional assets
function(download_asset NAME URL)
    set(DEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/external_scenes/${NAME}/${NAME}.glb")
    if(NOT EXISTS ${DEST})
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/external_scenes/${NAME}")
        message(STATUS "Downloading ${NAME} GLB from ${URL}...")
        file(DOWNLOAD ${URL} ${DEST} SHOW_PROGRESS)
    endif()
endfunction()

download_asset("DamagedHelmet" "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb")
download_asset("SciFiHelmet" "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/SciFiHelmet/glTF-Binary/SciFiHelmet.glb")
download_asset("Corset" "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/Corset/glTF-Binary/Corset.glb")
set_target_properties(TracyClient PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(TracyClient PRIVATE -Wno-everything)
endif()

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
    set(BINDLESS_FLAG "")
    if(USE_BINDLESS)
        set(BINDLESS_FLAG "-DUSE_BINDLESS=1")
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${DXC_EXECUTABLE} -T ${SHADER_TYPE}_${SHADER_MODEL} -E main
        -I ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders ${BINDLESS_FLAG}
        -Fh ${OUTPUT_FILE} -Vn ${VAR_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/sky.hlsli
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
    src/application/application.cpp
    src/application/ui.cpp
    src/application/ui_menu.cpp
    src/application/ui_inspector.cpp
    src/application/ui_metrics.cpp
    src/application/ui_overlay.cpp
    src/application/ui_util.cpp
    src/application/render.cpp
    src/application/setup.cpp
    src/application/scene.cpp
    src/billboard.cpp
    src/config.cpp
    src/gizmo.cpp
    src/input.cpp
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
    src/ssao.cpp
    src/shadow.cpp
    src/outline.cpp
    src/render_graph.cpp
    src/gbuffer.cpp
    src/restir.cpp
    src/lua_scripting.cpp
    src/lua_scripting_impl.cpp
    src/gfx/d3d12_backend.cpp
    src/gfx/d3d12_command.cpp
    src/gfx/d3d12_swapchain.cpp
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
    src/modules/config.ixx
    src/modules/gizmo.ixx
    src/modules/imgui_layer.ixx
    src/modules/shader_hotreload.ixx
    src/modules/object_picking.ixx
    src/modules/terrain.ixx
    src/modules/scene_file.ixx
    src/modules/ssao.ixx
    src/modules/shadow.ixx
    src/modules/outline.ixx
    src/modules/render_graph.ixx
    src/modules/gbuffer.ixx
    src/modules/restir.ixx
    src/modules/lua_scripting.ixx
    src/modules/application.ixx
    src/modules/window.ixx
    src/modules/gfx.ixx
)
target_compile_features(main PRIVATE cxx_std_23)
set_target_properties(main PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(main SYSTEM PRIVATE ${tinygltf_SOURCE_DIR} ${perlin_noise_SOURCE_DIR})

# -------------------------------
# Shader compilation
# -------------------------------
compile_shader(src/shaders/vertex_shader.hlsl vs 6_0 VS_HDR g_vertex_shader)
compile_shader(src/shaders/pixel_shader.hlsl ps 6_0 PS_HDR g_pixel_shader)
compile_shader(src/shaders/fullscreen_vs.hlsl vs 6_0 FULLSCREEN_VS_HDR g_fullscreen_vs)
compile_shader(src/shaders/bloom_prefilter_ps.hlsl ps 6_0 BLOOM_PREFILTER_HDR g_bloom_prefilter_ps)
compile_shader(src/shaders/bloom_downsample_ps.hlsl ps 6_0 BLOOM_DOWNSAMPLE_HDR g_bloom_downsample_ps)
compile_shader(src/shaders/bloom_upsample_ps.hlsl ps 6_0 BLOOM_UPSAMPLE_HDR g_bloom_upsample_ps)
compile_shader(src/shaders/bloom_composite_ps.hlsl ps 6_0 BLOOM_COMPOSITE_HDR g_bloom_composite_ps)
compile_shader(src/shaders/billboard_vs.hlsl vs 6_0 BILLBOARD_VS_HDR g_billboard_vs)
compile_shader(src/shaders/billboard_ps.hlsl ps 6_0 BILLBOARD_PS_HDR g_billboard_ps)
compile_shader(src/shaders/id_ps.hlsl ps 6_0 ID_PS_HDR g_id_ps)
compile_shader(src/shaders/outline_vs.hlsl vs 6_0 OUTLINE_VS_HDR g_outline_vs)
compile_shader(src/shaders/outline_ps.hlsl ps 6_0 OUTLINE_PS_HDR g_outline_ps)
compile_shader(src/shaders/normal_ps.hlsl ps 6_0 NORMAL_PS_HDR g_normal_ps)
compile_shader(src/shaders/ssao_ps.hlsl ps 6_0 SSAO_PS_HDR g_ssao_ps)
compile_shader(src/shaders/ssao_blur_ps.hlsl ps 6_0 SSAO_BLUR_PS_HDR g_ssao_blur_ps)
compile_shader(src/shaders/grid_vs.hlsl vs 6_0 GRID_VS_HDR g_grid_vs)
compile_shader(src/shaders/grid_ps.hlsl ps 6_0 GRID_PS_HDR g_grid_ps)
compile_shader(src/shaders/gbuffer_ps.hlsl ps 6_0 GBUFFER_PS_HDR g_gbuffer_ps)
add_custom_target(shaders DEPENDS
    ${VS_HDR} ${PS_HDR}
    ${FULLSCREEN_VS_HDR}
    ${BLOOM_PREFILTER_HDR} ${BLOOM_DOWNSAMPLE_HDR}
    ${BLOOM_UPSAMPLE_HDR} ${BLOOM_COMPOSITE_HDR}
    ${BILLBOARD_VS_HDR} ${BILLBOARD_PS_HDR}
    ${ID_PS_HDR}
    ${OUTLINE_VS_HDR} ${OUTLINE_PS_HDR}
    ${NORMAL_PS_HDR} ${SSAO_PS_HDR} ${SSAO_BLUR_PS_HDR}
    ${GRID_VS_HDR} ${GRID_PS_HDR}
    ${GBUFFER_PS_HDR}
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
    SHADER_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
    MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/models"
    SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/scenes"
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/scripts"
    ICONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
)
if(USE_BINDLESS)
    target_compile_definitions(main PRIVATE USE_BINDLESS=1)
endif()

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

# Copy MaterialIcons-Regular.ttf (icon font for UI menus)
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/MaterialIcons-Regular.ttf
    $<TARGET_FILE_DIR:main>/MaterialIcons-Regular.ttf
    COMMENT "Copying MaterialIcons-Regular.ttf to output directory"
)

target_link_libraries(main PRIVATE
    TracyClient
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
    lua_lib
    Microsoft::DirectX-Headers
)

if(BUILD_TESTING)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest
        GIT_TAG v2.4.11
    )
    FetchContent_MakeAvailable(doctest)
    include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

    add_executable(unit_tests
        tests/math_types_tests.cpp
    )
    target_compile_features(unit_tests PRIVATE cxx_std_23)
    target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(unit_tests PRIVATE doctest::doctest)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        target_compile_options(unit_tests PRIVATE
            -Wall -Wno-c++98-compat -fcolor-diagnostics
            $<$<CONFIG:Debug>:-O0 -g>
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(unit_tests PRIVATE
            /W3
            $<$<CONFIG:Debug>:/Od /Zi>
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
        )
    else()
        target_compile_options(unit_tests PRIVATE -Wall)
    endif()

    doctest_discover_tests(unit_tests)

    add_executable(lua_scripting_tests
        tests/lua_scripting_tests.cpp
        src/lua_scripting_impl.cpp
    )
    target_compile_features(lua_scripting_tests PRIVATE cxx_std_23)
    target_include_directories(lua_scripting_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(lua_scripting_tests PRIVATE doctest::doctest lua_lib flecs::flecs_static spdlog::spdlog)
    target_compile_definitions(lua_scripting_tests PRIVATE FMT_CONSTEVAL=)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        target_compile_options(lua_scripting_tests PRIVATE
            -Wall -Wno-c++98-compat -fcolor-diagnostics
            $<$<CONFIG:Debug>:-O0 -g>
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(lua_scripting_tests PRIVATE
            /W3
            $<$<CONFIG:Debug>:/Od /Zi>
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
        )
    else()
        target_compile_options(lua_scripting_tests PRIVATE -Wall)
    endif()
    doctest_discover_tests(lua_scripting_tests)

    # gfx abstraction smoke tests (D3D12 backend on WARP)
    add_executable(gfx_tests
        tests/gfx_tests.cpp
        src/gfx/d3d12_backend.cpp
        src/gfx/d3d12_command.cpp
        src/gfx/d3d12_swapchain.cpp
    )
    target_compile_features(gfx_tests PRIVATE cxx_std_23)
    set_target_properties(gfx_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_include_directories(gfx_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_sources(gfx_tests
        PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
        FILES
        src/modules/math.ixx
        src/modules/common.ixx
        src/modules/gfx.ixx
    )
    target_link_libraries(gfx_tests PRIVATE
        doctest::doctest
        spdlog::spdlog
        Microsoft::DirectXTK12
        Microsoft::DirectX-Headers
        d3d12.lib dxgi.lib dxguid.lib
    )
    target_compile_definitions(gfx_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        target_compile_options(gfx_tests PRIVATE
            -Wall -Wno-c++98-compat -fcolor-diagnostics
            $<$<CONFIG:Debug>:-O0 -g>
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        )
    endif()
    doctest_discover_tests(gfx_tests)
endif()
