cmake_minimum_required(VERSION 2.8)

include (CheckFunctionExists)
include (CheckIncludeFiles)
include (CheckSymbolExists)

project (libeventco)

function(append_flag FLAGS_VAR FLAG_VAR CHECK_VAR)
    string(FIND FLAGS_VAR "${CHECK_VAR}" res)
    if(res EQUAL -1)
        set(${FLAGS_VAR} "${${FLAGS_VAR}} ${FLAG_VAR}" PARENT_SCOPE)
    endif()
endfunction()
    
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra")

string(TOLOWER "${CMAKE_BUILD_TYPE}" MODE)
if (MODE STREQUAL "debug")
    string(TOUPPER "${CMAKE_PROJECT_NAME}" PNAME)
    append_flag(CMAKE_C_FLAGS "-g" "-g")
    append_flag(CMAKE_C_FLAGS "-O0" "-O")
    add_definitions("-DDEBUG")
endif ()

if (UNIX)
    include_directories("/usr/local/include")
endif ()

add_library(eventco src/eventco.c src/hashtab.c)
target_link_libraries (eventco event)

include_directories( include )

check_include_files (ucontext.h HAVE_UCONTEXT_H)
check_include_files (setjmp.h HAVE_SETJMP_H)
check_symbol_exists (sigaltstack "signal.h" HAVE_SIGALTSTACK)

configure_file(config.h.in config.h)
include_directories("${PROJECT_BINARY_DIR}")

enable_testing()

add_executable(test_echo tests/test_echo.c)
target_link_libraries (test_echo eventco)

add_executable(test_proxy tests/test_proxy.c)
target_link_libraries (test_proxy eventco)

add_executable(test_cond tests/test_cond.c)
target_link_libraries (test_cond eventco)
add_test(NAME test_cond COMMAND ${CMAKE_BINARY_DIR}/test_cond)

