cmake_minimum_required(VERSION 3.16)

project(BigheroGameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_subdirectory(glfw)

include_directories(
    ${Vulkan_INCLUDE_DIRS}
    glm
    stb
)

file(GLOB SOURCES src/*.cpp)

add_executable(BigheroGameEngine ${SOURCES})

target_link_libraries(BigheroGameEngine
    glfw
    ${Vulkan_LIBRARIES}
)