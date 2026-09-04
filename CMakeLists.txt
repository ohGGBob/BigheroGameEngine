cmake_minimum_required(VERSION 3.25)

project(BigHeroGameEngine
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "Vulkan-based 3D Game Engine"
    HOMEPAGE_URL "https://github.com/your-repo/BigHeroGameEngine")

# ===================== Policies & Standards =====================
# Use C++23 on Clang/GCC, C++20 on MSVC (incomplete C++23 support)
if(MSVC)
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)  # option() honors normal variables
endif()
if(POLICY CMP0114)
    cmake_policy(SET CMP0114 NEW)  # find_package() with version
endif()

# ===================== Build Configurations =====================
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# MSVC runtime library selection
if(MSVC)
    option(USE_MSVC_RUNTIME_LIBRARY_DLL "Use MSVC runtime library DLL" ON)
    if(NOT USE_MSVC_RUNTIME_LIBRARY_DLL)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ===================== Platform Detection =====================
set(BIGHERO_PLATFORM_WINDOWS ${WIN32})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BIGHERO_PLATFORM_LINUX TRUE)
else()
    set(BIGHERO_PLATFORM_LINUX FALSE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(BIGHERO_PLATFORM_MACOS TRUE)
else()
    set(BIGHERO_PLATFORM_MACOS FALSE)
endif()

# ===================== Vulkan SDK Detection =====================
set(VULKAN_SDK_PATH "" CACHE PATH "Vulkan SDK root directory (empty for auto-detect)")

if(NOT VULKAN_SDK_PATH)
    if(BIGHERO_PLATFORM_WINDOWS)
        file(GLOB VULKAN_SDK_CANDIDATES
            "D:/VulkanSDK/*" "C:/VulkanSDK/*"
            "$ENV{VULKAN_SDK}" "$ENV{ProgramFiles}/VulkanSDK/*")
    elseif(BIGHERO_PLATFORM_LINUX)
        file(GLOB VULKAN_SDK_CANDIDATES
            "/usr" "/usr/local" "/opt/vulkansdk" "$ENV{VULKAN_SDK}")
    elseif(BIGHERO_PLATFORM_MACOS)
        file(GLOB VULKAN_SDK_CANDIDATES
            "/usr/local" "/opt/vulkansdk" "$ENV{VULKAN_SDK}"
            "$ENV{HOME}/VulkanSDK")
    endif()

    foreach(_candidate ${VULKAN_SDK_CANDIDATES})
        if(IS_DIRECTORY "${_candidate}" AND EXISTS "${_candidate}/Include/vulkan/vulkan.h")
            set(VULKAN_SDK_PATH "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(VULKAN_SDK_PATH)
    set(ENV{VULKAN_SDK} "${VULKAN_SDK_PATH}")
    message(STATUS "Vulkan SDK: ${VULKAN_SDK_PATH}")
endif()

find_package(Vulkan REQUIRED)

# ===================== Compiler Options =====================
function(bighero_apply_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /std:c++20 /W4 /permissive- /utf-8 /MP
            /external:W3 /external:templates-
        )
        target_compile_definitions(${target} PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
            GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS
            _HAS_CXX23=0
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion
            -Wno-unused-parameter -Wno-missing-field-initializers
            -fdiagnostics-color=always
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target} PRIVATE
                -Wno-c++98-compat -Wno-c++98-compat-pedantic
                -Wno-switch-default -Wno-covered-switch-default
            )
        endif()
        target_compile_definitions(${target} PRIVATE
            GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS
        )
    endif()

    # Sanitizers (Debug builds only, opt-in)
    if(BIGHERO_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        if(NOT MSVC)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE
                -fsanitize=address,undefined
            )
        endif()
    endif()
endfunction()

# ===================== Third-party Dependencies =====================
# --- GLFW ---
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(BIGHERO_PLATFORM_LINUX)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
elseif(BIGHERO_PLATFORM_MACOS)
    set(GLFW_BUILD_COCOA ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(thirdparty/glfw)

# --- GLM (header-only, interface target) ---
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glm")
target_compile_features(glm INTERFACE cxx_std_20)

# --- Dear ImGui ---
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
bighero_apply_compile_options(imgui)

# --- miniaudio (header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniaudio")
target_compile_definitions(miniaudio INTERFACE MA_API_IMPLEMENTATION)

# --- stb (header-only) ---
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb")

# --- ReactPhysics3D via FetchContent ---
include(FetchContent)
FetchContent_Declare(
    reactphysics3d
    GIT_REPOSITORY https://github.com/DanielChappuis/reactphysics3d.git
    GIT_TAG v0.10.2
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
set(RP3D_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(RP3D_CODE_COVERAGE OFF CACHE BOOL "" FORCE)
set(RP3D_PROFILING_ENABLED OFF CACHE BOOL "" FORCE)
set(RP3D_LOGS_ENABLED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(reactphysics3d)

# Fix ReactPhysics3D v0.10.0 MSVC compile error (missing <chrono>)
set(_rp3d_logger "${reactphysics3d_SOURCE_DIR}/include/reactphysics3d/utils/DefaultLogger.h")
if(EXISTS "${_rp3d_logger}")
    file(READ "${_rp3d_logger}" _rp3d_logger_content)
    if(NOT _rp3d_logger_content MATCHES "#include <chrono>")
        string(REPLACE "#include <ctime>" "#include <ctime>\n#include <chrono>"
            _rp3d_logger_patched "${_rp3d_logger_content}")
        file(WRITE "${_rp3d_logger}" "${_rp3d_logger_patched}")
        message(STATUS "Patched ReactPhysics3D DefaultLogger.h: added #include <chrono>")
    endif()
endif()

# ===================== Shader Compilation =====================
find_program(GLSLC_EXECUTABLE NAMES glslc glslc.exe
    HINTS "${VULKAN_SDK_PATH}/Bin" "${VULKAN_SDK_PATH}/bin" ENV VULKAN_SDK
    PATH_SUFFIXES Bin bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install Vulkan SDK or set VULKAN_SDK_PATH.")
endif()
message(STATUS "glslc: ${GLSLC_EXECUTABLE}")

file(GLOB BIGHERO_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
set(BIGHERO_SHADER_OUTPUTS "")

foreach(_shader ${BIGHERO_SHADER_SOURCES})
    get_filename_component(_shader_full "${_shader}" NAME)
    string(REGEX REPLACE "\\.glsl$" "" _spv_name "${_shader_full}")
    string(TOLOWER "${_spv_name}" _stage_hint)

    if(_stage_hint MATCHES "vert")
        set(_stage "vert")
    elseif(_stage_hint MATCHES "frag")
        set(_stage "frag")
    elseif(_stage_hint MATCHES "comp")
        set(_stage "comp")
    elseif(_stage_hint MATCHES "geom")
        set(_stage "geom")
    elseif(_stage_hint MATCHES "tese")
        set(_stage "tese")
    elseif(_stage_hint MATCHES "tesc")
        set(_stage "tesc")
    else()
        message(FATAL_ERROR "Cannot infer shader stage from filename: ${_shader}")
    endif()

    set(_spv "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_spv_name}.spv")
    add_custom_command(
        OUTPUT "${_spv}"
        COMMAND "${GLSLC_EXECUTABLE}" -fshader-stage=${_stage} "${_shader}" -o "${_spv}"
        DEPENDS "${_shader}"
        COMMENT "glslc: ${_spv_name}.glsl -> ${_spv_name}.spv (${_stage})"
        VERBATIM)
    list(APPEND BIGHERO_SHADER_OUTPUTS "${_spv}")
endforeach()

add_custom_target(BigHeroShaders DEPENDS ${BIGHERO_SHADER_OUTPUTS})

# ===================== Engine Library =====================
# Explicit source listing (avoids GLOB_RECURSE CONFIGURE_DEPENDS issues)
file(GLOB BIGHERO_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h")
file(GLOB BIGHERO_PLATFORM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.h")
file(GLOB BIGHERO_RENDER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.h")
file(GLOB BIGHERO_SCENE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.h")
file(GLOB BIGHERO_EDITOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.h")
file(GLOB BIGHERO_AUDIO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/*.h")
file(GLOB BIGHERO_PHYSICS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/*.h")
file(GLOB BIGHERO_GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.h")

add_library(BigHeroEngine STATIC
    ${BIGHERO_CORE_SOURCES}
    ${BIGHERO_PLATFORM_SOURCES}
    ${BIGHERO_RENDER_SOURCES}
    ${BIGHERO_SCENE_SOURCES}
    ${BIGHERO_EDITOR_SOURCES}
    ${BIGHERO_AUDIO_SOURCES}
    ${BIGHERO_PHYSICS_SOURCES}
    ${BIGHERO_GAME_SOURCES})

add_library(BigHero::Engine ALIAS BigHeroEngine)

target_compile_features(BigHeroEngine PUBLIC cxx_std_20)
target_include_directories(BigHeroEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniaudio>
        $<INSTALL_INTERFACE:include/BigHero>
)
target_link_libraries(BigHeroEngine PUBLIC
    glm imgui Vulkan::Vulkan glfw reactphysics3d miniaudio stb)

# Platform-specific libraries
if(BIGHERO_PLATFORM_WINDOWS)
    target_link_libraries(BigHeroEngine PUBLIC ole32 winmm)
elseif(BIGHERO_PLATFORM_LINUX)
    target_link_libraries(BigHeroEngine PUBLIC dl pthread m)
elseif(BIGHERO_PLATFORM_MACOS)
    target_link_libraries(BigHeroEngine PUBLIC "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
    find_library(MOLTENVK MoltenVK)
    if(MOLTENVK)
        target_link_libraries(BigHeroEngine PUBLIC ${MOLTENVK})
    endif()
endif()

bighero_apply_compile_options(BigHeroEngine)

# ===================== Main Executable =====================
add_executable(BigHeroGameEngine
    src/main.cpp
    src/app/Application.cpp)
target_link_libraries(BigHeroGameEngine PRIVATE BigHero::Engine)
add_dependencies(BigHeroGameEngine BigHeroShaders)

# Output directory
set_target_properties(BigHeroGameEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

# Post-build: copy shaders and assets
add_custom_command(TARGET BigHeroGameEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:BigHeroGameEngine>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:BigHeroGameEngine>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:BigHeroGameEngine>/assets"
    COMMENT "Copy shaders and assets to output directory")

# ===================== Unit Tests =====================
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    add_executable(BigHeroTests src/tests/test_main.cpp)
    target_link_libraries(BigHeroTests PRIVATE BigHero::Engine)
    target_compile_features(BigHeroTests PRIVATE cxx_std_20)
    bighero_apply_compile_options(BigHeroTests)

    enable_testing()
    add_test(NAME BigHeroUnitTests COMMAND BigHeroTests)

    # Test with sanitizers if enabled
    if(BIGHERO_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(BigHeroTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    endif()
endif()

# ===================== Install & Export =====================
include(GNUInstallDirs)

install(TARGETS BigHeroGameEngine BigHeroTests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS BigHeroEngine
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY src/core/ src/platform/ src/render/ src/scene/ src/editor/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/BigHero
    FILES_MATCHING PATTERN "*.h")

install(DIRECTORY shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/BigHero/shaders
    FILES_MATCHING PATTERN "*.spv")

install(DIRECTORY assets/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/BigHero/assets)

# Export CMake config
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BigHeroGameEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BigHeroGameEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BigHeroGameEngineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BigHeroGameEngine)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/BigHeroGameEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/BigHeroGameEngineConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BigHeroGameEngine)

# ===================== CPack =====================
include(CPack)