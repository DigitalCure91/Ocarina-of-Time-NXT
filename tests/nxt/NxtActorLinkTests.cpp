// Link the actual NxtActors.cpp in a SEPARATE translation unit. Engine APIs below
// are test fixtures using the real declarations, never production replacements.
// This detects C/C++ linkage mismatches that object-only compilation cannot catch.
// Includes per-model render-state contracts: poison inherited segment state with
// the precise texture named by the crash log, then require executable lists where
// native models call segments 7/8/9/12. No GPU or ROM assets are exercised here.
#include "soh/Enhancements/NxtRpg/NxtWorld.h"
#include "soh/Enhancements/NxtRpg/NxtNavigation.h"
#include "soh/ActorDB.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace Fixture {
std::array<ActorDB::Entry, 2> entries;
int registered = 0, opens = 0, closes = 0, depth = 0;
int skeletonDraws = 0, linkDraws = 0, graphOpens = 0, graphCloses = 0;
int matrixDepth = 0;
int rigidInits = 0, flexInits = 0, destroyedColliders = 0, unregisteredSkeletons = 0;
int invalidAsset = 0;
bool failAllocation = false;
Nxt::Unit unit;
std::array<Vtx, 160> vertices{};
std::array<void*, 70> limbs{};
FlexSkeletonHeader skeleton{};
AnimationHeader enemyAnimation{};
std::array<s16, 3> enemyFrameData{};
std::array<JointIndex, 70> enemyJointIndices{};
LinkAnimationHeader linkAnimation{};
std::array<s16, (PLAYER_LIMB_MAX * 3 + 1) * 2> linkFrames{};
Mtx matrix{};
std::array<uintptr_t, 16> segments{};
const char messageTexture[] = "__OTR__textures/message_static/gDefaultMessageBackgroundTex";
Gfx hiliteCommands[] = {gsDPSetTileSize(1,0,0,0x40,0x40),gsSPEndDisplayList()};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void RequireDisplayList(int segment, int firstOpcode, const char* message) {
    Require(segments[segment] != 0 && segments[segment] != reinterpret_cast<uintptr_t>(messageTexture),message);
    const auto* commands=reinterpret_cast<const Gfx*>(segments[segment]);
    Require((commands[0].words.w0>>24)==static_cast<uintptr_t>(firstOpcode),message);
    Require((commands[1].words.w0>>24)==G_ENDDL,"Segment does not contain a terminated display list");
}
}

ActorDB* ActorDB::Instance = nullptr;
ActorDB::ActorDB() = default;
ActorDB::Entry::Entry() : entry{} {}
ActorDB::Entry& ActorDB::AddEntry(const ActorDBInit& init) {
    Fixture::Require(Fixture::registered < 2, "Unexpected actor registration");
    auto& result = Fixture::entries[static_cast<size_t>(Fixture::registered)];
    result.entry.id = 700 + Fixture::registered++;
    result.entry.instanceSize = init.instanceSize;
    result.entry.init = init.init;
    result.entry.destroy = init.destroy;
    result.entry.update = init.update;
    result.entry.draw = init.draw;
    return result;
}

namespace Nxt {
int botActorId = -1, enemyActorId = -1, frame = 0;
Navigation navigation;
Unit* Find(Actor* actor) { return actor && Fixture::unit.actor == actor ? &Fixture::unit : nullptr; }
void AddUnit(Actor* actor, bool bot) {
    Fixture::unit = Unit{};
    Fixture::unit.actor = actor;
    Fixture::unit.bot = bot;
    Fixture::unit.profile = bot ? 0 : actor->params;
}
void RemoveUnit(Actor*) { Fixture::unit = Unit{}; }
bool Running(PlayState*) { return false; }
bool Alive(const Unit& unit) { return unit.hp > 0.0f; }
void UpdateUnit(Unit&, PlayState*) {}
void HitUnit(Unit&, Actor*, int, float) {}
Actor* RecordedHitSource(Actor*) { return nullptr; }
int RecordedTouchDamage(Actor*) { return 0; }
float DistanceXZ(const Vec3f& a, const Vec3f& b) { return std::hypot(a.x - b.x, a.z - b.z); }
bool Navigation::Clear(PlayState*, const Vec3f&, const Vec3f&, float) const { return true; }
}

