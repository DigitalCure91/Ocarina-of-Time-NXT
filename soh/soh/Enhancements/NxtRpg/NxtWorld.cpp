#include "NxtWorld.h"
#include "NxtBridge.h"
#include "NxtNavigation.h"
#include "NxtConfig.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>

extern "C" { extern PlayState* gPlayState; }
namespace Nxt {
Options options;
std::unordered_map<Actor*, Unit> units;
std::array<BossView, BossCount> bossViews;
std::vector<CombatText> combatText;
int botActorId=-1,enemyActorId=-1,frame=0;
static uint32_t nextSerial=1, worldSeed=0x84A29B71;
static int scalingRevision=0;
static int activeScene=-1,activeRoom=-128,lastLevel=1,lastHurt=-100000,sceneAge=0;
static bool repopulate=false,tearingDown=false,skipDamageConversion=false;
static float regenBank=0;
static std::string status="Waiting for a playable scene";
static std::deque<int> enemyRespawns,botRespawns;
static std::array<Vec3f,BossCount> bossHomes{};
static std::array<bool,BossCount> bossHomeValid{};
struct HitRecord {
    Actor* source=nullptr;
    int tick=0;
    uint8_t touchDamage=0;
    Vec3f origin{};
};
static std::unordered_map<Actor*,HitRecord> hits;
Actor* RecordedHitSource(Actor* defender){
    auto hit=hits.find(defender);return hit!=hits.end()&&frame-hit->second.tick<8?hit->second.source:nullptr;
}
int RecordedTouchDamage(Actor* defender){
    auto hit=hits.find(defender);return hit!=hits.end()&&frame-hit->second.tick<8?hit->second.touchDamage:0;
}
static const char* bossNames[BossCount]={"Morrow, the Bone Colossus","Varkesh, Hyrule's Ravager",
    "Gorvath, the Iron Bulwark","Ashfang, the Moonstalker","Cindermaw, the Earthshaker"};
static const char* profileNames[5]={"Wandering Stalfos","Field Lizalfos","Marauding Moblin","Feral Wolfos","Wild Dodongo"};
static const char* botNames[]={"Aster","Rowan","Kael","Liora","Fen","Arden","Mira","Talon","Sera","Riven",
    "Orin","Lyra","Vex","Cedar","Eira","Rune","Ashen","Briar","Cora","Dax","Elara","Flint","Galen","Hale",
    "Iris","Jora","Kestrel","Lark","Moss","Nera","Oak","Pike","Quill","Reed","Sable","Thorn","Una","Vale",
    "Wren","Yara","Zephyr","Amber","Bram","Ciro","Dawn","Ember","Fable","Grove","Haven","Indra",
    "Juniper","Kira","Lumen","Merin","Nyx","Oriel","Perrin","Rook","Sol","Tessa","Vela","Willow","Zara","Nox"};
const char* BossName(int i){return bossNames[Clamp(i,0,BossCount-1)];}
const char* ProfileName(int i){return profileNames[Clamp(i,0,4)];}
const char* RoleName(Role role){return role==Role::Mender?"Mender":role==Role::Skirmisher?"Skirmisher":"Vanguard";}
const char* ActionName(Action a){switch(a){case Action::Roam:return "Exploring";case Action::Pursue:return "Engaging";
    case Action::Attack:return "Attacking";case Action::Recover:return "Recovering";case Action::Cast:return "Casting";
    case Action::Dead:return "Defeated";case Action::Depart:return "Departing";}return "";}
const std::string& Status(){return status;}
float DistanceXZ(const Vec3f& a,const Vec3f& b){float x=a.x-b.x,z=a.z-b.z;return std::sqrt(x*x+z*z);}
Unit* Find(Actor* a){auto it=units.find(a);return it==units.end()?nullptr:&it->second;}
bool Alive(const Unit& u){return u.actor&&u.actor->update&&u.hp>0&&u.action!=Action::Dead&&u.action!=Action::Depart;}
Stats CurrentPlayerStats(){return PlayerStats(options.rpg?static_cast<int>(gSaveContext.ship.nxt.level):1);}
bool SceneEnabled(int scene){
    if(scene<0||scene>=SCENE_TEST01)return false;
    if(scene>=SCENE_DEKU_TREE_BOSS&&scene<=SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR)return false;
    switch(scene){
        case SCENE_CHAMBER_OF_THE_SAGES:case SCENE_CUTSCENE_MAP:case SCENE_CASTLE_COURTYARD_GUARDS_DAY:
        case SCENE_CASTLE_COURTYARD_GUARDS_NIGHT:case SCENE_CASTLE_COURTYARD_ZELDA:
        case SCENE_GANON_BOSS:case SCENE_SHOOTING_GALLERY:case SCENE_BOMBCHU_BOWLING_ALLEY:
        case SCENE_FISHING_POND:case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR:
        case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE:return false;
        default:return true;
    }
}
bool CombatScene(int scene){
    // Explicit allowlist: a newly added interior is safe unless deliberately classified as combat terrain.
    if(scene>=SCENE_DEKU_TREE&&scene<=SCENE_ICE_CAVERN)return true;
    switch(scene){
        case SCENE_GERUDO_TRAINING_GROUND:case SCENE_INSIDE_GANONS_CASTLE:
        case SCENE_REDEAD_GRAVE:case SCENE_ROYAL_FAMILYS_TOMB:
        case SCENE_HYRULE_FIELD:case SCENE_ZORAS_RIVER:case SCENE_SACRED_FOREST_MEADOW:
        case SCENE_LAKE_HYLIA:case SCENE_ZORAS_FOUNTAIN:case SCENE_GERUDO_VALLEY:case SCENE_LOST_WOODS:
        case SCENE_DESERT_COLOSSUS:case SCENE_HAUNTED_WASTELAND:case SCENE_DEATH_MOUNTAIN_TRAIL:
        case SCENE_DEATH_MOUNTAIN_CRATER:case SCENE_OUTSIDE_GANONS_CASTLE:return true;
        default:return false;
    }
}
const char* SceneKind(int scene){return !SceneEnabled(scene)?"Protected scene":CombatScene(scene)?"Combat territory":"Safe settlement / interior";}
bool Running(PlayState* play){
    return options.enabled&&play&&play->state.running&&SceneEnabled(play->sceneNum)&&
        gSaveContext.gameMode==GAMEMODE_NORMAL&&gSaveContext.health>0&&play->pauseCtx.state==0&&
        play->csCtx.state==CS_STATE_IDLE&&play->msgCtx.msgMode==MSGMODE_NONE&&
        play->transitionTrigger==TRANS_TRIGGER_OFF&&GET_PLAYER(play)&&
        !Player_InCsMode(play);
}
void ReadOptions(){
    const Options previous=options;
    auto integer=[](const char* name,int def,int lo,int hi){return Clamp(ConfigInt(name,def),lo,hi);};
    auto real=[](const char* name,float def,float lo,float hi){float x=ConfigFloat(name,def);return std::isfinite(x)?Clamp(x,lo,hi):def;};
    options.enabled=integer(NXT_CVAR("Enabled"),1,0,1);options.rpg=integer(NXT_CVAR("Rpg"),1,0,1);
    options.bots=integer(NXT_CVAR("Bots"),1,0,1);options.enemies=integer(NXT_CVAR("Enemies"),1,0,1);
    options.bosses=integer(NXT_CVAR("WorldBosses"),1,0,1);options.nativeScaling=integer(NXT_CVAR("ScaleNative"),1,0,1);
    options.regen=integer(NXT_CVAR("Regen"),1,0,1);options.hud=integer(NXT_CVAR("Hud"),1,0,1);
    options.plates=integer(NXT_CVAR("Nameplates"),1,0,1);options.friendlyPlates=integer(NXT_CVAR("FriendlyPlates"),1,0,1);
    options.sharing=integer(NXT_CVAR("ShareXp"),1,0,1);
    options.botCount=integer(NXT_CVAR("BotCount"),30,0,50);options.indoorCount=integer(NXT_CVAR("IndoorCount"),10,0,50);
    options.enemyRatio=integer(NXT_CVAR("EnemyRatio"),2,0,4);options.enemyCap=integer(NXT_CVAR("EnemyCap"),90,0,120);
    options.raidSize=integer(NXT_CVAR("RaidSize"),12,0,24);options.raidCap=integer(NXT_CVAR("RaidCap"),36,0,48);
    options.variance=integer(NXT_CVAR("LevelSpread"),3,0,10);options.berserkChance=integer(NXT_CVAR("BerserkChance"),20,0,100);
    options.respawnSeconds=integer(NXT_CVAR("EnemyRespawn"),35,5,300);
    options.bossRespawnMinutes=integer(NXT_CVAR("BossRespawn"),20,2,120);
    options.bossDuration=integer(NXT_CVAR("BossDuration"),300,60,900);
    options.regenDelay=integer(NXT_CVAR("RegenDelay"),7,0,60);
    options.plateLimit=integer(NXT_CVAR("PlateLimit"),24,4,64);options.plateDistance=integer(NXT_CVAR("PlateDistance"),1500,200,4000);
    options.xpRate=real(NXT_CVAR("XpRate"),1,0.1f,10);options.damageRate=real(NXT_CVAR("DamageRate"),1,0.1f,5);
    options.regenPercent=real(NXT_CVAR("RegenPercent"),3,0.1f,20);
    options.bossHpRate=real(NXT_CVAR("BossHpRate"),1,0.25f,5);options.botPower=real(NXT_CVAR("BotPower"),1,0.25f,3);
    if(previous.rpg!=options.rpg||previous.nativeScaling!=options.nativeScaling||previous.botPower!=options.botPower||
       previous.bossHpRate!=options.bossHpRate||previous.bossDuration!=options.bossDuration||
       previous.raidSize!=options.raidSize||previous.variance!=options.variance)++scalingRevision;
}
int DesiredBots(int scene){
    if(!options.bots||!SceneEnabled(scene))return 0;
    std::string key=NXT_CVAR("Scene.")+std::to_string(scene)+".Bots";
    int local=Clamp(ConfigInt(key.c_str(),-1),-1,50);
    if(local>=0)return local;
    return (scene<SCENE_HYRULE_FIELD&&!CombatScene(scene))?options.indoorCount:options.botCount;
}
int DesiredEnemies(int scene){return options.enemies&&CombatScene(scene)?(std::min)(options.enemyCap,
    (std::max)(10,DesiredBots(scene))*options.enemyRatio):0;}
int LivingCount(bool bots,bool raidOnly){int n=0;for(const auto& [a,u]:units)
    if(Alive(u)&&!u.native&&u.bot==bots&&(raidOnly?u.raid:!u.raid)&&u.bossIndex<0)++n;return n;}
void RequestRepopulate(){repopulate=true;}
void AddText(const Vec3f& pos,const std::string& text,uint32_t color){
    if(combatText.size()>=48)combatText.erase(combatText.begin());
    combatText.push_back({pos,text,color,40});
}
static Actor* ResolveSource(Actor* a){
    if(!a||!gPlayState)return nullptr;
    if(a==&GET_PLAYER(gPlayState)->actor||Find(a))return a;
    if(a->parent&&(a->parent==&GET_PLAYER(gPlayState)->actor||Find(a->parent)))return a->parent;
    // Negative arrow parameters are the engine's hostile-arrow variant.
    // Resolving every arrow to Link made these attacks friendly fire to bots.
    if(a->id==ACTOR_EN_ARROW&&a->params<0)return a;
    switch(a->id){case ACTOR_EN_ARROW:case ACTOR_EN_BOOM:case ACTOR_EN_BOM:case ACTOR_EN_BOM_CHU:
        case ACTOR_ARMS_HOOK:case ACTOR_MAGIC_FIRE:case ACTOR_EN_M_THUNDER:return &GET_PLAYER(gPlayState)->actor;
        default:return a;}
}
void Rescale(Unit& u){
    float ratio=u.stats.maxHp>0?Clamp(u.hp/u.stats.maxHp,0.0f,1.0f):1.0f;
    uint32_t seed=u.serial*0x9E3779B9U+17;
    int level=VariedLevel(CurrentPlayerStats().level,options.variance,seed,u.bossIndex>=0?5:0);
    u.stats=u.bot?PlayerStats(level):EnemyStats(level,seed,u.berserk);
    if(u.bot){
        u.stats.attack=static_cast<int>(u.stats.attack*options.botPower*(u.role==Role::Mender?0.72f:0.88f));
        if(u.role==Role::Vanguard){u.stats.maxHp=static_cast<int>(u.stats.maxHp*1.30f);u.stats.defense+=6;}
        if(u.role==Role::Skirmisher){u.stats.attack=static_cast<int>(u.stats.attack*1.15f);u.stats.crit+=0.06f;}
    }
    if(u.bossIndex>=0){
        u.stats.defense+=level*2;u.stats.attack=static_cast<int>(u.stats.attack*1.50f);
        u.stats.maxHp=BossHealth(u.stats,options.raidSize,options.bossDuration,options.bossHpRate);
    }
    if(u.native&&(!options.rpg||!options.nativeScaling))u.stats.maxHp=(std::max)(1,u.nativeMaxHp);
    u.hp=ratio*u.stats.maxHp;u.displayHp=u.hp;
    u.scaledForLevel=CurrentPlayerStats().level;u.scaledForRevision=scalingRevision;
}
void AddUnit(Actor* a,bool bot){
    Unit u;u.actor=a;u.serial=nextSerial++;u.seed=u.serial*0x45D9F3BU+frame+1;u.bot=bot;
    u.home=u.goal=a->world.pos;u.raid=bot&&(a->params&0x100);u.squad=u.raid?((a->params>>5)&7):-1;
    u.bossIndex=!bot&&(a->params&0x100)?(a->params&7):-1;
    u.profile=bot?0:Clamp(a->params&7,0,4);
    u.role=static_cast<Role>(bot?(a->params&0x1F)%3:0);
    u.color=NextRandom(u.seed)%24;
    u.berserk=!bot&&u.bossIndex<0&&static_cast<int>(NextRandom(u.seed)%100)<options.berserkChance;
    if(bot){
        u.name=botNames[(u.serial+unsigned((std::max)(0,activeScene))*7)%64];
        u.name+="#"+std::to_string(u.serial%10000);
        if(u.raid)u.name+=" [Raid]";
    }else u.name=u.bossIndex>=0?BossName(u.bossIndex):ProfileName(u.profile);
    u.radius=bot?18.0f:(u.profile==2?30.0f:u.profile==4?40.0f:24.0f);
    u.height=bot?(LINK_IS_ADULT?68.0f:46.0f):(u.profile==2?115.0f:u.profile==4?55.0f:75.0f);
    // The models have different native unit scales, not a single scale copied onto every skeleton.
    static const float scales[5]={0.015f,0.015f,0.01f,0.0075f,0.01875f};
    u.modelScale=bot?0.01f:scales[u.profile];
    if(u.bossIndex>=0){u.radius*=3.4f;u.height*=3.4f;u.modelScale*=3.4f;
        if(bossHomeValid[u.bossIndex])u.home=bossHomes[u.bossIndex];}
    u.hp=static_cast<float>(u.stats.maxHp);Rescale(u);
    u.thinkTimer=static_cast<int>(NextRandom(u.seed)%15);u.cooldown=20;
    units.emplace(a,std::move(u));
}
void RemoveUnit(Actor* a){
    auto it=units.find(a);
    if(it!=units.end()){
        const Unit& u=it->second;
        if(!tearingDown&&!u.native&&u.action==Action::Dead&&u.bossIndex<0&&!u.raid){
            if(u.bot)botRespawns.push_back(frame+15*SimHz);
            else enemyRespawns.push_back(frame+options.respawnSeconds*SimHz);
        }
        units.erase(it);
    }
    hits.erase(a);
    // Projectiles can be destroyed before their victim updates. Keep the
    // collision's damage and position, while dropping the invalid pointer.
    for(auto& [_,hit]:hits)if(hit.source==a)hit.source=nullptr;
    for(auto& [_,u]:units)if(u.target==a){u.target=nullptr;u.targetSerial=0;u.path.clear();}
}
void Award(Unit& u){
    if(u.awarded||u.bot)return;u.awarded=true;
    if(u.native){u.hp=0;u.action=Action::Dead;}
    if(u.bossIndex>=0){
        gSaveContext.ship.nxt.bossCooldown[u.bossIndex]=options.bossRespawnMinutes*60;
        for(auto& [_,ally]:units)if(ally.raid&&ally.squad==u.bossIndex){ally.action=Action::Depart;ally.actionTimer=80;ally.target=nullptr;}
    }
    if(!options.enabled||!options.rpg||!gPlayState||gSaveContext.health<=0)return;
    Actor* player=&GET_PLAYER(gPlayState)->actor;
    float distance=DistanceXZ(player->world.pos,u.actor->world.pos);
    bool tagged=frame-u.lastPlayerTag<45*SimHz;
    float share=tagged&&distance<4000?1.0f:(options.sharing&&distance<650?0.35f:0.0f);
    if(share<=0)return;
    uint32_t reward=KillXp(u.stats.level,CurrentPlayerStats().level,u.berserk,u.bossIndex>=0,share,options.xpRate);
    auto& save=gSaveContext.ship.nxt;
    int levels=GrantXp(save.level,save.xp,reward);
    if(save.kills<(std::numeric_limits<uint32_t>::max)())++save.kills;
    if(u.bossIndex>=0&&save.bossKills<(std::numeric_limits<uint32_t>::max)())++save.bossKills;
    AddText(player->world.pos,"+"+std::to_string(reward)+" XP",0xFFFFD180);
    if(levels){
        gSaveContext.health=gSaveContext.healthCapacity;
        AddText(player->world.pos,"LEVEL "+std::to_string(save.level),0xFF88FFCC);
    }
}
void HitUnit(Unit& u,Actor* source,int baseDamage,float power){
    if(!Alive(u)||u.hurtTimer>0)return;
    source=ResolveSource(source);
    bool player=gPlayState&&source==&GET_PLAYER(gPlayState)->actor;
    Unit* attacker=Find(source);
    if(attacker&&attacker->bot==u.bot)return;
    if(u.bot&&player)return; // No accidental friendly fire.
    if(player){u.lastPlayerTag=frame;lastHurt=frame;}
    if(baseDamage<=0){u.hurtTimer=12;return;}
    Stats attack=attacker?attacker->stats:(player?CurrentPlayerStats():EnemyStats(CurrentPlayerStats().level,u.seed));
    const bool nativeContact=u.bot&&(!attacker||attacker->native);
    float factor=player?Clamp(0.6f+baseDamage*0.25f,0.65f,3.0f):
        nativeContact?power*Clamp(baseDamage/4.0f,0.25f,4.0f):power;
    if(Random01(u.seed)<DodgeChance(u.stats,attack)){
        u.hurtTimer=3;AddText(u.actor->world.pos,"EVADE",0xFFE1DCA0);return;
    }
    bool critical=Random01(u.seed)<attack.crit;
    int dealt=Damage(attack.attack,u.stats.defense,factor,critical,0.90f+Random01(u.seed)*0.20f);
    u.hp=(std::max)(0.0f,u.hp-dealt);u.hurtTimer=u.bossIndex>=0?2:6;u.combatTimer=12*SimHz;
    if(u.bot){
        // Independent, collision-safe recoil. Never use Actor_SetPlayerKnockback:
        // that API modifies the real Link even when passed an adventurer actor.
        Vec3f origin=u.actor->world.pos;
        auto hit=hits.find(u.actor);
        if(hit!=hits.end()&&frame-hit->second.tick<8&&hit->second.source==source)origin=hit->second.origin;
        else if(source)origin=source->world.pos;
        float dx=u.actor->world.pos.x-origin.x,dz=u.actor->world.pos.z-origin.z;
        float length=std::sqrt(dx*dx+dz*dz);
        if(length>0.01f){
            float force=nativeContact?Clamp(baseDamage*0.75f,2.0f,6.0f):3.0f;
            u.knockbackVelocity={dx/length*force,0,dz/length*force};u.knockbackTimer=6;
        }
    }
    if(attacker){attacker->combatTimer=12*SimHz;if(!u.bot&&attacker->bot){u.target=source;u.targetSerial=attacker->serial;}}
    if(player&&!u.bot){u.target=source;u.targetSerial=0;u.threat+=dealt*1.5f;}
    AddText(u.actor->world.pos,std::to_string(dealt)+(critical?"!":""),u.bot?0xFF8AC9FF:critical?0xFF68DAFF:0xFFEAEAEA);
    if(u.hp<=0){u.action=Action::Dead;u.actionTimer=30;u.target=nullptr;u.actor->flags&=~ACTOR_FLAG_ATTENTION_ENABLED;Award(u);}
}
void DamagePlayer(PlayState* play,Unit& source,float power){
    Player* player=GET_PLAYER(play);
    if(!Running(play)||!play->damagePlayer||player->invincibilityTimer!=0||source.hp<=0)return;
    int16_t yaw=DirectionYaw(source.actor->world.pos.x-player->actor.world.pos.x,source.actor->world.pos.z-player->actor.world.pos.z);
    int16_t delta=static_cast<int16_t>(yaw-player->actor.shape.rot.y);
    if((player->stateFlags1&PLAYER_STATE1_SHIELDING)&&std::abs(static_cast<int>(delta))<0x3000)power*=0.20f;
    if(Random01(source.seed)<DodgeChance(CurrentPlayerStats(),source.stats)){
        AddText(player->actor.world.pos,"EVADE",0xFFE1DCA0);lastHurt=frame;return;
    }
    int damage=Damage(source.stats.attack,CurrentPlayerStats().defense,power*options.damageRate);
    int native=HealthUnits(damage,CurrentPlayerStats().maxHp,gSaveContext.healthCapacity);
    skipDamageConversion=true;
    play->damagePlayer(play,-native);
    skipDamageConversion=false;
    // damagePlayer handles health/death; invulnerability prevents an entire raid hitting on one tick.
    if(gSaveContext.health>0)player->invincibilityTimer=20;
    lastHurt=frame;regenBank=0;
    AddText(player->actor.world.pos,"-"+std::to_string(damage),0xFF8585FF);
}
static bool NativeEligible(Actor* a){
    if(!a||a->category!=ACTORCAT_ENEMY||a->colChkInfo.health==0||a->id==ACTOR_EN_SW)return false;
    return a->id!=ACTOR_EN_NY&&a->id!=ACTOR_EN_ANUBICE;
}
void TrackNative(Actor* a){
    if(!options.enabled||!gPlayState||!SceneEnabled(gPlayState->sceneNum)||!NativeEligible(a)||Find(a))return;
    Unit u;u.actor=a;u.native=true;u.serial=nextSerial++;u.seed=u.serial*137+3;
    u.nativeMaxHp=u.nativeLastHealth=a->colChkInfo.health;u.name="Hyrule foe";u.home=a->world.pos;
    switch(a->id){case ACTOR_EN_TEST:u.name="Stalfos";break;case ACTOR_EN_ZF:u.name="Lizalfos";break;
        case ACTOR_EN_MB:u.name="Moblin";break;case ACTOR_EN_DODONGO:u.name="Dodongo";break;
        case ACTOR_EN_WF:u.name="Wolfos";break;case ACTOR_EN_RD:u.name="ReDead";break;
        case ACTOR_EN_TITE:u.name="Tektite";break;case ACTOR_EN_FIREFLY:u.name="Keese";break;
        case ACTOR_EN_DEKUBABA:u.name="Deku Baba";break;default:break;}
    u.hp=static_cast<float>(u.stats.maxHp);Rescale(u);units.emplace(a,std::move(u));
}
static void ClearWorld(){
    tearingDown=true;
    for(auto& [a,u]:units)if(!u.native&&a->update)Actor_Kill(a);
    units.clear();hits.clear();navigation.Reset();combatText.clear();
    enemyRespawns.clear();botRespawns.clear();bossHomeValid.fill(false);
    for(auto& b:bossViews)b={};
    tearingDown=false;regenBank=0;sceneAge=0;
}
static bool Spawn(PlayState* play,bool bot,Vec3f point,int params){
    if(units.size()>=MaxPopulation)return false;
    // The legacy total field is only u8; category lengths retain the full scene count.
    int actorCount=0;
    for(const auto& list:play->actorCtx.actorLists)actorCount+=list.length;
    if(actorCount>=ACTOR_NUMBER_MAX-80)return false;
    const int id=bot?botActorId:enemyActorId;
    return Actor_Spawn(&play->actorCtx,play,id,point.x,point.y,point.z,0,
                       static_cast<int16_t>(NextRandom(worldSeed)),0,static_cast<int16_t>(params))!=nullptr;
}
static int ProfileForScene(int scene){
    if(scene==SCENE_DEATH_MOUNTAIN_TRAIL||scene==SCENE_DEATH_MOUNTAIN_CRATER||scene==SCENE_DODONGOS_CAVERN)return 4;
    if(scene==SCENE_SACRED_FOREST_MEADOW)return NextRandom(worldSeed)%2?2:3;
    if(scene==SCENE_SHADOW_TEMPLE||scene==SCENE_BOTTOM_OF_THE_WELL||scene==SCENE_ROYAL_FAMILYS_TOMB)return 0;
    if(scene==SCENE_LOST_WOODS||scene==SCENE_FOREST_TEMPLE)return NextRandom(worldSeed)%2?1:3;
    return NextRandom(worldSeed)%5;
}
static void UpdateBosses(PlayState* play){
    if(play->sceneNum!=SCENE_HYRULE_FIELD||!options.bosses||!navigation.Ready())return;
    Actor* player=&GET_PLAYER(play)->actor;
    for(int index=0;index<BossCount;++index){
        Unit* boss=nullptr;int raiders=0;
        for(auto& [_,u]:units){if(u.bossIndex==index&&Alive(u))boss=&u;if(u.raid&&u.squad==index&&Alive(u))++raiders;}
        auto& view=bossViews[index];view.name=BossName(index);view.cooldown=gSaveContext.ship.nxt.bossCooldown[index];
        view.alive=boss!=nullptr;view.raiders=raiders;
        if(boss){view.hp=boss->hp;view.maxHp=static_cast<float>(boss->stats.maxHp);view.level=boss->stats.level;
            view.engaged=boss->combatTimer>0;
            if(DistanceXZ(player->world.pos,boss->home)<1700&&raiders<options.raidSize&&
               LivingCount(true,true)<options.raidCap&&(frame%8)==index%8){
                Vec3f point;
                if(navigation.SpawnPoint(play,worldSeed,point,false,&boss->home))
                    Spawn(play,true,point,0x100|(index<<5)|(raiders%24));
            }
            if(DistanceXZ(player->world.pos,boss->home)>4500){
                for(auto& [_,u]:units)if(u.raid&&u.squad==index&&u.action!=Action::Depart){
                    u.action=Action::Depart;u.actionTimer=60;u.target=nullptr;
                }
            }
            continue;
        }
        view.engaged=false;view.hp=0;
        if(view.cooldown>0||frame%10!=index*2)continue;
        if(!bossHomeValid[index]){
            Vec3f best{};bool valid=false;float bestScore=-1;
            for(int attempt=0;attempt<12;++attempt){
                Vec3f p;if(!navigation.SpawnPoint(play,worldSeed,p,true))continue;
                float separation=100000;
                for(int j=0;j<BossCount;++j)if(j!=index&&bossHomeValid[j])separation=(std::min)(separation,DistanceXZ(p,bossHomes[j]));
                if(separation<1400)continue;
                if(separation>bestScore){bestScore=separation;best=p;valid=true;}
            }
            if(!valid)continue;
            bossHomes[index]=best;bossHomeValid[index]=true;
        }
        // Never recreate an actor in the same spot while its death animation is still running.
        bool dying=false;for(auto& [_,u]:units)if(u.bossIndex==index)dying=true;
        if(!dying)Spawn(play,false,bossHomes[index],0x100|index);
    }
}
static void UpdatePopulation(PlayState* play){
    if(!navigation.Ready())return;
    while(!enemyRespawns.empty()&&enemyRespawns.front()<=frame)enemyRespawns.pop_front();
    while(!botRespawns.empty()&&botRespawns.front()<=frame)botRespawns.pop_front();
    int countBots=LivingCount(true),countEnemies=LivingCount(false);
    int wantedBots=DesiredBots(play->sceneNum),wantedEnemies=DesiredEnemies(play->sceneNum);
    int raidTotal=LivingCount(true,true);
    std::array<int,BossCount> perBoss{};
    for(const auto& [a,u]:units)if(u.raid&&Alive(u)&&u.squad>=0&&u.squad<BossCount)++perBoss[u.squad];
    for(auto& [a,u]:units){
        if(!Alive(u)||u.native)continue;
        if(u.bossIndex>=0&&!options.bosses){u.action=Action::Depart;u.actionTimer=20;u.target=nullptr;}
        if(u.raid&&(!options.bosses||raidTotal>options.raidCap||u.squad<0||u.squad>=BossCount||
                   perBoss[u.squad]>options.raidSize)){
            u.action=Action::Depart;u.actionTimer=30;u.target=nullptr;--raidTotal;
            if(u.squad>=0&&u.squad<BossCount)--perBoss[u.squad];
        }
    }
    if(!options.bosses)for(auto& boss:bossViews)boss={};
    // Live reductions retire excess actors; no story actor or regular bot is commandeered for a raid.
    for(auto& [a,u]:units){
        if(u.native||u.raid||u.bossIndex>=0||!Alive(u))continue;
        if((u.bot&&countBots>wantedBots)||(!u.bot&&countEnemies>wantedEnemies)){
            u.action=Action::Depart;u.actionTimer=15;if(u.bot)--countBots;else --countEnemies;
        }
    }
    if(frame%3==0){
        Vec3f point;
        if(countBots+static_cast<int>(botRespawns.size())<wantedBots&&navigation.SpawnPoint(play,worldSeed,point))
            Spawn(play,true,point,static_cast<int>(NextRandom(worldSeed)%64));
        if(countEnemies+static_cast<int>(enemyRespawns.size())<wantedEnemies&&navigation.SpawnPoint(play,worldSeed,point))
            Spawn(play,false,point,ProfileForScene(play->sceneNum));
    }
    UpdateBosses(play);
}
void Tick(){
    ReadOptions();
    PlayState* play=gPlayState;
    if(!play)return;
    if(!options.enabled){if(!units.empty())ClearWorld();activeScene=-1;return;}
    if(!Running(play))return;
    ++frame;
    if(activeScene!=play->sceneNum||activeRoom!=play->roomCtx.curRoom.num||repopulate){
        ClearWorld();activeScene=play->sceneNum;activeRoom=play->roomCtx.curRoom.num;repopulate=false;
        navigation.Begin(play);
        for(int category=0;category<ACTORCAT_MAX;++category)
            for(Actor* a=play->actorCtx.actorLists[category].head;a;a=a->next)TrackNative(a);
    }
    ++sceneAge;
    navigation.Step(play);
    if(navigation.Ready()&&frame%120==0)navigation.RefreshReachable(play);
    status=navigation.Ready()?std::to_string(navigation.ReachableCount())+" connected navigation nodes":
        "Surveying safe terrain: "+std::to_string(navigation.nodes.size())+" nodes";
    UpdatePopulation(play);
    if(frame%SimHz==0)for(auto& seconds:gSaveContext.ship.nxt.bossCooldown)if(seconds>0)--seconds;
    int playerLevel=CurrentPlayerStats().level;
    lastLevel=playerLevel;
    for(auto& [a,u]:units){
        // Defer rescaling until combat ends, including actors that were fighting at level-up.
        if(Alive(u)&&u.combatTimer==0&&(u.scaledForLevel!=playerLevel||u.scaledForRevision!=scalingRevision))Rescale(u);
        u.displayHp+=(u.hp-u.displayHp)*0.20f;
        if(u.native){
            const int nativeHealth=a->colChkInfo.health;
            // Some story encounters resurrect their original actors in place.
            // Detect a real zero-to-positive transition rather than treating dying animations as alive.
            if(nativeHealth>0&&u.nativeLastHealth==0&&u.hp<=0){
                u.nativeMaxHp=nativeHealth;u.awarded=false;u.action=Action::Roam;
                u.hp=static_cast<float>(u.stats.maxHp);Rescale(u);u.hurtTimer=u.combatTimer=0;
            }
            if(nativeHealth==0){u.hp=0;u.action=Action::Dead;}
            u.nativeLastHealth=nativeHealth;
            if(u.hurtTimer>0)--u.hurtTimer;
            if(u.combatTimer>0)--u.combatTimer;
            if((!options.nativeScaling||!options.rpg)&&u.nativeMaxHp>0)u.hp=a->colChkInfo.health;
            if(frame%20==0){u.height=(std::max)(50.0f,a->focus.pos.y-a->world.pos.y);}
        }
        if(frame%8==static_cast<int>(u.serial%8)&&DistanceXZ(a->world.pos,GET_PLAYER(play)->actor.world.pos)<options.plateDistance){
            Vec3f head=a->world.pos;head.y+=u.height;
            u.visible=navigation.Clear(play,play->mainCamera.eye,head,0);
        }
    }
    if(options.regen&&gSaveContext.health>0&&gSaveContext.health<gSaveContext.healthCapacity&&
       frame-lastHurt>=options.regenDelay*SimHz){
        regenBank+=gSaveContext.healthCapacity*(options.regenPercent/100.0f)*RecoveryBonus(CurrentPlayerStats())/SimHz;
        int amount=static_cast<int>(regenBank);
        if(amount>0){gSaveContext.health=(std::min)(int(gSaveContext.healthCapacity),int(gSaveContext.health)+amount);regenBank-=amount;}
    }else regenBank=0;
    for(auto& text:combatText){--text.life;text.pos.y+=0.65f;}
    combatText.erase(std::remove_if(combatText.begin(),combatText.end(),[](const CombatText& t){return t.life<=0;}),combatText.end());
}
void OnHealthChange(int16_t amount) {
    if(amount<0){lastHurt=frame;regenBank=0;}
}
void OnGameLoaded() {
    activeScene=-1;lastHurt=frame;lastLevel=CurrentPlayerStats().level;
}
void OnWorldDestroyed() {
    // Engine owns actor destruction; never walk old pointers during teardown.
    units.clear();hits.clear();navigation.Reset();combatText.clear();activeScene=-1;activeRoom=-128;
    enemyRespawns.clear();botRespawns.clear();bossHomeValid.fill(false);
    for(auto& boss:bossViews)boss={};
}
} // namespace Nxt

