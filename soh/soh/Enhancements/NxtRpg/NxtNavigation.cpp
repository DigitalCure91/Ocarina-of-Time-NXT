#include "NxtNavigation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
namespace Nxt {
Navigation navigation;
static float LengthSq(const Vec3f& a, const Vec3f& b) {
    float x = a.x-b.x, y = a.y-b.y, z = a.z-b.z;
    return x*x + y*y + z*z;
}
static bool Finite(const Vec3f& pos) {
    return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z);
}
void Navigation::Reset() {
    nodes.clear(); candidates.clear(); sampleCursor = edgeCursor = 0; built = false;
}
bool Navigation::Ground(PlayState* play, Vec3f& pos, float step, float drop) const {
    if (!play || !play->colCtx.colHeader || !Finite(pos) ||
        !std::isfinite(step) || !std::isfinite(drop) || step < 0 || drop < 0) return false;
    Vec3f query = pos; query.y += step;
    CollisionPoly* poly = nullptr; int bgId = BGCHECK_SCENE;
    float height = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &query);
    if (!poly || !std::isfinite(height) || height <= BGCHECK_Y_MIN + 1 ||
        height - pos.y > step || pos.y - height > drop || poly->normal.y < 23000) return false;
    if (SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, bgId) ||
        SurfaceType_GetSceneExitIndex(&play->colCtx, poly, bgId) != 0 ||
        SurfaceType_IsWallDamage(&play->colCtx, poly, bgId) ||
        SurfaceType_GetFloorEffect(&play->colCtx, poly, bgId) == FLOOR_EFFECT_2) return false;
    const uint32_t property = func_80041EA4(&play->colCtx, poly, bgId);
    if (property == FLOOR_PROPERTY_5 || property == FLOOR_PROPERTY_12) return false;
    WaterBox* water = nullptr; float waterY = height;
    if (WaterBox_GetSurface1(play, &play->colCtx, pos.x, pos.z, &waterY, &water) && waterY > height + 12) return false;
    pos.y = height + 1.0f;
    // Standing room, not roofs with low ceilings or crawl tunnels.
    Vec3f top = pos; top.y += 58;
    return Clear(play, pos, top, 3.0f);
}
bool Navigation::Clear(PlayState* play, const Vec3f& a, const Vec3f& b, float height) const {
    if (!play || !play->colCtx.colHeader || !Finite(a) || !Finite(b) || !std::isfinite(height)) return false;
    Vec3f from = a, to = b, hit{}; from.y += height; to.y += height;
    CollisionPoly* poly = nullptr; int bgId = BGCHECK_SCENE;
    return !BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, true, true, true, &bgId);
}
bool Navigation::Walkable(PlayState* play, const Vec3f& a, const Vec3f& b) const {
    if (!Finite(a) || !Finite(b) || std::fabs(a.y-b.y) > DistanceXZ(a,b) * 0.7f + 35 || !Clear(play,a,b)) return false;
    // Do not stretch the sampling interval across long shortcuts and miss a pit.
    if (DistanceXZ(a,b) > 1680.0f) return false;
    const int steps = Clamp(static_cast<int>(std::ceil(DistanceXZ(a,b)/24.0f)), 1, 70);
    Vec3f previous = a;
    for (int i=1; i<=steps; ++i) {
        float t = static_cast<float>(i)/steps;
        Vec3f next{a.x+(b.x-a.x)*t, previous.y, a.z+(b.z-a.z)*t};
        if (!Ground(play,next,28,40)) return false;
        previous = next;
    }
    return std::fabs(previous.y-b.y) < 40;
}
void Navigation::Begin(PlayState* play) {
    Reset();
    if (!play) { built=true; return; }
    const CollisionHeader* header = play->colCtx.colHeader;
    if (!header || !header->vtxList || !header->polyList) { built=true; return; }
    uint32_t seed = 0x27D4EB2DU ^ (static_cast<uint32_t>(play->sceneNum)<<8) ^ (play->roomCtx.curRoom.num+1);
    const float span = static_cast<float>((std::max)(header->maxBounds.x-header->minBounds.x, header->maxBounds.z-header->minBounds.z));
    edgeRange = Clamp(span / 17.0f, 200.0f, 1600.0f);
    // Stratified reservoir sampling is independent of polygon ordering and bounds the build work.
    size_t seen = 0;
    constexpr size_t candidateLimit = 1500;
    for (size_t i=0; i<header->numPolygons; ++i) {
        const auto& poly=header->polyList[i];
        if (poly.normal.y < 23000) continue;
        if (COLPOLY_VTX_INDEX(poly.vtxData[0]) >= header->numVertices ||
            COLPOLY_VTX_INDEX(poly.vtxData[1]) >= header->numVertices ||
            COLPOLY_VTX_INDEX(poly.vtxData[2]) >= header->numVertices) continue;
        const auto& a=header->vtxList[COLPOLY_VTX_INDEX(poly.vtxData[0])];
        const auto& b=header->vtxList[COLPOLY_VTX_INDEX(poly.vtxData[1])];
        const auto& c=header->vtxList[COLPOLY_VTX_INDEX(poly.vtxData[2])];
        float twiceArea=std::fabs((float(b.x)-a.x)*(float(c.z)-a.z)-(float(b.z)-a.z)*(float(c.x)-a.x));
        int samples=Clamp(static_cast<int>(twiceArea/(edgeRange*edgeRange*0.32f))+1,1,24);
        for (int k=0;k<samples;++k) {
            float u = k==0 ? 1.0f/3.0f : 0.08f+Random01(seed)*0.84f;
            float v = k==0 ? 1.0f/3.0f : (0.08f+Random01(seed)*0.84f)*(1-u);
            Vec3f p{a.x+(b.x-a.x)*u+(c.x-a.x)*v, a.y+(b.y-a.y)*u+(c.y-a.y)*v+1,
                    a.z+(b.z-a.z)*u+(c.z-a.z)*v};
            ++seen;
            if (candidates.size()<candidateLimit) candidates.push_back(p);
            else { size_t at=NextRandom(seed)%seen; if(at<candidateLimit) candidates[at]=p; }
        }
    }
    // Shuffle the reservoir before truncation; sorting all candidates by player distance
    // would fill the graph budget near the entrance and omit distant world-boss terrain.
    for(size_t i=candidates.size();i>1;--i)std::swap(candidates[i-1],candidates[NextRandom(seed)%i]);
    Actor* player = &GET_PLAYER(play)->actor;
    std::vector<Vec3f> local;
    // Include the reachable entrance first; use dense local samples in tiny interiors.
    for (int r=0;r<4;++r) for (int k=0;k<12;++k) {
        float angle=k*6.2831853f/12;
        local.push_back({player->world.pos.x+std::sin(angle)*r*90,player->world.pos.y,
                              player->world.pos.z+std::cos(angle)*r*90});
    }
    candidates.insert(candidates.begin(),local.begin(),local.end());
    nodes.reserve(640);
}
void Navigation::Step(PlayState* play) {
    if (built) return;
    // Floor queries and edge probes are deliberately spread over simulation ticks.
    if (sampleCursor<candidates.size()) {
        int budget=32;
        float minSeparation=Clamp(edgeRange*0.30f,35.0f,320.0f);
        while(budget-- && sampleCursor<candidates.size()) {
            Vec3f p=candidates[sampleCursor++];
            if (!Ground(play,p,20,30)) continue;
            bool duplicate=false;
            for(const auto& n:nodes) if(LengthSq(n.pos,p)<minSeparation*minSeparation){duplicate=true;break;}
            if(!duplicate && nodes.size()<640) nodes.push_back({p,{},false});
        }
        return;
    }
    int budget=8;
    while(budget-- && edgeCursor<nodes.size()) {
        size_t index=edgeCursor++;
        std::vector<std::pair<float,int>> nearbyNodes;
        for(size_t j=0;j<nodes.size();++j) {
            if(j==index)continue;
            float distance=LengthSq(nodes[index].pos,nodes[j].pos);
            if(distance<edgeRange*edgeRange)nearbyNodes.emplace_back(distance,static_cast<int>(j));
        }
        std::sort(nearbyNodes.begin(),nearbyNodes.end());
        if(nearbyNodes.size()>10)nearbyNodes.resize(10);
        for(const auto& pair:nearbyNodes) {
            int j=pair.second;
            if(std::find(nodes[index].edges.begin(),nodes[index].edges.end(),j)!=nodes[index].edges.end())continue;
            // Edges are stored in both directions; validate both so a safe
            // step down cannot become an impossible climb on the return trip.
            if(Walkable(play,nodes[index].pos,nodes[j].pos)&&Walkable(play,nodes[j].pos,nodes[index].pos)) {
                nodes[index].edges.push_back(j);nodes[j].edges.push_back(static_cast<int>(index));
            }
        }
    }
    if(edgeCursor>=nodes.size()) { built=true; candidates.clear(); RefreshReachable(play); }
}
int Navigation::Nearest(const Vec3f& pos,bool reachableOnly) const {
    int result=-1; float best=(std::numeric_limits<float>::max)();
    for(size_t i=0;i<nodes.size();++i) {
        if(reachableOnly&&!nodes[i].reachable)continue;
        float d=LengthSq(pos,nodes[i].pos);
        if(d<best){best=d;result=static_cast<int>(i);}
    }
    return result;
}
int Navigation::Connect(PlayState* play, const Vec3f& pos, bool reachableOnly) const {
    // A Euclidean-nearest node may be on the other side of a wall or another
    // floor. Only attach a route to a node the actor can actually walk to.
    std::vector<std::pair<float,int>> nearby;
    const float range = Clamp(edgeRange * 1.5f, 350.0f, 1680.0f);
    for (size_t i=0; i<nodes.size(); ++i) {
        if (reachableOnly && !nodes[i].reachable) continue;
        float distance = LengthSq(pos,nodes[i].pos);
        if (distance <= range*range) nearby.emplace_back(distance,static_cast<int>(i));
    }
    std::sort(nearby.begin(),nearby.end());
    const size_t count = (std::min)(nearby.size(),size_t{24});
    for (size_t i=0; i<count; ++i) {
        int node = nearby[i].second;
        if (Walkable(play,pos,nodes[node].pos)) return node;
    }
    return -1;
}
void Navigation::RefreshReachable(PlayState* play) {
    for(auto& n:nodes)n.reachable=false;
    Vec3f player=GET_PLAYER(play)->actor.world.pos;
    std::queue<int> pending;
    // Connecting all visible local seeds also tolerates a temporarily closed dynamic door.
    for(size_t i=0;i<nodes.size();++i) {
        if(LengthSq(nodes[i].pos,player)<400*400 && Walkable(play,player,nodes[i].pos)) {
            nodes[i].reachable=true; pending.push(static_cast<int>(i));
        }
    }
    while(!pending.empty()) {
        int at=pending.front();pending.pop();
        for(int next:nodes[at].edges) if(!nodes[next].reachable) {
            nodes[next].reachable=true;pending.push(next);
        }
    }
}
int Navigation::ReachableCount() const {
    return static_cast<int>(std::count_if(nodes.begin(),nodes.end(),[](const NavNode& n){return n.reachable;}));
}
std::vector<int> Navigation::Route(int start,int goal) const {
    if(start<0||goal<0||start>=static_cast<int>(nodes.size())||goal>=static_cast<int>(nodes.size()))return {};
    std::vector<float> cost(nodes.size(),(std::numeric_limits<float>::max)());
    std::vector<int> previous(nodes.size(),-1);
    using Entry=std::pair<float,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    cost[start]=0.0f;open.emplace(0.0f,start);
    while(!open.empty()) {
        auto [distance,index]=open.top();open.pop();
        if(distance>cost[index]+0.01f)continue;
        if(index==goal)break;
        for(int next:nodes[index].edges) {
            float candidate=distance+std::sqrt(LengthSq(nodes[index].pos,nodes[next].pos));
            if(candidate<cost[next]){cost[next]=candidate;previous[next]=index;open.emplace(candidate,next);}
        }
    }
    if(start!=goal&&previous[goal]<0)return {};
    std::vector<int> result;
    for(int node=goal;node!=-1;node=previous[node]){result.push_back(node);if(node==start)break;}
    std::reverse(result.begin(),result.end());return result;
}
bool Navigation::Move(PlayState* play,Actor* actor,const Vec3f& desired) const {
    if (!actor || !Finite(actor->world.pos) || !Finite(desired)) return false;
    Vec3f next=desired;
    if(!Ground(play,next,28,40)||!Clear(play,actor->world.pos,next))return false;
    Vec3f resolved{};CollisionPoly* wall=nullptr;int bgId=BGCHECK_SCENE;
    // Actor collision tests use a real body radius and consider dynamic walls as well as static geometry.
    const Unit* unit=Find(actor);
    const float radius=unit?unit->radius:18.0f;
    const float height=unit?Clamp(unit->height*0.5f,25.0f,100.0f):35.0f;
    BgCheck_EntitySphVsWall3(&play->colCtx,&resolved,&next,&actor->world.pos,radius,&wall,&bgId,actor,height);
    if(LengthSq(next,resolved)>30*30)return false;
    if(!Ground(play,resolved,28,40))return false;
    const float dx=resolved.x-actor->world.pos.x, dz=resolved.z-actor->world.pos.z;
    const float wantedX=desired.x-actor->world.pos.x, wantedZ=desired.z-actor->world.pos.z;
    // A wall solver may return the same point, or push the actor backwards.
    // Neither is progress toward the requested step.
    if (dx*dx+dz*dz < 0.01f || dx*wantedX+dz*wantedZ <= 0.0f) return false;
    actor->world.pos=resolved;
    return true;
}
bool Navigation::SpawnPoint(PlayState* play,uint32_t& seed,Vec3f& result,bool large,const Vec3f* around) const {
    if(!built||nodes.empty())return false;
    float best=-1;bool found=false;
    const auto player=GET_PLAYER(play)->actor.world.pos;
    for(int attempt=0;attempt<80;++attempt) {
        const auto& node=nodes[NextRandom(seed)%nodes.size()];if(!node.reachable)continue;
        Vec3f p=node.pos;
        p.x+=(Random01(seed)-0.5f)*50;p.z+=(Random01(seed)-0.5f)*50;
        if(around&&(DistanceXZ(p,*around)>950||DistanceXZ(p,*around)<200))continue;
        if(DistanceXZ(p,player)<(large?500:95))continue;
        if(!Ground(play,p,28,35)||!Clear(play,node.pos,p))continue;
        float gap=large?1200.0f:600.0f;
        for(const auto& [actor,u]:units) {
            if(!Alive(u)||u.native)continue;
            gap=(std::min)(gap,DistanceXZ(actor->world.pos,p)-u.radius);
        }
        if(gap<(large?180:24))continue;
        if(large) {
            Vec3f top=p;top.y+=430;
            if(!Clear(play,p,top,3.0f))continue;
            bool fits=true;
            for(int k=0;k<8;++k){float a=k*6.2831853f/8;Vec3f q{p.x+std::sin(a)*160,p.y,p.z+std::cos(a)*160};
                if(!Ground(play,q,30,40)||!Clear(play,p,q)){fits=false;break;}}
            if(!fits)continue;
        }
        float score=gap+(around?0:(std::min)(DistanceXZ(p,player)*0.03f,150.0f));
        if(score>best){best=score;result=p;found=true;}
    }
    return found;
}
} // namespace Nxt
