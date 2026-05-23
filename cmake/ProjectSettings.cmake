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
find_package(PkgConfig REQUIRED)

# SDL3
find_package(sdl3 CONFIG QUIET)
if(NOT sdl3_FOUND)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
endif()
if(NOT SDL3_FOUND AND NOT TARGET PkgConfig::SDL3)
    message(FATAL_ERROR "SDL3 not found. Install libsdl3-dev or sdl3 package.")
endif()

# SDL3_ttf
pkg_check_modules(SDL3_TTF IMPORTED_TARGET sdl3-ttf)
if(NOT SDL3_TTF_FOUND)
    message(WARNING "SDL3_ttf not found. Font rendering will be unavailable.")
    set(ENGINE3D_HAS_TTF 0)
else()
    set(ENGINE3D_HAS_TTF 1)
endif()

# SDL3_image (optional, for future use)
pkg_check_modules(SDL3_IMAGE IMPORTED_TARGET sdl3-image QUIET)

# SDL3_sound (optional, for future use)
pkg_check_modules(SDL3_SOUND IMPORTED_TARGET sdl3-sound QUIET)

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
