add_library(engine3d STATIC
    core/engine.cpp
    core/component.cpp
    core/entity.cpp
    core/transform_component.cpp
    core/mesh_component.cpp
    camera/camera_component.cpp
    camera/camera_controller.cpp
    platform/platform.cpp
    render/renderer_core.cpp
    render/vulkan_util.cpp
    render/texture_asset_loader.cpp
    render/texture_manager.cpp
    render/shader_manager.cpp
    render/material_manager.cpp
    render/opaque_pass.cpp
    render/transparent_pass.cpp
    render/material_system.cpp
    render/base_material_instance.cpp
    render/backend/vulkan_context.cpp
    render/backend/vulkan_resources.cpp
    render/backend/vulkan_swapchain.cpp
    object/model_3ds.cpp
    object/media_library.cpp
    object/mesh_manager.cpp
    font/font_manager.cpp
    font/font_atlas_loader.cpp
    object/grid_renderer.cpp
    object/skybox_renderer.cpp
    particle/particle_system.cpp
    font/text_overlay_renderer.cpp
    gui/gui.cpp
    gui/gui_button.cpp
    gui/gui_input_text.cpp
    gui/gui_list_box.cpp
    gui/gui_static_text.cpp
    gui/gui_check_box.cpp
    gui/gui_combo_box.cpp
    gui/gui_memo.cpp
    gui/gui_track_bar.cpp
    gui/gui_scroll_bar.cpp
    gui/gui_renderer.cpp
    scene/scene_manager.cpp
)

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
add_shader_bundle(
    default
    default.vert.hlsl=default.hlsl@vertexMain
    default.frag.hlsl=default.hlsl@fragmentMain
)

target_compile_definitions(engine3d
    PUBLIC
        ENGINE3D_HAS_TTF=${ENGINE3D_HAS_TTF}
    PRIVATE
        ENGINE_TEXT_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/text_overlay"
        ENGINE_GRID_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/grid"
        ENGINE_SKYBOX_SHADER_DIR="${PROJECT_SHADER_OUTPUT_DIR}/skybox"
)

target_include_directories(engine3d
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/render
        ${CMAKE_CURRENT_SOURCE_DIR}/camera
        ${CMAKE_CURRENT_SOURCE_DIR}/scene
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/object
        ${CMAKE_CURRENT_SOURCE_DIR}/font
        ${CMAKE_CURRENT_SOURCE_DIR}/particle
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
)

if(TARGET PkgConfig::SDL3)
    target_link_libraries(engine3d PUBLIC PkgConfig::SDL3)
else()
    target_link_libraries(engine3d PUBLIC sdl3::sdl3)
endif()

if(TARGET PkgConfig::SDL3_TTF AND SDL3_TTF_FOUND)
    target_link_libraries(engine3d PRIVATE PkgConfig::SDL3_TTF)
endif()

if(TARGET PkgConfig::SDL3_IMAGE AND SDL3_IMAGE_FOUND)
    target_link_libraries(engine3d PRIVATE PkgConfig::SDL3_IMAGE)
endif()

if(TARGET PkgConfig::SDL3_SOUND AND SDL3_SOUND_FOUND)
    target_link_libraries(engine3d PRIVATE PkgConfig::SDL3_SOUND)
endif()

target_link_libraries(engine3d
    PUBLIC
        Vulkan::Vulkan
)

add_dependencies(engine3d text_overlay_shaders grid_shaders skybox_shaders)
