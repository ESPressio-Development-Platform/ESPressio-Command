cmake_minimum_required(VERSION 3.16)
project(ESPressioCommand LANGUAGES CXX)
add_library(ESPressio_Command INTERFACE)
target_include_directories(ESPressio_Command INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
