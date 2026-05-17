set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin)
set(PROJECT_SHADER_OUTPUT_DIR ${PROJECT_OUTPUT_DIR}/assets/shaders)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_OUTPUT_DIR})

foreach(config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${PROJECT_OUTPUT_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${PROJECT_OUTPUT_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${PROJECT_OUTPUT_DIR})
endforeach()

find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_package(Freetype QUIET)

if(Freetype_FOUND)
    set(ENGINE3D_HAS_FREETYPE 1)
else()
    set(ENGINE3D_HAS_FREETYPE 0)
    message(WARNING "FreeType headers not found. TTF font loading and on-screen text will be unavailable until you install freetype-devel.")
endif()

set(SHADER_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
set(SHADER_BINARY_ROOT ${PROJECT_SHADER_OUTPUT_DIR})

find_program(GLSLC_EXECUTABLE NAMES glslc)
find_program(GLSLANG_VALIDATOR_EXECUTABLE NAMES glslangValidator)
find_program(SLANGC_EXECUTABLE NAMES slangc)
find_program(DXC_EXECUTABLE NAMES dxc)

if(GLSLC_EXECUTABLE)
    set(GLSL_SHADER_COMPILER ${GLSLC_EXECUTABLE})
    set(GLSL_SHADER_COMPILER_MODE glslc)
elseif(GLSLANG_VALIDATOR_EXECUTABLE)
    set(GLSL_SHADER_COMPILER ${GLSLANG_VALIDATOR_EXECUTABLE})
    set(GLSL_SHADER_COMPILER_MODE glslangValidator)
else()
    message(FATAL_ERROR "Neither glslc nor glslangValidator was found in PATH")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/ShaderBundles.cmake)
