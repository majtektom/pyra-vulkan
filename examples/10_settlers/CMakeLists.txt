add_executable(10_settlers_example
    main.cpp
    settlers_defs.cpp
    settlers_assets.cpp
    settlers_world.cpp
)
target_link_libraries(10_settlers_example PRIVATE engine3d)
