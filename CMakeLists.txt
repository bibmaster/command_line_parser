cmake_minimum_required(VERSION 3.8.0)

project(command_line_parser CXX C)

set(CMAKE_CXX_STANDARD 20)

add_executable(command_line_test
  command_line_parser.hpp
  command_line_test.cpp
  )


