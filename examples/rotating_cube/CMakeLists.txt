add_shader_bundle(
    rotating_cube
    cube.vert.hlsl=cube.hlsl@vertexMain
    cube.frag.hlsl=cube.hlsl@fragmentMain
)

add_executable(rotating_cube_example
    main.cpp
)

target_link_libraries(rotating_cube_example PRIVATE engine3d)
target_compile_definitions(rotating_cube_example PRIVATE
    EXAMPLE_SHADER_DIR="${rotating_cube_SHADER_DIR}"
)
add_dependencies(rotating_cube_example ${rotating_cube_SHADER_TARGET})
