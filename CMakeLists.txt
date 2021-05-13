cmake_minimum_required(VERSION 2.8)

include (CheckFunctionExists)
include (CheckIncludeFiles)
include (CheckSymbolExists)

include (CPM.cmake)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
  ADD_DEFINITIONS(-DDEBUG=1)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

project (libeventco)

CPMAddPackage(
    GITHUB_REPOSITORY msaf1980/libcoro
    VERSION 1.61.1
    OPTIONS "CORO_TEST OFF"
)
#set_target_properties(coro PROPERTIES COMPILE_DEFINITIONS "CORO_ASM=1")


check_include_files (ucontext.h HAVE_UCONTEXT_H)
check_include_files (setjmp.h HAVE_SETJMP_H)
check_symbol_exists (sigaltstack "signal.h" HAVE_SIGALTSTACK)

if (HAVE_UCONTEXT_H)
    add_definitions(-DHAVE_UCONTEXT_H)
endif (HAVE_UCONTEXT_H)
if (HAVE_SETJMP_H)
    add_definitions(-DHAVE_SETJMP_H)
endif (HAVE_SETJMP_H)
if (HAVE_SIGALTSTACK)
    add_definitions(-DHAVE_SIGALTSTACK)
endif (HAVE_SIGALTSTACK)

add_library(eventco src/eventco.c src/hashtab.c "${libcoro_SOURCE_DIR}/coro.c")
target_link_libraries (eventco event)

include_directories( include )

check_include_files (ucontext.h HAVE_UCONTEXT_H)
check_include_files (setjmp.h HAVE_SETJMP_H)
check_symbol_exists (sigaltstack "signal.h" HAVE_SIGALTSTACK)

if (HAVE_UCONTEXT_H)
    add_definitions(-DHAVE_UCONTEXT_H)
endif (HAVE_UCONTEXT_H)
if (HAVE_SETJMP_H)
    add_definitions(-DHAVE_SETJMP_H)
endif (HAVE_SETJMP_H)
if (HAVE_SIGALTSTACK)
    add_definitions(-DHAVE_SIGALTSTACK)
endif (HAVE_SIGALTSTACK)

add_definitions(-DCORO_ASM=1)

include_directories ("${PROJECT_BINARY_DIR}")
include_directories ("${libcoro_SOURCE_DIR}")

enable_testing()

add_executable(test_echo tests/test_echo.c)
target_link_libraries (test_echo eventco)

add_executable(test_proxy tests/test_proxy.c)
target_link_libraries (test_proxy eventco)

add_executable(test_cond tests/test_cond.c)
target_link_libraries (test_cond eventco)
add_test(NAME test_cond COMMAND ${CMAKE_BINARY_DIR}/test_cond)

add_executable(bench_yield bench/bench_yield.c bench/benchutils.c)
target_link_libraries (bench_yield eventco)
add_test(NAME bench_yield COMMAND ${CMAKE_BINARY_DIR}/bench_yield)