extern "C" {
SaveContext gSaveContext{};
Gfx D_80116280[8]{};
Gfx gCullBackDList[] = {gsSPGeometryMode(G_CULL_FRONT,G_CULL_BACK),gsSPEndDisplayList()};
void FrameInterpolation_RecordOpenChild(const void*, int) { ++Fixture::opens; ++Fixture::depth; }
void FrameInterpolation_RecordCloseChild(void) {
    Fixture::Require(Fixture::depth > 0, "Unbalanced interpolation close");
    ++Fixture::closes; --Fixture::depth;
}
void Graph_OpenDisps(Gfx**, GraphicsContext*, const char*, s32) { ++Fixture::graphOpens; }
void Graph_CloseDisps(Gfx**, GraphicsContext*, const char*, s32) { ++Fixture::graphCloses; }
void* Graph_Alloc(GraphicsContext*, size_t size) {
    Fixture::Require(size <= sizeof(Fixture::vertices), "Unexpected vertex allocation size");
    return Fixture::failAllocation ? nullptr : Fixture::vertices.data();
}
void Actor_Kill(Actor* actor) { actor->update = nullptr; }
void Actor_SetScale(Actor* actor, f32 scale) { actor->scale = {scale, scale, scale}; }
void ActorShape_Init(ActorShape*, f32, ActorShadowFunc, f32) {}
void ActorShadow_DrawCircle(Actor*, Lights*, PlayState*) {}
s32 Collider_InitCylinder(PlayState*, ColliderCylinder*) { return 1; }
s32 Collider_DestroyCylinder(PlayState*, ColliderCylinder*) { ++Fixture::destroyedColliders; return 1; }
s32 Collider_SetCylinder(PlayState*, ColliderCylinder*, Actor*, ColliderCylinderInit*) { return 1; }
void Collider_UpdateCylinder(Actor*, ColliderCylinder*) {}
s32 CollisionCheck_SetAC(PlayState*, CollisionCheckContext*, Collider*) { return 1; }
s32 CollisionCheck_SetAT(PlayState*, CollisionCheckContext*, Collider*) { return 1; }
void EffectSsKiraKira_SpawnSmall(PlayState*, Vec3f*, Vec3f*, Vec3f*, Color_RGBA8*, Color_RGBA8*) {}
AnimationHeaderCommon* ResourceMgr_LoadAnimByName(const char*) {
    Fixture::enemyAnimation.common.frameCount = 2;
    Fixture::enemyAnimation.frameData = Fixture::enemyFrameData.data();
    Fixture::enemyAnimation.jointIndices = Fixture::enemyJointIndices.data();
    Fixture::linkAnimation.common.frameCount = 2;
    Fixture::linkAnimation.segment = Fixture::linkFrames.data();
    if(Fixture::invalidAsset==5)return nullptr;
    if(Fixture::invalidAsset==6){
        Fixture::linkAnimation.segment=nullptr;
        Fixture::enemyAnimation.frameData=nullptr;
    }
    return Fixture::unit.bot ? &Fixture::linkAnimation.common : &Fixture::enemyAnimation.common;
}
SkeletonHeader* ResourceMgr_LoadSkeletonByName(const char*, SkelAnime*) {
    Fixture::skeleton.sh.segment = Fixture::limbs.data();
    Fixture::skeleton.sh.limbCount = static_cast<u8>(Fixture::unit.bot ? PLAYER_LIMB_MAX - 1 : 1);
    Fixture::skeleton.dListCount = 1;
    Fixture::skeleton.sh.skeletonType = Fixture::unit.bot||Fixture::unit.profile==2||Fixture::unit.profile==3 ?
                                      SKELANIME_TYPE_FLEX:SKELANIME_TYPE_NORMAL;
    if(Fixture::invalidAsset==1)return nullptr;
    if(Fixture::invalidAsset==2)Fixture::skeleton.sh.segment=nullptr;
    if(Fixture::invalidAsset==3)Fixture::skeleton.sh.limbCount=70;
    if(Fixture::invalidAsset==4)Fixture::skeleton.sh.skeletonType=SKELANIME_TYPE_CURVE;
    if(Fixture::invalidAsset==7)Fixture::skeleton.dListCount=0;
    if(Fixture::invalidAsset==8)Fixture::skeleton.dListCount=70;
    return &Fixture::skeleton.sh;
}
void ResourceMgr_UnregisterSkeleton(SkelAnime*) { ++Fixture::unregisteredSkeletons; }
s16 Animation_GetLastFrame(void*) { return 1; }
void Animation_Change(SkelAnime*, AnimationHeader*, f32, f32, f32, u8, f32) {}
s32 SkelAnime_Update(SkelAnime*) { return 0; }
s32 SkelAnime_Init(PlayState*, SkelAnime* skel, SkeletonHeader* header, AnimationHeader*,
                   Vec3s* joints, Vec3s* morphs, s32 count) {
    Fixture::Require(header->skeletonType==SKELANIME_TYPE_NORMAL,"Flexible skeleton used rigid initializer");
    ++Fixture::rigidInits;
    skel->skeletonHeader=header;
    skel->skeleton = header->segment;
    skel->jointTable = joints;
    skel->morphTable = morphs;
    skel->limbCount = static_cast<u8>(count);
    return 1;
}
s32 SkelAnime_InitFlex(PlayState*, SkelAnime* skel, FlexSkeletonHeader* header, AnimationHeader*,
                      Vec3s* joints, Vec3s* morphs, s32 count) {
    Fixture::Require(header->sh.skeletonType==SKELANIME_TYPE_FLEX,"Rigid skeleton used flexible initializer");
    ++Fixture::flexInits;
    skel->skeletonHeader=&header->sh;
    skel->skeleton=header->sh.segment;
    skel->jointTable=joints;
    skel->morphTable=morphs;
    skel->limbCount=static_cast<u8>(count);
    skel->dListCount=header->dListCount;
    return 1;
}
void SkelAnime_DrawFlexLod(PlayState*, void**, Vec3s*, s32 count, OverrideLimbDrawOpa,
                          PostLimbDrawOpa, void*, s32) {
    Fixture::Require(count==1,"Link matrix count is invalid");
    Fixture::RequireDisplayList(12,G_GEOMETRYMODE,"Link inherited a non-command culling segment");
    ++Fixture::linkDraws;
}
void SkelAnime_DrawSkeletonOpa(PlayState*, SkelAnime* skel, OverrideLimbDrawOpa,
                              PostLimbDrawOpa, void*) {
    if(Fixture::unit.profile==0||Fixture::unit.profile==1)
        Fixture::RequireDisplayList(7,G_SETTILESIZE,"Enemy would execute textbox texture as hilite commands");
    if(Fixture::unit.profile==1){
        Fixture::RequireDisplayList(8,G_TEXTURE,"Lizalfos texture setup is not executable");
        Fixture::Require(Fixture::segments[9]==reinterpret_cast<uintptr_t>(&D_80116280[2]),
                         "Lizalfos render-mode segment was not initialized");
    }
    if(skel->skeletonHeader->skeletonType==SKELANIME_TYPE_FLEX)
        Fixture::Require(skel->dListCount==1,"Flexible enemy did not initialize its matrix count");
    ++Fixture::skeletonDraws;
}
void Gfx_SetupDL_25Opa(GraphicsContext*) {}
void Gfx_SetupDL_25Xlu(GraphicsContext*) {}
void gSPSegment(void* command, int segment, uintptr_t target) {
    Fixture::segments[segment]=target;
    __gSPSegment(command,segment,target);
}
void func_8002EBCC(Actor*,PlayState* play,s32 flag) {
    Fixture::Require(flag==1,"Hilite segment was not requested");
    gSPSegment(play->state.gfxCtx->polyOpa.p++,7,reinterpret_cast<uintptr_t>(Fixture::hiliteCommands));
}
void gSPVertex(Gfx*, uintptr_t, int, int) {}
f32 Math_SinS(s16 angle) { return std::sin(static_cast<float>(angle) * (3.14159265f / 32768.0f)); }
f32 Math_CosS(s16 angle) { return std::cos(static_cast<float>(angle) * (3.14159265f / 32768.0f)); }
void Matrix_Push(void) { ++Fixture::matrixDepth; }
void Matrix_Pop(void) { --Fixture::matrixDepth; }
void Matrix_Translate(f32, f32, f32, u8) {}
Mtx* Matrix_NewMtx(GraphicsContext*, char*, s32) { return &Fixture::matrix; }
}

