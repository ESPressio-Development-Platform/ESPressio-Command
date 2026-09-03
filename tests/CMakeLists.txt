cmake_minimum_required(VERSION 3.16)
project(ESPressioCommandTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
include(FetchContent)

FetchContent_Declare(
    ESPressioSystem
    GIT_REPOSITORY https://github.com/ESPressio-Development-Platform/ESPressio-System.git
    GIT_TAG structural_realignment_propagation_ESPressio-Mesh
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ESPressioSystem)
set(ESPRESSIO_SYSTEM_INCLUDE ${espressiosystem_SOURCE_DIR}/src)

FetchContent_Declare(
    ESPressioPrimitive
    GIT_REPOSITORY https://github.com/ESPressio-Development-Platform/ESPressio-Primitive.git
    GIT_TAG structural_realignment_propagation_ESPressio-Mesh
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ESPressioPrimitive)
set(ESPRESSIO_PRIMITIVE_INCLUDE ${espressioprimitive_SOURCE_DIR}/src)

FetchContent_Declare(
    ESPressioObservable
    GIT_REPOSITORY https://github.com/ESPressio-Development-Platform/ESPressio-Observable.git
    GIT_TAG structural_realignment
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ESPressioObservable)
set(ESPRESSIO_OBSERVABLE_INCLUDE ${espressioobservable_SOURCE_DIR}/src)

FetchContent_Declare(
    ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ArduinoJson)
set(ARDUINOJSON_INCLUDE ${arduinojson_SOURCE_DIR}/src)

set(
    COMMAND_INCLUDES
    ../src
    ${ESPRESSIO_SYSTEM_INCLUDE}
    ${ESPRESSIO_PRIMITIVE_INCLUDE}
    ${ESPRESSIO_OBSERVABLE_INCLUDE}
)

enable_testing()

add_executable(test_command test_command.cpp)
target_include_directories(test_command PRIVATE ${COMMAND_INCLUDES})
add_test(NAME ESPressioCommand COMMAND test_command)

add_executable(test_observable test_observable.cpp)
target_include_directories(test_observable PRIVATE ${COMMAND_INCLUDES})
add_test(NAME ESPressioCommandObservable COMMAND test_observable)

add_executable(test_json_command test_json_command.cpp)
target_include_directories(test_json_command PRIVATE ${COMMAND_INCLUDES} ${ARDUINOJSON_INCLUDE})
add_test(NAME ESPressioCommandJson COMMAND test_json_command)

add_executable(test_async_command test_async_command.cpp)
target_include_directories(test_async_command PRIVATE ${COMMAND_INCLUDES})
add_test(NAME ESPressioCommandAsyncIntent COMMAND test_async_command)
