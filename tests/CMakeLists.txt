cmake_minimum_required(VERSION 3.16)
project(ESPressioCommandTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

enable_testing()
add_executable(test_command test_command.cpp)
target_include_directories(test_command PRIVATE ../src)
add_test(NAME ESPressioCommand COMMAND test_command)
