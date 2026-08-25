cmake_minimum_required(VERSION 3.16)
project(ESPressioCommandTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)

FetchContent_Declare(
    ESPressioObservable
    GIT_REPOSITORY https://github.com/Flowduino/ESPressio-Observable.git
    GIT_TAG 3.0.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(ESPressioObservable)
if(NOT espressioobservable_POPULATED)
    FetchContent_Populate(ESPressioObservable)
endif()
set(ESPRESSIO_OBSERVABLE_INCLUDE ${espressioobservable_SOURCE_DIR}/src)

FetchContent_Declare(
    ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.3
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(ArduinoJson)
if(NOT arduinojson_POPULATED)
    FetchContent_Populate(ArduinoJson)
endif()
set(ARDUINOJSON_INCLUDE ${arduinojson_SOURCE_DIR}/src)

enable_testing()

add_executable(test_command test_command.cpp)
target_include_directories(test_command PRIVATE ../src ${ESPRESSIO_OBSERVABLE_INCLUDE})
add_test(NAME ESPressioCommand COMMAND test_command)

add_executable(test_observable test_observable.cpp)
target_include_directories(test_observable PRIVATE ../src ${ESPRESSIO_OBSERVABLE_INCLUDE})
add_test(NAME ESPressioCommandObservable COMMAND test_observable)

add_executable(test_json_command test_json_command.cpp)
target_include_directories(
    test_json_command
    PRIVATE
        ../src
        ${ESPRESSIO_OBSERVABLE_INCLUDE}
        ${ARDUINOJSON_INCLUDE}
)
add_test(NAME ESPressioCommandJson COMMAND test_json_command)

add_executable(test_async_command test_async_command.cpp)
target_include_directories(test_async_command PRIVATE ../src)
add_test(NAME ESPressioCommandAsync COMMAND test_async_command)
