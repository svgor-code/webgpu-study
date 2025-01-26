cmake_minimum_required(VERSION 3.13) # CMake version check
project(app)                         # Create project "app"
set(CMAKE_CXX_STANDARD 20)           # Enable C++20 standard

add_executable(app main.cpp webgpu-utils.h webgpu-utils.cpp)

set(DAWN_FETCH_DEPENDENCIES ON)
add_subdirectory("dawn" EXCLUDE_FROM_ALL)
target_link_libraries(app PRIVATE dawn::webgpu_dawn glfw webgpu_glfw)