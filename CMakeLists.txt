cmake_minimum_required(VERSION 3.20)

project(FacetIR
  VERSION 0.1.0
  DESCRIPTION "Facet Gen 1 representation and projection layer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(facet
  src/ast.cpp
  src/lexer.cpp
  src/registry.cpp
  src/facet.cpp
  src/sympy.cpp)

target_include_directories(facet
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(facet PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>)

add_executable(facet_cli src/facet_cli.cpp)
target_link_libraries(facet_cli PRIVATE facet)
set_target_properties(facet_cli PROPERTIES OUTPUT_NAME facet)

add_executable(test_facet tests/test_facet.cpp)
target_link_libraries(test_facet PRIVATE facet)

enable_testing()
add_test(NAME facet_gen1 COMMAND test_facet)
set_tests_properties(facet_gen1 PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
