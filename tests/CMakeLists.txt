add_executable(scene_hierarchy_tests
    scene_hierarchy_tests.cpp
)

target_link_libraries(scene_hierarchy_tests
    PRIVATE
        engine3d
)

add_test(
    NAME scene_hierarchy_tests
    COMMAND scene_hierarchy_tests
)
