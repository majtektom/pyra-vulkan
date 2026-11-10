add_executable(10_settlers_example
    main.cpp
    # Definicje i zasoby
    settlers_defs.cpp
    settlers_assets.cpp
    # Symulacja świata (klasa World)
    world_create.cpp
    world_entities.cpp
    world_queries.cpp
    world_movement.cpp
    world_update.cpp
    world_settlers.cpp
    world_combat.cpp
    world_ai.cpp
    world_roads.cpp
    world_save.cpp
    world_fog.cpp
    # Aplikacja (klasa SettlersApp)
    app_core.cpp
    app_menus.cpp
    app_input.cpp
    app_hud.cpp
    app_buildbar.cpp
    app_audio.cpp
    app_debug.cpp
    app_tests.cpp
    app_save.cpp
)
target_link_libraries(10_settlers_example PRIVATE engine3d)
