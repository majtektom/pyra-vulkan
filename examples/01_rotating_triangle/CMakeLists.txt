add_shader_bundle(
    rotating_triangle
    triangle.vert.hlsl=triangle.hlsl@vertexMain
    triangle.frag.hlsl=triangle.hlsl@fragmentMain
)

add_executable(01_rotating_triangle_example
    main.cpp
)

target_link_libraries(01_rotating_triangle_example PRIVATE engine3d)
target_compile_definitions(01_rotating_triangle_example PRIVATE
    EXAMPLE_SHADER_DIR="${rotating_triangle_SHADER_DIR}"
)
add_dependencies(01_rotating_triangle_example ${rotating_triangle_SHADER_TARGET})
