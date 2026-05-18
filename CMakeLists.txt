cmake_minimum_required(VERSION 3.20)

project(engine3d_examples LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/ProjectSettings.cmake)

add_custom_target(compile_shaders ALL)

add_subdirectory(engine)
add_subdirectory(examples)