int main() {
    try {
        ActorDB registry;
        ActorDB::Instance = &registry;
        Nxt::InitActors();
        Fixture::Require(Fixture::registered == 2, "Actors were not registered");
        Fixture::Require(Nxt::botActorId == 700 && Nxt::enemyActorId == 701, "Unexpected actor ids");
        auto play = std::make_unique<PlayState>();
        auto graphics = std::make_unique<GraphicsContext>();
        std::array<Gfx, 512> opa{}, xlu{};
        play->state.gfxCtx = graphics.get();
        auto drawCase = [&](bool bot, int boss, bool allocationFailure) {
            const auto& entry = Fixture::entries[bot ? 0 : 1].entry;
            Fixture::Require(entry.init && entry.destroy && entry.update && entry.draw,
                             "Actor callbacks are incomplete");
            std::unique_ptr<Actor, decltype(&std::free)> actor(
                static_cast<Actor*>(std::calloc(1, entry.instanceSize)), &std::free);
            Fixture::Require(actor != nullptr, "Actor allocation failed");
            actor->id = static_cast<s16>(entry.id);
            actor->params = static_cast<s16>(boss < 0 ? 0 : boss);
            actor->update = entry.update;
            entry.init(actor.get(), play.get());
            Fixture::Require(actor->update != nullptr, "Actor initialization failed");
            Fixture::Require((actor->colChkInfo.damageTable==nullptr)==bot,
                             "Enemy weapon damage table was incorrectly assigned to a bot");
            Fixture::unit.bossIndex = boss;
            Fixture::unit.profile = boss < 0 ? 0 : boss;
            Fixture::unit.action = boss < 0 ? Nxt::Action::Roam : Nxt::Action::Cast;
            Fixture::unit.castDuration = 40;
            Fixture::unit.actionTimer = 20;
            Fixture::failAllocation = allocationFailure;
            graphics->polyOpa.p = opa.data();
            graphics->polyXlu.p = xlu.data();
            Fixture::segments.fill(reinterpret_cast<uintptr_t>(Fixture::messageTexture));
            const int before = Fixture::opens;
            entry.draw(actor.get(), play.get());
            Fixture::Require(Fixture::opens - before == (boss < 0 ? 1 : 2),
                             "Draw/telegraph did not call interpolation recorders");
            Fixture::Require(Fixture::opens == Fixture::closes && Fixture::depth == 0,
                             "Interpolation scope leaked");
            Fixture::Require(Fixture::matrixDepth == 0, "Matrix scope leaked");
            entry.destroy(actor.get(), play.get());
        };
        gSaveContext.linkAge = LINK_AGE_CHILD;
        drawCase(true, -1, false);
        gSaveContext.linkAge = LINK_AGE_ADULT;
        drawCase(true, -1, false);
        drawCase(false, -1, false);
        for (int boss = 0; boss < Nxt::BossCount; ++boss) drawCase(false, boss, false);
        drawCase(false, 0, true);
        Fixture::Require(Fixture::linkDraws == 2 && Fixture::skeletonDraws == 7,
                         "Expected draw branches were not reached");
        Fixture::Require(Fixture::rigidInits==5&&Fixture::flexInits==2,
                         "Expected rigid/flexible model initializers were not used");
        // Missing/incompatible model and animation resources must terminate
        // initialization safely, with every initialized collider cleaned up.
        int rejectedAssets=0;
        for(bool bot:{false,true})for(int fault=1;fault<=8;++fault){
            const auto& entry=Fixture::entries[bot?0:1].entry;
            std::unique_ptr<Actor,decltype(&std::free)> actor(
                static_cast<Actor*>(std::calloc(1,entry.instanceSize)),&std::free);
            Fixture::Require(actor!=nullptr,"Actor allocation failed");
            actor->id=static_cast<s16>(entry.id);
            actor->params=2; // flexible Moblin, so invalid matrix counts are tested as well
            actor->update=entry.update;
            Fixture::invalidAsset=fault;
            const int destroyedBefore=Fixture::destroyedColliders;
            const int unregisteredBefore=Fixture::unregisteredSkeletons;
            entry.init(actor.get(),play.get());
            Fixture::Require(actor->update==nullptr,"Invalid resource was accepted");
            entry.destroy(actor.get(),play.get());
            Fixture::Require(Fixture::destroyedColliders-destroyedBefore==2,
                             "Failed actor initialization leaked collider lifetime");
            Fixture::Require(Fixture::unregisteredSkeletons-unregisteredBefore==1,
                             "Failed actor initialization leaked skeleton registration");
            ++rejectedAssets;
        }
        Fixture::invalidAsset=0;
#ifndef NDEBUG
        Fixture::Require(Fixture::graphOpens == Fixture::opens && Fixture::graphCloses == Fixture::closes,
                         "Debug display-list scopes are unbalanced");
#else
        Fixture::Require(Fixture::graphOpens == 0 && Fixture::graphCloses == 0,
                         "Unexpected debug graph calls in Release");
#endif
        std::cout << "PASS actual NxtActors.cpp linked against C-linkage engine fixtures\n"
                  << "PASS 9 draw cases; child/adult, enemy, 5 boss telegraphs, allocation failure\n"
                  << "PASS per-model display-list segments replace poisoned textbox texture state\n"
                  << "PASS rigid/flexible skeleton initialization and " << rejectedAssets << " invalid-asset cleanup cases\n"
                  << "PASS " << Fixture::opens << " balanced interpolation scopes\n";
        ActorDB::Instance = nullptr;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
