add_library(engine3d STATIC
    src/CameraController.cpp
    src/Engine.cpp
    src/FontAtlasLoader.cpp
    src/FontManager.cpp
    src/GridRenderer.cpp
    src/InputManager.cpp
    src/MaterialManager.cpp
    src/SceneManager.cpp
    src/ShaderManager.cpp
    src/SkyboxRenderer.cpp
    src/TextureManager.cpp
    src/TextureAssetLoader.cpp
    src/VulkanRenderer.cpp
    src/VulkanUtil.cpp
    src/X11Window.cpp
)

# Zalaczamy modul core (loadery modeli itp.)
add_subdirectory(core)

add_shader_bundle(
    text_overlay
    text.vert.hlsl=text.hlsl@vertexMain
    text.frag.hlsl=text.hlsl@fragmentMain
)
add_shader_bundle(
    grid
    grid.vert.hlsl=grid.hlsl@vertexMain
    grid.frag.hlsl=grid.hlsl@fragmentMain
)
add_shader_bundle(
    skybox
    skybox.vert.hlsl=skybox.hlsl@vertexMain
    skybox.frag.hlsl=skybox.hlsl@fragmentMain
)

target_compile_definitions(engine3d
    PUBLIC
        ENGINE3D_HAS_FREETYPE=${ENGINE3D_HAS_FREETYPE}
    PRIVATE
        ENGINE_TEXT_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/text_overlay"
        ENGINE_GRID_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/grid"
        ENGINE_SKYBOX_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/skybox"
)

target_include_directories(engine3d
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(Freetype_FOUND)
    target_link_libraries(engine3d PRIVATE Freetype::Freetype)
endif()

target_link_libraries(engine3d
    PUBLIC
        Vulkan::Vulkan
        X11::X11
)

add_dependencies(engine3d text_overlay_shaders grid_shaders skybox_shaders)
