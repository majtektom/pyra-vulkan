set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Katalog z binarkami i skompilowanymi shaderami (domyślnie <repo>/bin; skrypt wydania buduje gdzie indziej,
# żeby nie nadpisać binarek używanych na co dzień).
set(ENGINE3D_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin CACHE PATH "Katalog wyjściowy binarek i shaderów")
set(PROJECT_OUTPUT_DIR ${ENGINE3D_OUTPUT_DIR})
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

# SDL3_ttf - rasteryzacja czcionek (wymagana; Fedora: SDL3_ttf-devel, Ubuntu: libsdl3-ttf-dev)
pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)
set(ENGINE3D_HAS_TTF 1)

# SDL3_image - wczytywanie obrazów (mapy wysokości, tekstury glTF; Fedora: SDL3_image-devel, Ubuntu: libsdl3-image-dev)
pkg_check_modules(SDL3_IMAGE REQUIRED IMPORTED_TARGET sdl3-image)

# Binarki szukają libengine3d.so we własnym katalogu ($ORIGIN) - katalog bin/ można przenieść w dowolne miejsce.
# ENGINE3D_PORTABLE=ON (paczka dla innych, tools/build_release.sh): libstdc++ i libgcc wkompilowane statycznie,
# więc u odbiorcy nie jest potrzebna ta sama wersja kompilatora C++. Biblioteka C (glibc) zostaje dynamiczna -
# o zgodności decyduje system, na którym budujemy (dlatego wydanie budujemy w starszym kontenerze Ubuntu).
option(ENGINE3D_PORTABLE "Statyczne libstdc++/libgcc (paczka do przekazania)" OFF)
if(ENGINE3D_PORTABLE)
    add_link_options(-static-libstdc++ -static-libgcc)
endif()
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

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
