add_shader_bundle(model_viewer
    model_viewer.vert.hlsl=cube.hlsl@vertexMain
    model_viewer.frag.hlsl=cube.hlsl@fragmentMain
)

add_executable(model_viewer_example
    main.cpp
)

target_link_libraries(model_viewer_example PRIVATE engine3d)
target_compile_definitions(model_viewer_example PRIVATE
    EXAMPLE_SHADER_DIR="${model_viewer_SHADER_DIR}"
)
add_dependencies(model_viewer_example ${model_viewer_SHADER_TARGET})
