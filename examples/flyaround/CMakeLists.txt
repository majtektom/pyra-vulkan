add_executable(flyaround_example main.cpp)
target_link_libraries(flyaround_example PRIVATE engine3d)
target_compile_definitions(flyaround_example PRIVATE
    EXAMPLE_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/rotating_cube"
)
add_dependencies(flyaround_example rotating_cube_shaders)
