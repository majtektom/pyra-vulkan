cmake_minimum_required(VERSION 3.20)

project(engine3d_examples LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug or Release" FORCE)
endif()

include(cmake/ProjectSettings.cmake)

enable_testing()

add_custom_target(compile_shaders ALL)

add_subdirectory(engine)
add_subdirectory(examples)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
