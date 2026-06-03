#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

#include "core/entity.h"
#include "core/light_component.h"
#include "scene/scene.h"
#include "scene/prefab_serializer.h"

using namespace engine3d;

namespace {

bool fail(const std::string& message) {
    std::cerr << "[FAIL] " << message << std::endl;
    return false;
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        return fail(message);
    }
    return true;
}

bool containsEntity(const std::vector<Entity*>& entities, const Entity* needle) {
    return std::find(entities.begin(), entities.end(), needle) != entities.end();
}

bool nearlyEqual(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

std::filesystem::path makeTempPrefabPath() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("engine3d_prefab_hierarchy_test_" + std::to_string(now) + ".json");
}

bool testBasicParentChildAndCycleProtection() {
    Scene scene("hierarchy_basic");

    Entity* grandParent = scene.createEntity("grand_parent");
    Entity* parent = scene.createEntity("parent");
    Entity* child = scene.createEntity("child");

    if (!expect(scene.setParent(parent, grandParent), "setParent(parent, grandParent) should succeed")) {
        return false;
    }
    if (!expect(scene.setParent(child, parent), "setParent(child, parent) should succeed")) {
        return false;
    }

    if (!expect(scene.parentOf(child) == parent, "child should have parent")) {
        return false;
    }

    const std::vector<Entity*> parentChildren = scene.childrenOf(parent);
    if (!expect(containsEntity(parentChildren, child), "parent should contain child")) {
        return false;
    }

    // Re-parenting grand parent under child would create a cycle and must be rejected.
    if (!expect(!scene.setParent(grandParent, child), "cycle creation should be rejected")) {
        return false;
    }

    if (!expect(scene.parentOf(grandParent) == nullptr, "grand parent should remain root after rejected cycle")) {
        return false;
    }

    return true;
}

bool testRemoveSubtreePolicy() {
    Scene scene("hierarchy_remove_subtree");

    Entity* root = scene.createEntity("root");
    Entity* parent = scene.createEntity("parent");
    Entity* child = scene.createEntity("child");

    const EntityId rootId = root->id();
    const EntityId parentId = parent->id();
    const EntityId childId = child->id();

    if (!expect(scene.setParent(parent, root), "setParent(parent, root) should succeed")) {
        return false;
    }
    if (!expect(scene.setParent(child, parent), "setParent(child, parent) should succeed")) {
        return false;
    }

    if (!expect(scene.removeEntity(parentId, HierarchyRemovalPolicy::RemoveSubtree), "removeEntity RemoveSubtree should succeed")) {
        return false;
    }

    if (!expect(scene.getEntity(parentId) == nullptr, "parent should be removed")) {
        return false;
    }
    if (!expect(scene.getEntity(childId) == nullptr, "child should be removed with subtree")) {
        return false;
    }
    if (!expect(scene.getEntity(rootId) != nullptr, "root should stay alive")) {
        return false;
    }

    return true;
}

bool testReparentChildrenToParentPolicy() {
    Scene scene("hierarchy_reparent_children");

    Entity* grandParent = scene.createEntity("grand_parent");
    Entity* parent = scene.createEntity("parent");
    Entity* child = scene.createEntity("child");

    const EntityId grandParentId = grandParent->id();
    const EntityId parentId = parent->id();
    const EntityId childId = child->id();

    if (!expect(scene.setParent(parent, grandParent), "setParent(parent, grandParent) should succeed")) {
        return false;
    }
    if (!expect(scene.setParent(child, parent), "setParent(child, parent) should succeed")) {
        return false;
    }

    if (!expect(scene.removeEntity(parentId, HierarchyRemovalPolicy::ReparentChildrenToParent), "removeEntity ReparentChildrenToParent should succeed")) {
        return false;
    }

    Entity* grandParentAfter = scene.getEntity(grandParentId);
    Entity* childAfter = scene.getEntity(childId);

    if (!expect(grandParentAfter != nullptr, "grand parent should stay alive")) {
        return false;
    }
    if (!expect(childAfter != nullptr, "child should stay alive")) {
        return false;
    }
    if (!expect(scene.getEntity(parentId) == nullptr, "parent should be removed")) {
        return false;
    }
    if (!expect(scene.parentOf(childAfter) == grandParentAfter, "child should be reparented to grand parent")) {
        return false;
    }

    const std::vector<Entity*> grandChildren = scene.childrenOf(grandParentAfter);
    if (!expect(containsEntity(grandChildren, childAfter), "grand parent should list child after reparent")) {
        return false;
    }

    return true;
}

