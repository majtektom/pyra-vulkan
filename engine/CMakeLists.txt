add_library(engine3d SHARED
    core/logger.cpp
    core/engine.cpp
    core/asset_registry.cpp
    core/component.cpp
    core/entity.cpp
    # Komponenty ECS (dane)
    core/mesh_component.cpp
    core/mesh_manager.cpp
    camera/camera_component.cpp
    camera/camera_controller.cpp
    platform/platform.cpp
    render/renderer_core.cpp
    render/vulkan_util.cpp
    render/texture_asset_loader.cpp
    render/texture_manager.cpp
    render/shader_manager.cpp
    render/material_manager.cpp
    render/material_compiler.cpp
    render/render_orchestrator.cpp
    render/opaque_pass.cpp
    render/transparent_pass.cpp
    render/material_system.cpp
    render/base_material_instance.cpp
    render/shader_hot_reload.cpp
    render/backend/vulkan_context.cpp
    render/backend/vulkan_resources.cpp
    render/backend/vulkan_swapchain.cpp
    object/model_3ds.cpp
    object/model_md2.cpp
    object/gltf/model_gltf.cpp
    object/grid_renderer.cpp
    object/skybox_renderer.cpp
    object/skydome_renderer.cpp
    object/ocean_renderer.cpp
    object/terrain/terrain.cpp
    object/terrain/terrain_index_generator.cpp
    object/terrain/terrain_heightmap_generator.cpp
    object/terrain/terrain_placement_system.cpp
    object/terrain/terrain_system.cpp
    object/terrain/terrain_component.cpp
    feature/particle/particle_event.cpp
    feature/particle/emitter.cpp
    feature/particle/system.cpp
    feature/sprite/sprite.cpp
    feature/font/font_manager.cpp
    feature/font/font_atlas_loader.cpp
    feature/font/text_overlay_renderer.cpp
    feature/gui/gui.cpp
    feature/gui/gui_button.cpp
    feature/gui/gui_input_text.cpp
    feature/gui/gui_list_box.cpp
    feature/gui/gui_static_text.cpp
    feature/gui/gui_check_box.cpp
    feature/gui/gui_combo_box.cpp
    feature/gui/gui_memo.cpp
    feature/gui/gui_track_bar.cpp
    feature/gui/gui_scroll_bar.cpp
    feature/gui/gui_renderer.cpp
    scene/scene.cpp
    scene/camera_extraction_system.cpp
    scene/layer_manager.cpp
    scene/light_system.cpp
    scene/render_frame_prep_system.cpp
    scene/render_extraction_system.cpp
    scene/prefab_serializer.cpp
    scene/scene_serializer.cpp
    scene/transform_system.cpp
    scene/scene_manager.cpp
)

add_shader_bundle(
    default
    default.vert.hlsl=default.hlsl@vertexMain
    default.frag.hlsl=default.hlsl@fragmentMain
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
# "skinned" shader removed — skinning goes through gbuffer_skinned in deferred pipeline

add_shader_bundle(
    terrain
    terrain.vert.hlsl=terrain.hlsl@vertexMain
    terrain.frag.hlsl=terrain.hlsl@fragmentMain
)

add_shader_bundle(
    particle
    particle.vert.hlsl=particle.hlsl@vertexMain
    particle.frag.hlsl=particle.hlsl@fragmentMain
)

add_shader_bundle(
    sprite
    sprite.vert.hlsl=sprite.hlsl@vertexMain
    sprite.frag.hlsl=sprite.hlsl@fragmentMain
)

add_shader_bundle(
    skydome
    skydome.vert.hlsl=skydome.hlsl@vertexMain
    skydome.frag.hlsl=skydome.hlsl@fragmentMain
)

add_shader_bundle(
    ocean
    ocean.vert.hlsl=ocean.hlsl@vertexMain
    ocean.frag.hlsl=ocean.hlsl@fragmentMain
)

add_shader_bundle(
    shadow_depth
    shadow_depth.vert.hlsl=shadow_depth.hlsl@vertexMain
    shadow_depth.frag.hlsl=shadow_depth.hlsl@fragmentMain
)

add_shader_bundle(
    shadow_depth_skinned
    shadow_depth_skinned.vert.hlsl=shadow_depth_skinned.hlsl@vertexMain
    shadow_depth_skinned.frag.hlsl=shadow_depth_skinned.hlsl@fragmentMain
)

add_shader_bundle(
    gbuffer
    gbuffer.vert.hlsl=gbuffer.hlsl@vertexMain
    gbuffer.frag.hlsl=gbuffer.hlsl@fragmentMain
)

add_shader_bundle(
    gbuffer_skinned
    gbuffer_skinned.vert.hlsl=gbuffer/gbuffer_skinned.hlsl@vertexMain
    gbuffer_skinned.frag.hlsl=gbuffer/gbuffer_skinned.hlsl@fragmentMain
)

add_shader_bundle(
    deferred
    deferred.vert.hlsl=deferred.hlsl@vertexMain
    deferred.frag.hlsl=deferred.hlsl@fragmentMain
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

find_package(Threads REQUIRED)

target_link_libraries(engine3d
    PUBLIC
        Vulkan::Vulkan
        Threads::Threads
)


add_dependencies(engine3d text_overlay_shaders grid_shaders skybox_shaders default_shaders terrain_shaders particle_shaders sprite_shaders skydome_shaders ocean_shaders shadow_depth_shaders shadow_depth_skinned_shaders gbuffer_shaders deferred_shaders gbuffer_skinned_shaders)
