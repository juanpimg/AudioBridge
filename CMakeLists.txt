cmake_minimum_required(VERSION 3.22)

project(AudioBridgeProject VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(plugin)
add_subdirectory(receiver)
