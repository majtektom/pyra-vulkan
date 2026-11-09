#include "feature/ai/nav_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace engine3d {

void NavGrid::init(int width, int height, float cellSize, const Vec3& origin) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    cellSize_ = cellSize > 0.0f ? cellSize : 1.0f;
    origin_ = origin;
    const size_t n = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    staticBlocked_.assign(n, 0);
    obstacleCount_.assign(n, 0);
    gScore_.assign(n, 0.0f);
    parent_.assign(n, -1);
    visitStamp_.assign(n, 0);
    closedStamp_.assign(n, 0);
    stamp_ = 0;
}

bool NavGrid::isWalkable(int cx, int cz) const {
    if (!inBounds(cx, cz)) return false;
    const int i = index(cx, cz);
    return staticBlocked_[i] == 0 && obstacleCount_[i] == 0;
}

bool NavGrid::isStaticBlocked(int cx, int cz) const {
    if (!inBounds(cx, cz)) return true;
    return staticBlocked_[index(cx, cz)] != 0;
}

bool NavGrid::isWalkableWorld(float x, float z) const {
    int cx, cz;
    if (!worldToCell(x, z, cx, cz)) return false;
    return isWalkable(cx, cz);
}

void NavGrid::setStaticBlocked(int cx, int cz, bool blocked) {
    if (!inBounds(cx, cz)) return;
    staticBlocked_[index(cx, cz)] = blocked ? 1 : 0;
}

void NavGrid::forEachCellInRect(float minX, float minZ, float maxX, float maxZ,
                                const std::function<void(int)>& fn) const {
    const int x0 = std::max(0, static_cast<int>(std::floor((minX - origin_.x) / cellSize_)));
    const int z0 = std::max(0, static_cast<int>(std::floor((minZ - origin_.z) / cellSize_)));
    const int x1 = std::min(width_ - 1, static_cast<int>(std::floor((maxX - origin_.x) / cellSize_)));
    const int z1 = std::min(height_ - 1, static_cast<int>(std::floor((maxZ - origin_.z) / cellSize_)));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            fn(index(x, z));
        }
    }
}

void NavGrid::addObstacle(float minX, float minZ, float maxX, float maxZ) {
    forEachCellInRect(minX, minZ, maxX, maxZ, [this](int i) {
        if (obstacleCount_[i] < 0xFFFF) ++obstacleCount_[i];
    });
}

void NavGrid::removeObstacle(float minX, float minZ, float maxX, float maxZ) {
    forEachCellInRect(minX, minZ, maxX, maxZ, [this](int i) {
        if (obstacleCount_[i] > 0) --obstacleCount_[i];
    });
}

bool NavGrid::isAreaFree(float minX, float minZ, float maxX, float maxZ) const {
    // Prostokąt poza siatką nie jest wolny.
    int cx, cz;
    if (!worldToCell(minX, minZ, cx, cz) || !worldToCell(maxX, maxZ, cx, cz)) return false;
    bool free = true;
    forEachCellInRect(minX, minZ, maxX, maxZ, [&](int i) {
        if (staticBlocked_[i] != 0 || obstacleCount_[i] != 0) free = false;
    });
    return free;
}

bool NavGrid::worldToCell(float x, float z, int& cx, int& cz) const {
    cx = static_cast<int>(std::floor((x - origin_.x) / cellSize_));
    cz = static_cast<int>(std::floor((z - origin_.z) / cellSize_));
    return inBounds(cx, cz);
}

Vec3 NavGrid::cellCenter(int cx, int cz) const {
    return {
        origin_.x + (static_cast<float>(cx) + 0.5f) * cellSize_,
        0.0f,
        origin_.z + (static_cast<float>(cz) + 0.5f) * cellSize_,
    };
}

bool NavGrid::findNearestWalkable(int cx, int cz, int maxRadius, int& outX, int& outZ) const {
    if (isWalkable(cx, cz)) {
        outX = cx;
        outZ = cz;
        return true;
    }
    for (int r = 1; r <= maxRadius; ++r) {
        float bestDist = std::numeric_limits<float>::max();
        bool found = false;
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                const int x = cx + dx, z = cz + dz;
                if (!isWalkable(x, z)) continue;
                const float d = static_cast<float>(dx * dx + dz * dz);
                if (d < bestDist) {
                    bestDist = d;
                    outX = x;
                    outZ = z;
                    found = true;
                }
            }
        }
        if (found) return true;
    }
    return false;
}

void NavGrid::computeRegions(bool includeObstacles) {
    const size_t n = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    region_.assign(n, -1);
    auto passable = [&](int i) { return staticBlocked_[i] == 0 && (!includeObstacles || obstacleCount_[i] == 0); };
    std::vector<int> queue;
    queue.reserve(n);
    int next = 0;
    for (int start = 0; start < static_cast<int>(n); ++start) {
        if (region_[start] >= 0 || !passable(start)) continue;
        queue.clear();
        queue.push_back(start);
        region_[start] = next;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            const int cur = queue[qi];
            const int cx = cur % width_, cz = cur / width_;
            const int nb[4][2] = {{cx + 1, cz}, {cx - 1, cz}, {cx, cz + 1}, {cx, cz - 1}};
            for (const auto& p : nb) {
                if (!inBounds(p[0], p[1])) continue;
                const int ni = index(p[0], p[1]);
                if (region_[ni] >= 0 || !passable(ni)) continue;
                region_[ni] = next;
                queue.push_back(ni);
            }
        }
        ++next;
    }
}

int NavGrid::regionAt(float x, float z) const {
    int cx, cz;
    if (region_.empty() || !worldToCell(x, z, cx, cz)) return -1;
    return region_[index(cx, cz)];
}