extern "C" void Nxt_RecordCollision(Actor* attacker,Actor* defender,uint8_t touchDamage){
    if(!Nxt::options.enabled||!attacker||!defender)return;
    if(Nxt::Find(defender)|| (gPlayState&&defender==&GET_PLAYER(gPlayState)->actor))
        Nxt::hits[defender]={Nxt::ResolveSource(attacker),Nxt::frame,touchDamage,attacker->world.pos};
}
extern "C" int Nxt_IsBotActor(const Actor* actor){
    // Classify by the registered actor ID, not settings or transient health.
    // A collider from the last active frame still needs isolation after an
    // option toggle or a death, until the engine resets its collision lists.
    return actor&&Nxt::botActorId>=0&&actor->id==Nxt::botActorId;
}
extern "C" int Nxt_ApplyNativeDamage(Actor* actor){
    using namespace Nxt;
    Unit* u=Find(actor);
    if(!options.enabled||!options.rpg||!options.nativeScaling||!gPlayState||!SceneEnabled(gPlayState->sceneNum)||!u||!u->native)return -1;
    if(actor->colChkInfo.damage==0)return actor->colChkInfo.health;
    Actor* source=nullptr;auto hit=hits.find(actor);
    if(hit!=hits.end()&&frame-hit->second.tick<8)source=hit->second.source;
    if(hit==hits.end()&&gPlayState&&DistanceXZ(actor->world.pos,GET_PLAYER(gPlayState)->actor.world.pos)<160)
        source=&GET_PLAYER(gPlayState)->actor;
    u->hurtTimer=0;
    HitUnit(*u,source,actor->colChkInfo.damage);
    int proxy=u->hp<=0?0:Clamp(static_cast<int>(std::ceil(u->hp/u->stats.maxHp*u->nativeMaxHp)),1,u->nativeMaxHp);
    actor->colChkInfo.health=static_cast<uint8_t>(proxy);
    u->nativeLastHealth=proxy;
    return proxy;
}
extern "C" int16_t Nxt_AdjustPlayerDamage(PlayState* play,int16_t amount){
    using namespace Nxt;
    if(amount>=0||!options.enabled||!options.rpg||!play||!SceneEnabled(play->sceneNum)||skipDamageConversion)return amount;
    // Lethal scripted hazards retain their vanilla semantics (void/crush/event damage).
    if(-int(amount)>=gSaveContext.healthCapacity)return amount;
    Unit* attacker=nullptr;auto hit=hits.find(&GET_PLAYER(play)->actor);
    if(hit!=hits.end()&&frame-hit->second.tick<8)attacker=Find(hit->second.source);
    if(!attacker||(attacker->native&&!options.nativeScaling))return amount;
    Stats stats=attacker->stats;
    float power=Clamp(-amount/8.0f,0.12f,4.0f);
    int damage=Damage(stats.attack,CurrentPlayerStats().defense,power*options.damageRate);
    return static_cast<int16_t>(-HealthUnits(damage,CurrentPlayerStats().maxHp,gSaveContext.healthCapacity));
}
