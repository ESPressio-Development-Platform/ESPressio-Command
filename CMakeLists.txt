cmake_minimum_required(VERSION 3.16)
project(ESPressioCommand LANGUAGES CXX)

add_library(ESPressio_Command INTERFACE)
target_include_directories(ESPressio_Command INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ESPressio_Command INTERFACE cxx_std_17)
target_link_libraries(ESPressio_Command INTERFACE
    ESPressio_System
    ESPressio_Primitive
    ESPressio_Task
    ESPressio_Threads
    ESPressio_Timing
    ESPressio_Serializable
    ESPressio_Persistence)
