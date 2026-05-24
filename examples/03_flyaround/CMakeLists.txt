add_executable(03_flyaround_example main.cpp)
target_link_libraries(03_flyaround_example PRIVATE engine3d)
target_compile_definitions(03_flyaround_example PRIVATE
    EXAMPLE_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/rotating_cube"
)
add_dependencies(03_flyaround_example rotating_cube_shaders)