bool NavGrid::sameRegion(const Vec3& a, const Vec3& b) const {
    const int ra = regionAt(a.x, a.z);
    return ra >= 0 && ra == regionAt(b.x, b.z);
}

bool NavGrid::lineWalkable(const Vec3& a, const Vec3& b) const {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / (cellSize_ * 0.35f))));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        if (!isWalkableWorld(a.x + dx * t, a.z + dz * t)) return false;
    }
    return true;
}

bool NavGrid::findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& outPath,
                       bool allowPartial, int maxExpanded) const {
    outPath.clear();
    if (width_ == 0) return false;

    int sx, sz, gx, gz;
    worldToCell(from.x, from.z, sx, sz);
    worldToCell(to.x, to.z, gx, gz);
    sx = std::clamp(sx, 0, width_ - 1);
    sz = std::clamp(sz, 0, height_ - 1);
    gx = std::clamp(gx, 0, width_ - 1);
    gz = std::clamp(gz, 0, height_ - 1);

    // Start w przeszkodzie (np. jednostka wychodząca z budynku) - zaczynamy od najbliższej wolnej komórki.
    if (!isWalkable(sx, sz)) {
        int nx, nz;
        if (!findNearestWalkable(sx, sz, 6, nx, nz)) return false;
        sx = nx;
        sz = nz;
    }
    bool goalAdjusted = false;
    if (!isWalkable(gx, gz)) {
        int nx, nz;
        if (!findNearestWalkable(gx, gz, 12, nx, nz)) return false;
        gx = nx;
        gz = nz;
        goalAdjusted = true;
    }

    if (sx == gx && sz == gz) {
        outPath.push_back(goalAdjusted ? cellCenter(gx, gz) : Vec3{to.x, 0.0f, to.z});
        return true;
    }

    // Nowa "generacja" buforów zamiast czyszczenia całych tablic.
    ++stamp_;
    if (stamp_ == 0) {
        std::fill(visitStamp_.begin(), visitStamp_.end(), 0);
        std::fill(closedStamp_.begin(), closedStamp_.end(), 0);
        stamp_ = 1;
    }

    struct Node {
        float f;
        int idx;
        bool operator>(const Node& o) const { return f > o.f; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    auto heuristic = [&](int x, int z) {
        // Odległość oktylna (dopuszczalna dla ruchu 8-kierunkowego)
        const float dx = static_cast<float>(std::abs(x - gx));
        const float dz = static_cast<float>(std::abs(z - gz));
        return (dx + dz) + (1.41421356f - 2.0f) * std::min(dx, dz);
    };

    const int start = index(sx, sz);
    const int goal = index(gx, gz);
    gScore_[start] = 0.0f;
    parent_[start] = -1;
    visitStamp_[start] = stamp_;
    open.push({heuristic(sx, sz), start});

    int bestIdx = start;
    float bestH = heuristic(sx, sz);
    int expanded = 0;
    bool reached = false;

    static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        if (closedStamp_[cur.idx] == stamp_) continue;
        closedStamp_[cur.idx] = stamp_;

        if (cur.idx == goal) {
            reached = true;
            break;
        }
        if (++expanded > maxExpanded) break;

        const int cx = cur.idx % width_;
        const int cz = cur.idx / width_;
        const float h = heuristic(cx, cz);
        if (h < bestH) {
            bestH = h;
            bestIdx = cur.idx;
        }

        for (const auto& d : dirs) {
            const int nx = cx + d[0], nz = cz + d[1];
            if (!isWalkable(nx, nz)) continue;
            const bool diagonal = d[0] != 0 && d[1] != 0;
            // Bez ścinania narożników
            if (diagonal && (!isWalkable(cx + d[0], cz) || !isWalkable(cx, cz + d[1]))) continue;
            const int ni = index(nx, nz);
            if (closedStamp_[ni] == stamp_) continue;
            const float tentative = gScore_[cur.idx] + (diagonal ? 1.41421356f : 1.0f);
            if (visitStamp_[ni] != stamp_ || tentative < gScore_[ni]) {
                visitStamp_[ni] = stamp_;
                gScore_[ni] = tentative;
                parent_[ni] = cur.idx;
                open.push({tentative + heuristic(nx, nz), ni});
            }
        }
    }

    int endIdx = reached ? goal : bestIdx;
    if (!reached && (!allowPartial || endIdx == start)) {
        return false;
    }

    // Odtworzenie ścieżki (od końca)
    std::vector<int> cells;
    for (int i = endIdx; i != -1; i = parent_[i]) {
        cells.push_back(i);
        if (i == start) break;
    }
    std::reverse(cells.begin(), cells.end());

    std::vector<Vec3> raw;
    raw.reserve(cells.size());
    for (size_t i = 1; i < cells.size(); ++i) {
        raw.push_back(cellCenter(cells[i] % width_, cells[i] / width_));
    }
    if (reached && !goalAdjusted && !raw.empty()) {
        raw.back() = {to.x, 0.0f, to.z};
    }

    // Wygładzanie: pomijamy punkty, do których da się dojść w linii prostej.
    Vec3 anchor{from.x, 0.0f, from.z};
    size_t i = 0;
    while (i < raw.size()) {
        size_t furthest = i;
        // Zachłannie wydłużamy odcinek, dopóki linia prosta jest przechodnia.
        for (size_t j = i + 1; j < raw.size(); ++j) {
            if (!lineWalkable(anchor, raw[j])) break;
            furthest = j;
        }
        outPath.push_back(raw[furthest]);
        anchor = raw[furthest];
        i = furthest + 1;
    }
    return !outPath.empty();
}

} // namespace engine3d
