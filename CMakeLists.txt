cmake_minimum_required(VERSION 3.16)

set(VULKAN_SDK "D:/VulkanSDK/1.4.350.0")
set(ENV{VULKAN_SDK} ${VULKAN_SDK})

project(BigHeroGameEngine LANGUAGES C CXX)

# 删除旧的C++17全局标准，改用目标单独指定C++20
find_package(Vulkan REQUIRED)

# 第三方库路径更新为thirdparty下
add_subdirectory(thirdparty/glfw)

set(SOURCES
    src/main.cpp
    src/render/shader_loader.h
    src/render/ubo_structs.h
    src/render/ubo_buffer.h
    src/render/descriptor_set.h
)
add_executable(BigHeroGameEngine ${SOURCES})

# 单独给可执行目标开启C++20
target_compile_features(BigHeroGameEngine PRIVATE cxx_std_20)

target_include_directories(BigHeroGameEngine PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    thirdparty/glm
    thirdparty/stb
    src
)

target_link_libraries(BigHeroGameEngine
    glfw
    ${Vulkan_LIBRARIES}
)

# 编译时复制shaders、assets资源到exe同目录
file(COPY shaders assets DESTINATION ${CMAKE_BINARY_DIR})