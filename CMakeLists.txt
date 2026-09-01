cmake_minimum_required(VERSION 3.20)

project(BigHeroGameEngine LANGUAGES C CXX)

# ===================== 全局编译设置 =====================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===================== Vulkan SDK 探测 =====================
# 优先级：缓存变量 VULKAN_SDK_PATH > 环境变量 > 常见安装目录自动探测（多版本时取最新）
set(VULKAN_SDK_PATH "" CACHE PATH "Vulkan SDK 根目录（留空则自动探测）")

if(NOT VULKAN_SDK_PATH)
    file(GLOB VULKAN_SDK_CANDIDATES "D:/VulkanSDK/*" "C:/VulkanSDK/*")
    foreach(_candidate ${VULKAN_SDK_CANDIDATES})
        if(IS_DIRECTORY "${_candidate}")
            set(VULKAN_SDK_PATH "${_candidate}")
        endif()
    endforeach()
endif()

if(VULKAN_SDK_PATH)
    set(ENV{VULKAN_SDK} "${VULKAN_SDK_PATH}")
    message(STATUS "Vulkan SDK: ${VULKAN_SDK_PATH}")
endif()

find_package(Vulkan REQUIRED)

# ===================== 第三方依赖 =====================
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/glfw)

# glm 为头文件库，以接口目标形式引入
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glm")

# Dear ImGui（核心 + GLFW/Vulkan后端）
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# ===================== 着色器编译（构建期自动执行） =====================
# glslc 随 SDK 分发，优先在 SDK 的 Bin 目录查找
find_program(GLSLC_EXECUTABLE NAMES glslc glslc.exe
    HINTS "${VULKAN_SDK_PATH}/Bin" "${VULKAN_SDK_PATH}/bin" ENV VULKAN_SDK
    PATH_SUFFIXES Bin bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "未找到 glslc，请安装 Vulkan SDK 或通过 VULKAN_SDK_PATH 指定 SDK 目录")
endif()
message(STATUS "glslc: ${GLSLC_EXECUTABLE}")

# 每个 shaders/*.glsl 编译为同名 *.spv，阶段由文件名推断（vert/frag/comp/geom/tesc/tese）
file(GLOB BIGHERO_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
set(BIGHERO_SHADER_OUTPUTS "")
foreach(_shader ${BIGHERO_SHADER_SOURCES})
    # 完整文件名去掉.glsl后缀作为spv名称（支持 shadow.vert.glsl 这类多级命名）
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
        message(FATAL_ERROR "无法从文件名推断着色器阶段: ${_shader}")
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

# ===================== 引擎静态库 =====================
file(GLOB_RECURSE BIGHERO_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h"     "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.h"   "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.h"    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.h"   "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.cpp")

add_library(BigHeroEngine STATIC ${BIGHERO_ENGINE_SOURCES})
add_library(BigHero::Engine ALIAS BigHeroEngine)

target_compile_features(BigHeroEngine PUBLIC cxx_std_20)
target_include_directories(BigHeroEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb")
target_link_libraries(BigHeroEngine PUBLIC glm imgui Vulkan::Vulkan glfw)

if(MSVC)
    target_compile_options(BigHeroEngine PUBLIC /W4 /permissive- /utf-8 /MP)
    target_compile_definitions(BigHeroEngine PUBLIC
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)
endif()

# ===================== 可执行程序 =====================
# Application 为应用层（资源装配+主循环），与 main 入口一同编译为可执行文件；
# 引擎核心（core/platform/render/scene/editor）保留在 BigHeroEngine 静态库中。
add_executable(BigHeroGameEngine
    src/main.cpp
    src/app/Application.cpp)
target_link_libraries(BigHeroGameEngine PRIVATE BigHero::Engine)
add_dependencies(BigHeroGameEngine BigHeroShaders)

# 产物统一输出到 bin/<配置>，VS调试工作目录与资源目录保持一致
set_target_properties(BigHeroGameEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

# 构建后把编译出的 SPIR-V 与资源目录拷到可执行文件旁（多配置下自动落在对应配置目录）
add_custom_command(TARGET BigHeroGameEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:BigHeroGameEngine>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:BigHeroGameEngine>/assets"
    COMMENT "复制着色器与资源到输出目录")

# ===================== 单元测试（纯逻辑，不依赖 GPU 运行） =====================
add_executable(BigHeroTests src/tests/test_main.cpp)
target_link_libraries(BigHeroTests PRIVATE BigHero::Engine)
target_compile_features(BigHeroTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(BigHeroTests PRIVATE /W4 /utf-8)
    # 与引擎保持一致：Vulkan NDC（z∈[0,1]）与弧度制，保证 Frustum 等依赖投影约定的单测正确
    target_compile_definitions(BigHeroTests PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)
endif()

# ===================== 单元测试（CTest 集成） =====================
enable_testing()
add_test(NAME BigHeroUnitTests COMMAND BigHeroTests)

# ===================== 安装与导出 =====================
include(GNUInstallDirs)
install(TARGETS BigHeroGameEngine BigHeroTests
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS BigHeroEngine
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
# 导出引擎头文件（按模块目录保留结构）
install(DIRECTORY src/core/ src/platform/ src/render/ src/scene/ src/editor/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/BigHero
    FILES_MATCHING PATTERN "*.h")

