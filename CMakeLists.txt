cmake_minimum_required(VERSION 3.10)
project(vm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    main.cpp
    vm/chunk.cpp
    vm/debug.cpp
    vm/vm.cpp
    frontend/ast.cpp
    frontend/lexer.cpp
    frontend/parser.cpp
    frontend/compiler.cpp
)

add_executable(vm ${SOURCES})

target_include_directories(vm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vm
    ${CMAKE_CURRENT_SOURCE_DIR}/frontend
)

target_compile_options(vm PRIVATE
    $<$<CONFIG:Debug>:-Wall -Wextra -g -O0>
    $<$<CONFIG:Release>:-Wall -Wextra -O2 -DNDEBUG>
)

option(VM_DEBUG_TRACE "Enable bytecode trace output" OFF)
if(VM_DEBUG_TRACE)
    target_compile_definitions(vm PRIVATE DEBUG_TRACE)
endif()

enable_testing()
add_test(NAME smoke COMMAND vm)
