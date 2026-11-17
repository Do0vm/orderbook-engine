cmake_minimum_required(VERSION 3.16)
project(orderbook_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(OBE_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

add_library(obe src/order_book.cpp)
target_include_directories(obe PUBLIC include)
if(MSVC)
  target_compile_options(obe PUBLIC /W4 /permissive-)
else()
  target_compile_options(obe PUBLIC -Wall -Wextra -Wpedantic)
  if(OBE_SANITIZE)
    target_compile_options(obe PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(obe PUBLIC -fsanitize=address,undefined)
  endif()
endif()

add_executable(obe_tests tests/test_order_book.cpp)
target_link_libraries(obe_tests PRIVATE obe)

add_executable(obe_bench bench/bench.cpp)
target_link_libraries(obe_bench PRIVATE obe)

add_executable(obe_demo examples/demo.cpp)
target_link_libraries(obe_demo PRIVATE obe)

enable_testing()
add_test(NAME order_book COMMAND obe_tests)
