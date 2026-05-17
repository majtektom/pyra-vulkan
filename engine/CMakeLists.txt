add_library(engine3d STATIC
    src/Engine.cpp
    src/ShaderManager.cpp
    src/TextureManager.cpp
    src/TextureAssetLoader.cpp
    src/VulkanRenderer.cpp
    src/X11Window.cpp
)

target_include_directories(engine3d
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine3d
    PUBLIC
        Vulkan::Vulkan
        X11::X11
)