bool testDetachChildrenPolicy() {
    Scene scene("hierarchy_detach_children");

    Entity* parent = scene.createEntity("parent");
    Entity* child = scene.createEntity("child");

    const EntityId parentId = parent->id();
    const EntityId childId = child->id();

    if (!expect(scene.setParent(child, parent), "setParent(child, parent) should succeed")) {
        return false;
    }

    if (!expect(scene.removeEntity(parentId, HierarchyRemovalPolicy::DetachChildren), "removeEntity DetachChildren should succeed")) {
        return false;
    }

    Entity* childAfter = scene.getEntity(childId);
    if (!expect(childAfter != nullptr, "child should stay alive after detach")) {
        return false;
    }
    if (!expect(scene.parentOf(childAfter) == nullptr, "child should be detached from parent")) {
        return false;
    }

    return true;
}

bool testPrefabRoundtripPreservesHierarchyAndData() {
    Scene source("prefab_source");

    Entity* root = source.createEntity("root_source");
    Entity* child = source.createEntity("child_source");
    Entity* grandChild = source.createEntity("grand_child_source");

    root->transform().position = Vec3{1.0f, 2.0f, 3.0f};
    child->transform().position = Vec3{4.0f, 5.0f, 6.0f};
    grandChild->transform().position = Vec3{7.0f, 8.0f, 9.0f};

    auto* childLight = child->addComponent<LightComponent>();
    childLight->type = LightType::Spot;
    childLight->intensity = 2.5f;
    childLight->range = 42.0f;

    if (!expect(source.setParent(child, root), "setParent(child, root) should succeed in source prefab")) {
        return false;
    }
    if (!expect(source.setParent(grandChild, child), "setParent(grandChild, child) should succeed in source prefab")) {
        return false;
    }

    const std::filesystem::path prefabPath = makeTempPrefabPath();

    if (!expect(PrefabSerializer::saveEntityToFile(*root, prefabPath.string()), "PrefabSerializer::saveEntityToFile should succeed")) {
        return false;
    }

    Scene destination("prefab_destination");
    Entity* instantiatedRoot =
        PrefabSerializer::instantiateFromFile(destination, prefabPath.string(), "root_instance");

    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    if (!expect(instantiatedRoot != nullptr, "PrefabSerializer::instantiateFromFile should return root entity")) {
        return false;
    }
    if (!expect(instantiatedRoot->getName() == "root_instance", "Root instance should use name override")) {
        return false;
    }

    const std::vector<Entity*> rootChildren = destination.childrenOf(instantiatedRoot);
    if (!expect(rootChildren.size() == 1, "Instantiated root should have exactly one direct child")) {
        return false;
    }

    Entity* instantiatedChild = rootChildren[0];
    if (!expect(instantiatedChild->getName() == "child_source", "Instantiated child should keep source name")) {
        return false;
    }
    if (!expect(destination.parentOf(instantiatedChild) == instantiatedRoot, "Instantiated child parent should be root instance")) {
        return false;
    }

    const std::vector<Entity*> childChildren = destination.childrenOf(instantiatedChild);
    if (!expect(childChildren.size() == 1, "Instantiated child should have exactly one grandchild")) {
        return false;
    }

    Entity* instantiatedGrandChild = childChildren[0];
    if (!expect(instantiatedGrandChild->getName() == "grand_child_source", "Grandchild should keep source name")) {
        return false;
    }
    if (!expect(destination.parentOf(instantiatedGrandChild) == instantiatedChild, "Grandchild parent should be instantiated child")) {
        return false;
    }

    if (!expect(nearlyEqual(instantiatedChild->transform().position.x, 4.0f), "Child transform.x should be preserved")) {
        return false;
    }
    if (!expect(nearlyEqual(instantiatedChild->transform().position.y, 5.0f), "Child transform.y should be preserved")) {
        return false;
    }
    if (!expect(nearlyEqual(instantiatedChild->transform().position.z, 6.0f), "Child transform.z should be preserved")) {
        return false;
    }

    auto* instantiatedLight = instantiatedChild->getComponent<LightComponent>();
    if (!expect(instantiatedLight != nullptr, "Child light component should be restored")) {
        return false;
    }
    if (!expect(instantiatedLight->type == LightType::Spot, "Child light type should be preserved")) {
        return false;
    }
    if (!expect(nearlyEqual(instantiatedLight->intensity, 2.5f), "Child light intensity should be preserved")) {
        return false;
    }
    if (!expect(nearlyEqual(instantiatedLight->range, 42.0f), "Child light range should be preserved")) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = testBasicParentChildAndCycleProtection() && ok;
    ok = testRemoveSubtreePolicy() && ok;
    ok = testReparentChildrenToParentPolicy() && ok;
    ok = testDetachChildrenPolicy() && ok;
    ok = testPrefabRoundtripPreservesHierarchyAndData() && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] scene_hierarchy_tests" << std::endl;
    return 0;
}
