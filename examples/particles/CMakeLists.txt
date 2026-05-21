add_executable(particles_example main.cpp)
target_link_libraries(particles_example PRIVATE engine3d)
target_compile_definitions(particles_example PRIVATE
    EXAMPLE_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/rotating_cube"
)
add_dependencies(particles_example rotating_cube_shaders)
