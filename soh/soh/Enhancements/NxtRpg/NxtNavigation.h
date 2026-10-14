#pragma once
#include "NxtWorld.h"
namespace Nxt {
struct NavNode { Vec3f pos; std::vector<int> edges; bool reachable = false; };
class Navigation {
  public:
    void Reset();
    void Begin(PlayState* play);
    void Step(PlayState* play);
    void RefreshReachable(PlayState* play);
    bool Ready() const { return built; }
    bool Ground(PlayState* play, Vec3f& pos, float step = 45.0f, float drop = 60.0f) const;
    bool Clear(PlayState* play, const Vec3f& a, const Vec3f& b, float height = 30.0f) const;
    bool Walkable(PlayState* play, const Vec3f& a, const Vec3f& b) const;
    bool Move(PlayState* play, Actor* actor, const Vec3f& desired) const;
    int Nearest(const Vec3f& pos, bool reachableOnly = false) const;
    int Connect(PlayState* play, const Vec3f& pos, bool reachableOnly = false) const;
    std::vector<int> Route(int start, int goal) const;
    bool SpawnPoint(PlayState* play, uint32_t& seed, Vec3f& result, bool large = false,
                    const Vec3f* around = nullptr) const;
    std::vector<NavNode> nodes;
    int ReachableCount() const;
  private:
    std::vector<Vec3f> candidates;
    size_t sampleCursor = 0, edgeCursor = 0;
    bool built = false;
    float edgeRange = 500.0f;
};
extern Navigation navigation;
} // namespace Nxt
