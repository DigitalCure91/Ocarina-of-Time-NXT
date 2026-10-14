#include "NxtWorld.h"
#include "NxtNavigation.h"
#include "soh/ActorDB.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
#include "objects/object_sk2/object_sk2.h"
#include "objects/object_zf/object_zf.h"
#include "objects/object_mb/object_mb.h"
#include "objects/object_wf/object_wf.h"
#include "objects/object_dodongo/object_dodongo.h"
#include <cmath>
#include <cstring>
#include <type_traits>

namespace Nxt {
// OPEN_DISPS/CLOSE_DISPS declare these functions at block scope. Inside Nxt,
// a global header alone does not give those declarations C linkage. Redeclare
// the same engine API here, taking its types from the canonical header, so the
// macros reference the existing C symbols rather than new C++-mangled symbols.
extern "C" {
decltype(::FrameInterpolation_RecordOpenChild) FrameInterpolation_RecordOpenChild;
decltype(::FrameInterpolation_RecordCloseChild) FrameInterpolation_RecordCloseChild;
}

// Actors are allocated/zeroed by the C engine. Never put C++ containers here.
struct NxtActor {
    Actor actor;
    SkelAnime skel;
    Vec3s joints[70], morph[70];
    ColliderCylinder body, weapon;
    const char* animationPath;
    uint8_t ready, adult;
};
static_assert(std::is_trivially_copyable_v<NxtActor>);
struct Model { const char* skeleton; const char* idle; const char* walk; const char* attack; const char* death; };
static const Model models[] = {
    {gStalfosSkel,gStalfosMiddleGuardAnim,gStalfosFastAdvanceAnim,gStalfosDownSlashAnim,gStalfosFallOverBackwardsAnim},
    {gZfLizalfosSkel,gZfCryingAnim,gZfWalkingAnim,gZfSlashAnim,gZfDyingAnim},
    {gEnMbSpearSkel,gEnMbSpearStandStillAnim,gEnMbSpearWalkAnim,gEnMbSpearChargeAnim,gEnMbSpearFallFaceDownAnim},
    {gWolfosNormalSkel,gWolfosWaitingAnim,gWolfosRunningAnim,gWolfosSlashingAnim,gWolfosRearingUpFallingOverAnim},
    {gDodongoSkel,gDodongoWaitAnim,gDodongoWalkAnim,gDodongoBreatheFireAnim,gDodongoDieAnim}
};
static const uint8_t tunics[24][3] = {
    {36,165,94},{58,116,231},{219,55,76},{165,87,224},{235,164,45},{37,185,186},
    {219,99,173},{234,234,213},{61,66,84},{94,144,67},{72,84,172},{166,54,42},
    {105,66,137},{206,125,45},{29,112,133},{168,107,131},{144,167,187},{91,62,39},
    {153,195,70},{112,167,245},{245,128,77},{196,157,240},{210,189,126},{66,211,168}
};
// No OC collider: adventurers cannot body-block doors, push the real player, or trigger NPC interactions.
static ColliderCylinderInit bodyInit = {
    {COLTYPE_HIT0,AT_NONE,AC_ON | AC_TYPE_PLAYER,OC1_NONE,OC2_NONE,COLSHAPE_CYLINDER},
    {ELEMTYPE_UNK0,{0,0,0},{0xFFCFFFFF,0,0},TOUCH_NONE,BUMP_ON,OCELEM_NONE},
    {20,70,0,{0,0,0}}
};
static ColliderCylinderInit weaponInit = {
    {COLTYPE_HIT0,AT_ON | AT_TYPE_PLAYER,AC_NONE,OC1_NONE,OC2_NONE,COLSHAPE_CYLINDER},
    {ELEMTYPE_UNK0,{0x00000200,0,2},{0,0,0},TOUCH_ON | TOUCH_SFX_NORMAL,BUMP_NONE,OCELEM_NONE},
    {30,60,0,{0,0,0}}
};
static DamageTable damageTable;
static Gfx lizalfosTexture[] = {gsSPTexture(0x0A00,0x0A00,0,G_TX_RENDERTILE,G_ON),gsSPEndDisplayList()};

static bool SampleLink(NxtActor* self,Unit& u) {
    const char* path = u.action==Action::Dead ? gPlayerAnim_link_normal_re_dead_attack :
        u.action==Action::Attack ? gPlayerAnim_link_fighter_Lside_kiru :
        u.action==Action::Cast ? gPlayerAnim_link_fighter_defense_wait :
        u.speed>0.1f ? gPlayerAnim_link_normal_run : gPlayerAnim_link_fighter_wait_long;
    auto* animation=reinterpret_cast<LinkAnimationHeader*>(ResourceMgr_LoadAnimByName(path));
    if(!animation || !animation->segment || animation->common.frameCount<=0)return false;
    if(self->animationPath!=path){self->animationPath=path;u.animFrame=0;}
    const int count=animation->common.frameCount;
    if(u.action==Action::Attack){
        const float progress=1.0f-float(u.actionTimer)/float((std::max)(1,u.attackDuration));
        u.animFrame=Clamp(progress,0.0f,1.0f)*(count-1);
    } else {
        u.animFrame+=u.speed>0.1f?Clamp(u.speed*0.34f,0.65f,2.4f):1.0f;
        if(u.action==Action::Dead)u.animFrame=(std::min)(u.animFrame,float(count-1));
        else u.animFrame=std::fmod(u.animFrame,float(count));
    }
    const int first=Clamp(int(u.animFrame),0,count-1),second=(first+1)%count;
    const float blend=u.animFrame-first;
    // Link's resource format includes one face-control s16 after the joint vectors.
    // Read directly, avoiding the engine's fixed-size, shared player-animation job queue.
    constexpr size_t stride=sizeof(Vec3s)*PLAYER_LIMB_MAX+sizeof(int16_t);
    const auto* data=static_cast<const uint8_t*>(animation->segment);
    std::memcpy(self->joints,data+stride*first,stride);
    std::memcpy(self->morph,data+stride*second,stride);
    for(int limb=0;limb<PLAYER_LIMB_MAX;++limb){
        int16_t* lhs=&self->joints[limb].x;const int16_t* rhs=&self->morph[limb].x;
        for(int axis=0;axis<3;++axis){
            const int delta=limb==0?int(rhs[axis])-lhs[axis]:int(static_cast<int16_t>(rhs[axis]-lhs[axis]));
            lhs[axis]=static_cast<int16_t>(lhs[axis]+int(delta*blend));
        }
    }
    return true;
}
static void Animate(NxtActor* self,Unit& u) {
    if(u.bot){SampleLink(self,u);return;}
    const auto& model=models[u.profile];
    const char* path=u.action==Action::Dead?model.death:
        (u.action==Action::Attack||u.action==Action::Cast)?model.attack:u.speed>0.1f?model.walk:model.idle;
    auto* anim=reinterpret_cast<AnimationHeader*>(ResourceMgr_LoadAnimByName(path));
    if(!anim||!anim->frameData||!anim->jointIndices||anim->common.frameCount<=0)return;
    if(self->animationPath!=path){
        self->animationPath=path;
        Animation_Change(&self->skel,anim,1.0f,0.0f,Animation_GetLastFrame(anim),
            (u.action==Action::Dead||u.action==Action::Attack)?ANIMMODE_ONCE:ANIMMODE_LOOP,-3.0f);
    }
    self->skel.playSpeed=u.berserk?1.85f:u.enraged?1.40f:1.0f;
    if(u.action==Action::Attack)self->skel.playSpeed=(self->skel.endFrame+1)/(std::max)(1,u.attackDuration);
    SkelAnime_Update(&self->skel);
}
static void Initialize(Actor* actor,PlayState* play) {
    auto* self=reinterpret_cast<NxtActor*>(actor);
    const bool bot=actor->id==botActorId;
    AddUnit(actor,bot);
    auto* unit=Find(actor);if(!unit){Actor_Kill(actor);return;}
    Unit& u=*unit;
    self->adult=LINK_IS_ADULT;
    Actor_SetScale(actor,u.modelScale);
    ActorShape_Init(&actor->shape,0.0f,ActorShadow_DrawCircle,bot?22.0f:35.0f);
    actor->gravity=0;actor->room=-1;
    actor->uncullZoneForward=12000;actor->uncullZoneScale=500;actor->uncullZoneDownward=600;
    actor->naviEnemyId=0xFF;
    actor->colChkInfo.mass=MASS_IMMOVABLE;
    actor->colChkInfo.health=1;
    // Native attacks define their strength in the toucher. Link's weapon lookup
    // table belongs only on enemies; using it for bots changes trap/enemy damage.
    actor->colChkInfo.damageTable=bot?nullptr:&damageTable;
    Collider_InitCylinder(play,&self->body);
    Collider_SetCylinder(play,&self->body,actor,&bodyInit);
    self->body.base.acFlags=AC_ON | (bot?AC_TYPE_ENEMY:AC_TYPE_PLAYER);
    self->body.dim.radius=static_cast<s16>(u.radius);
    self->body.dim.height=static_cast<s16>(u.height);
    Collider_InitCylinder(play,&self->weapon);
    Collider_SetCylinder(play,&self->weapon,actor,&weaponInit);
    self->ready=1; // Collider lifetime is independent of whether an asset can be loaded.
    const char* skelPath=bot?(self->adult?gLinkAdultSkel:gLinkChildSkel):models[u.profile].skeleton;
    if(!bot&&u.bossIndex==1)skelPath=gZfDinolfosSkel;
    SkeletonHeader* header=ResourceMgr_LoadSkeletonByName(skelPath,&self->skel);
    if(!header||!header->segment||header->limbCount==0||header->limbCount+1>70||
       (header->skeletonType!=SKELANIME_TYPE_NORMAL&&header->skeletonType!=SKELANIME_TYPE_FLEX)||
       (bot&&(header->limbCount+1!=PLAYER_LIMB_MAX||header->skeletonType!=SKELANIME_TYPE_FLEX))){
        Actor_Kill(actor);return;
    }
    if(header->skeletonType==SKELANIME_TYPE_FLEX){
        const auto count=reinterpret_cast<FlexSkeletonHeader*>(header)->dListCount;
        if(count==0||count>header->limbCount){Actor_Kill(actor);return;}
    }
    if(bot){
        self->skel.skeletonHeader=header;
        self->skel.skeleton=static_cast<void**>(SEGMENTED_TO_VIRTUAL(header->segment));
        self->skel.limbCount=PLAYER_LIMB_MAX;
        self->skel.dListCount=reinterpret_cast<FlexSkeletonHeader*>(header)->dListCount;
        self->skel.jointTable=self->joints;self->skel.morphTable=self->morph;
        if(!SampleLink(self,u)){Actor_Kill(actor);return;}
    } else {
        auto* anim=reinterpret_cast<AnimationHeader*>(ResourceMgr_LoadAnimByName(models[u.profile].idle));
        if(!anim||!anim->frameData||!anim->jointIndices||anim->common.frameCount<=0){Actor_Kill(actor);return;}
        if(header->skeletonType==SKELANIME_TYPE_FLEX)
            SkelAnime_InitFlex(play,&self->skel,reinterpret_cast<FlexSkeletonHeader*>(header),anim,
                              self->joints,self->morph,header->limbCount+1);
        else
            SkelAnime_Init(play,&self->skel,header,anim,self->joints,self->morph,header->limbCount+1);
        self->animationPath=models[u.profile].idle;
    }
    self->ready=2;
    actor->focus.pos=actor->world.pos;actor->focus.pos.y+=u.height;
}
static void Destroy(Actor* actor,PlayState* play) {
    auto* self=reinterpret_cast<NxtActor*>(actor);
    if(self->ready){Collider_DestroyCylinder(play,&self->body);Collider_DestroyCylinder(play,&self->weapon);}
    // Frame arrays belong to the actor allocation, so SkelAnime_Free must not free them.
    ResourceMgr_UnregisterSkeleton(&self->skel);
    RemoveUnit(actor);
}
static void Update(Actor* actor,PlayState* play) {
    auto* self=reinterpret_cast<NxtActor*>(actor);
    Unit* ptr=Find(actor);
    if(!ptr||self->ready!=2){Actor_Kill(actor);return;}
    Unit& u=*ptr;
    if(!Running(play)){self->body.base.acFlags&=~AC_HIT;return;}
    if(self->body.base.acFlags&AC_HIT){
        self->body.base.acFlags&=~AC_HIT;
        // Projectiles may already have been destroyed earlier in this actor-update pass.
        // Use the collision-time snapshot, never pointers into a former projectile's collider.
        Actor* source=RecordedHitSource(actor);
        int damage=actor->colChkInfo.damage;
        if(damage==0)damage=RecordedTouchDamage(actor);
        // Native enemy touchers commonly have damage in their collider rather than in a damage table.
        HitUnit(u,source,damage);
        self->body.base.ac=nullptr;
    }
    UpdateUnit(u,play);
    if(!actor->update)return;
    Animate(self,u);
    actor->focus.pos=actor->world.pos;actor->focus.pos.y+=u.height;
    float fade=u.action==Action::Dead?Clamp(u.actionTimer/30.0f,0.05f,1.0f):
        u.action==Action::Depart?Clamp(u.actionTimer/15.0f,0.05f,1.0f):1.0f;
    Actor_SetScale(actor,u.modelScale*fade);
    if(Alive(u)){
        Collider_UpdateCylinder(actor,&self->body);
        // The original collision checker is quadratic. Only register bodies near the player or a combatant.
        const float distance=DistanceXZ(actor->world.pos,GET_PLAYER(play)->actor.world.pos);
        if(distance<2000||u.combatTimer>0||u.target)
            CollisionCheck_SetAC(play,&play->colChkCtx,&self->body.base);
        Unit* target=Find(u.target);
        const bool strike=u.action==Action::Attack&&u.actionTimer<=u.attackDuration/2&&
                          u.actionTimer>=u.attackDuration/2-1;
        if(u.bot&&strike&&target&&target->native&&Alive(*target)&&
           navigation.Clear(play,actor->world.pos,target->actor->world.pos)){
            self->weapon.dim.pos.x=static_cast<s16>(actor->world.pos.x+Math_SinS(actor->shape.rot.y)*35);
            self->weapon.dim.pos.z=static_cast<s16>(actor->world.pos.z+Math_CosS(actor->shape.rot.y)*35);
            self->weapon.dim.pos.y=static_cast<s16>(actor->world.pos.y);
            self->weapon.base.atFlags&=~(AT_HIT|AT_BOUNCED);
            CollisionCheck_SetAT(play,&play->colChkCtx,&self->weapon.base);
        }
        if((u.berserk||u.enraged||(u.bot&&u.action==Action::Cast)) && (frame+u.serial)%8==0 && distance<1400){
            Vec3f pos=actor->world.pos,velocity{0,1.8f,0},accel{0,0.02f,0};
            pos.y+=u.height*(0.25f+Random01(u.seed)*0.55f);
            pos.x+=(Random01(u.seed)-0.5f)*u.radius*2;pos.z+=(Random01(u.seed)-0.5f)*u.radius*2;
            Color_RGBA8 primary{255,80,30,255},environment{190,8,30,255};
            if(u.bot){primary={100,255,170,255};environment={20,150,100,255};}
            EffectSsKiraKira_SpawnSmall(play,&pos,&velocity,&accel,&primary,&environment);
        }
    }
}
static s32 LinkLimb(PlayState*,s32 limb,Gfx** display,Vec3f*,Vec3s*,void* arg) {
    auto* self=static_cast<NxtActor*>(arg);
    if(limb==PLAYER_LIMB_WAIST)*display=(Gfx*)(self->adult?gLinkAdultWaistNearDL:gLinkChildWaistNearDL);
    else if(limb==PLAYER_LIMB_L_HAND)*display=(Gfx*)(self->adult?gLinkAdultLeftHandHoldingMasterSwordNearDL:gLinkChildLeftFistAndKokiriSwordNearDL);
    else if(limb==PLAYER_LIMB_R_HAND)*display=(Gfx*)(self->adult?gLinkAdultRightHandHoldingHylianShieldNearDL:gLinkChildRightFistAndDekuShieldNearDL);
    else if(limb==PLAYER_LIMB_SHEATH)*display=(Gfx*)(self->adult?gLinkAdultSheathNearDL:gLinkChildSheathNearDL);
    return false;
}
static void Telegraph(PlayState* play,const Unit& u) {
    static char fileTag[]="NxtActors";
    if(u.bossIndex<0||u.action!=Action::Cast)return;
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(u.castOrigin.x,u.castOrigin.y+4,u.castOrigin.z,MTXMODE_NEW);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPClearGeometryMode(POLY_XLU_DISP++,G_LIGHTING|G_CULL_BACK|G_CULL_FRONT);
    gDPSetCombineMode(POLY_XLU_DISP++,G_CC_PRIMITIVE,G_CC_PRIMITIVE);
    gDPSetPrimColor(POLY_XLU_DISP++,0,0,255,72,32,static_cast<int>(120+60*std::sin(frame*0.6f)));
    gSPMatrix(POLY_XLU_DISP++,Matrix_NewMtx(play->state.gfxCtx,fileTag,__LINE__),G_MTX_NOPUSH|G_MTX_LOAD|G_MTX_MODELVIEW);
    constexpr int segments=40;
    auto* vertices=static_cast<Vtx*>(Graph_Alloc(play->state.gfxCtx,sizeof(Vtx)*segments*4));
    if(vertices){
        const float progress=1.0f-float(u.actionTimer)/(std::max)(1,u.castDuration);
        const float outer=u.castRadius,inner=outer*(0.93f-0.12f*Clamp(progress,0.0f,1.0f));
        const float start=u.bossIndex==1?u.actor->shape.rot.y*(3.14159265f/32768.0f)-1.3744f:0;
        const float arc=u.bossIndex==1?2.7488f:6.2831853f;
        for(int i=0;i<segments;++i){
            const float angles[4]={start+arc*i/segments,start+arc*(i+1)/segments,
                                   start+arc*(i+1)/segments,start+arc*i/segments};
            for(int j=0;j<4;++j){
                Vtx& v=vertices[i*4+j];std::memset(&v,0,sizeof(v));
                const float radius=j<2?outer:inner;
                v.v.ob[0]=static_cast<s16>(std::sin(angles[j])*radius);
                v.v.ob[2]=static_cast<s16>(std::cos(angles[j])*radius);
                v.v.cn[0]=255;v.v.cn[1]=255;v.v.cn[2]=255;v.v.cn[3]=255;
            }
            gSPVertex(POLY_XLU_DISP++,reinterpret_cast<uintptr_t>(vertices+i*4),4,0);
            gSP2Triangles(POLY_XLU_DISP++,0,1,2,0,0,2,3,0);
        }
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}
static void Draw(Actor* actor,PlayState* play) {
    auto* self=reinterpret_cast<NxtActor*>(actor);Unit* ptr=Find(actor);
    if(!ptr||self->ready!=2)return;
    Unit& u=*ptr;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    if(u.bot){
        const auto& color=tunics[u.color%24];
        gDPSetEnvColor(POLY_OPA_DISP++,color[0],color[1],color[2],255);
        gSPSegment(POLY_OPA_DISP++,0x08,reinterpret_cast<uintptr_t>(self->adult?gLinkAdultEyesOpenTex:gLinkChildEyesOpenTex));
        gSPSegment(POLY_OPA_DISP++,0x09,reinterpret_cast<uintptr_t>(self->adult?gLinkAdultMouth1Tex:gLinkChildMouth1Tex));
        // Link's shield/limbs call this segment as executable geometry state.
        // Every actor must establish its own segment state, including after UI
        // and other actors have replaced it with textures or unrelated lists.
        gSPSegment(POLY_OPA_DISP++,0x0C,reinterpret_cast<uintptr_t>(gCullBackDList));
        SkelAnime_DrawFlexLod(play,self->skel.skeleton,self->joints,self->skel.dListCount,LinkLimb,nullptr,self,0);
    }else{
        // Stalfos and Lizalfos meshes call segment 7 as a hilite display list.
        // Message drawing leaves the textbox TEXTURE in that same segment. If
        // this setup is omitted, the interpreter executes "__OTR__textures/..."
        // as commands (the reported 0x54/0x73/0x61/0x72 crash).
        if(u.profile==0||u.profile==1)func_8002EBCC(actor,play,1);
        gDPSetEnvColor(POLY_OPA_DISP++,u.berserk||u.enraged?110:0,0,0,255);
        if(u.profile==1){gSPSegment(POLY_OPA_DISP++,0x08,reinterpret_cast<uintptr_t>(lizalfosTexture));gSPSegment(POLY_OPA_DISP++,0x09,reinterpret_cast<uintptr_t>(&D_80116280[2]));}
        if(u.profile==3)gSPSegment(POLY_OPA_DISP++,0x08,reinterpret_cast<uintptr_t>(gWolfosNormalEyeOpenTex));
        SkelAnime_DrawSkeletonOpa(play,&self->skel,nullptr,nullptr,self);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    Telegraph(play,u);
}
void InitActors() {
    // Ordinary attacks retain meaningful differences (stun tools do not suddenly become lethal).
    const uint8_t damages[32]={0,2,1,2,0,2,2,0,1,2,4,2,2,2,2,2,2,2,2,2,0,0,1,4,2,2,8,4,0,0,4,0};
    for(int i=0;i<32;++i)damageTable.table[i]=damages[i];
    ActorDBInit init;
    init.name="NxtAdventurer";init.desc="Independent NXT adventurer";init.category=ACTORCAT_NPC;
    init.flags=ACTOR_FLAG_UPDATE_CULLING_DISABLED;init.objectId=OBJECT_GAMEPLAY_KEEP;
    init.instanceSize=sizeof(NxtActor);init.init=Initialize;init.destroy=Destroy;init.update=Update;init.draw=Draw;
    botActorId=ActorDB::Instance->AddEntry(init).entry.id;
    init.name="NxtEncounter";init.desc="NXT outdoor or dungeon encounter";init.category=ACTORCAT_PROP;
    init.flags=ACTOR_FLAG_UPDATE_CULLING_DISABLED|ACTOR_FLAG_ATTENTION_ENABLED|ACTOR_FLAG_HOSTILE;
    enemyActorId=ActorDB::Instance->AddEntry(init).entry.id;
}
} // namespace Nxt
