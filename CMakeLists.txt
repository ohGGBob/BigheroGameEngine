cmake_minimum_required(VERSION 3.16)

set(VULKAN_SDK "D:/VulkanSDK/1.4.350.0")
set(ENV{VULKAN_SDK} ${VULKAN_SDK})

project(BigheroGameEngine LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

# 第三方库路径更新为thirdparty下
add_subdirectory(thirdparty/glfw)

set(SOURCES
    src/main.cpp
)
add_executable(BigheroGameEngine ${SOURCES})

target_include_directories(BigheroGameEngine PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    thirdparty/glm
    thirdparty/stb
)

target_link_libraries(BigheroGameEngine
    glfw
    ${Vulkan_LIBRARIES}
)

# 编译时复制shaders、assets资源到exe同目录
file(COPY shaders assets DESTINATION ${CMAKE_BINARY_DIR})