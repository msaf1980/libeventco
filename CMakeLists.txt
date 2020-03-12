cmake_minimum_required(VERSION 2.8)

include (CheckFunctionExists)
include (CheckIncludeFiles)
include (CheckSymbolExists)

project (libevco)
add_library(evco src/evco.c src/hashtab.c)
target_link_libraries (evco event)
#set_target_properties(coro PROPERTIES COMPILE_DEFINITIONS "CORO_ASM=1")

include_directories( include )

check_include_files (ucontext.h HAVE_UCONTEXT_H)
check_include_files (setjmp.h HAVE_SETJMP_H)
check_symbol_exists (sigaltstack "signal.h" HAVE_SIGALTSTACK)

configure_file(config.h.in config.h)
include_directories("${PROJECT_BINARY_DIR}")

enable_testing()

add_executable(test_echo tests/test_echo.c)
target_link_libraries (test_echo evco)

add_executable(test_proxy tests/test_proxy.c)
target_link_libraries (test_proxy evco)

add_executable(test_cond tests/test_cond.c)
target_link_libraries (test_cond evco)
add_test(NAME test_cond COMMAND ${CMAKE_BINARY_DIR}/test_cond)

add_executable(bench_yield bench/bench_yield.c bench/benchutils.c)
target_link_libraries (bench_yield evco)
add_test(NAME bench_yield COMMAND ${CMAKE_BINARY_DIR}/bench_yield)
