cmake_minimum_required(VERSION 3.20)

project(engine3d_examples LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug or Release" FORCE)
endif()

include(cmake/ProjectSettings.cmake)

add_custom_target(compile_shaders ALL)

add_subdirectory(engine)
add_subdirectory(examples)
