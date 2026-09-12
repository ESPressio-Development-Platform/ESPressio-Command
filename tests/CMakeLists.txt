cmake_minimum_required(VERSION 3.16)
project(ESPressioCommandContracts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ESPRESSIO_DEPENDENCIES "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "Dependency working-branch checkouts")
find_package(Threads REQUIRED)
enable_testing()
set(COMMAND_INCLUDES ../src)
foreach(dependency System Primitive Task Threads Timing Serializable Units Observable Persistence)
    list(APPEND COMMAND_INCLUDES "${ESPRESSIO_DEPENDENCIES}/ESPressio-${dependency}/src")
endforeach()
list(APPEND COMMAND_INCLUDES "${ESPRESSIO_DEPENDENCIES}/ESPressio-Timing/tests/stubs" "${ESPRESSIO_DEPENDENCIES}/ESPressio-Task/tests")

foreach(name core_contract wire_v1 response_capability runtime persistence persistent_results remote_replay outbound recovery critical_execution no_heap eradication fault_injection)
    add_executable(command_${name} test_command_${name}.cpp)
    target_include_directories(command_${name} PRIVATE ${COMMAND_INCLUDES})
    target_compile_options(command_${name} PRIVATE -Wall -Wextra -Werror -fno-rtti -UNDEBUG)
    target_link_libraries(command_${name} PRIVATE Threads::Threads)
    add_test(NAME command_${name} COMMAND command_${name})
endforeach()

set(COMMAND_NEGATIVE_INCLUDE_ARGS "-I${CMAKE_CURRENT_LIST_DIR}/../src")
foreach(dependency System Primitive Task Threads Timing Serializable Units Observable Persistence)
    list(APPEND COMMAND_NEGATIVE_INCLUDE_ARGS "-I${ESPRESSIO_DEPENDENCIES}/ESPressio-${dependency}/src")
endforeach()
list(APPEND COMMAND_NEGATIVE_INCLUDE_ARGS
    "-I${ESPRESSIO_DEPENDENCIES}/ESPressio-Timing/tests/stubs"
    "-I${ESPRESSIO_DEPENDENCIES}/ESPressio-Task/tests")

function(add_command_compile_failure name)
    add_test(
        NAME command_compile_fail_${name}
        COMMAND ${CMAKE_CXX_COMPILER}
            -std=c++17 -fno-rtti -Wall -Wextra -Werror -fsyntax-only
            ${COMMAND_NEGATIVE_INCLUDE_ARGS}
            "${CMAKE_CURRENT_LIST_DIR}/negative/${name}.cpp")
    set_tests_properties(command_compile_fail_${name} PROPERTIES WILL_FAIL TRUE)
endfunction()

add_command_compile_failure(zero_type_id)
add_command_compile_failure(invalid_capacity)
add_command_compile_failure(missing_response_capacity)
add_command_compile_failure(invalid_execution_policy)
add_command_compile_failure(removed_registry_api)
add_command_compile_failure(removed_correlation_id)
