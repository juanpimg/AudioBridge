cmake_minimum_required(VERSION 3.20)

project(AudioBridgeReceiver VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AudioBridgeReceiver WIN32
    main.cpp
    miniaudio.h
    ../common/SharedAudioProtocol.h
)

target_include_directories(AudioBridgeReceiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(AudioBridgeReceiver PRIVATE
    user32
    gdi32
    comctl32
    shell32
    ole32
)

set_target_properties(AudioBridgeReceiver PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
