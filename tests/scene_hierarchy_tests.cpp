#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include "core/entity.h"
#include "core/light_component.h"
#include "feature/fog/volumetric_fog_renderer.h"
#include "scene/scene.h"

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

bool testLocalFogVolumesAreBoundedAndSanitized() {
    VolumetricFogRenderer fog;
    std::vector<LocalFogVolume> volumes(VolumetricFogRenderer::kMaxLocalVolumes + 1);
    volumes[0].density = -1.0f;
    volumes[0].halfExtents = {-2.0f, 0.0f, 5.0f};
    volumes[0].edgeFade = 2.0f;

    fog.setLocalVolumes(std::move(volumes));
    const auto& result = fog.localVolumes();

    return expect(result.size() == VolumetricFogRenderer::kMaxLocalVolumes,
                  "Local fog volume count should be limited") &&
           expect(nearlyEqual(result[0].density, 0.0f),
                  "Local fog density should not be negative") &&
           expect(result[0].halfExtents.x > 0.0f && result[0].halfExtents.y > 0.0f,
                  "Local fog extents should be positive") &&
           expect(nearlyEqual(result[0].edgeFade, 1.0f),
                  "Local fog edge fade should be clamped");
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

} // namespace

int main() {
    bool ok = true;

    ok = testBasicParentChildAndCycleProtection() && ok;
    ok = testRemoveSubtreePolicy() && ok;
    ok = testReparentChildrenToParentPolicy() && ok;
    ok = testDetachChildrenPolicy() && ok;
    ok = testLocalFogVolumesAreBoundedAndSanitized() && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] scene_hierarchy_tests" << std::endl;
    return 0;
}
