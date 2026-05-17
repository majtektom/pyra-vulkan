add_library(engine3d STATIC
    src/Engine.cpp
    src/FontAtlasLoader.cpp
    src/FontManager.cpp
    src/ShaderManager.cpp
    src/TextureManager.cpp
    src/TextureAssetLoader.cpp
    src/VulkanRenderer.cpp
    src/X11Window.cpp
)

add_shader_bundle(text_overlay text.vert text.frag)

target_compile_definitions(engine3d
    PUBLIC
        ENGINE3D_HAS_FREETYPE=${ENGINE3D_HAS_FREETYPE}
    PRIVATE
        ENGINE_TEXT_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/text_overlay"
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

add_dependencies(engine3d text_overlay_shaders)
