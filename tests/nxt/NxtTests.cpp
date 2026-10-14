// Headless tests compile the real simulation against the uploaded engine headers.
// The engine boundary below deliberately uses a synthetic flat collision world.
// These tests do NOT validate the renderer, ROM assets, real scenes, or gameplay timing.
#include "soh/Enhancements/NxtRpg/NxtConfig.h"
#include "soh/Enhancements/NxtRpg/NxtNavigation.h"
#include "soh/Enhancements/NxtRpg/NxtBridge.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <functional>
#include <set>
#include <map>
#include <cstring>
#include <climits>

extern "C" { PlayState* gPlayState=nullptr; SaveContext gSaveContext{}; }
namespace Fixture {
std::unique_ptr<PlayState> play;
std::unique_ptr<Player> player;
std::vector<std::unique_ptr<Actor>> actors;
std::vector<Vec3s> vertices;
std::vector<CollisionPoly> polys;
CollisionHeader header{};
CollisionPoly floorPoly{};
std::map<std::string,int> settings;
std::map<std::string,float> reals;
bool wet=false,voidFloor=false,wall=false,lowCeiling=false,cutscene=false;
bool pit=false,stepFloor=false,blockedMotion=false,reverseWallCorrection=false;
u32 property=0,exitIndex=0,effect=0,wallDamage=0;
int assertions=0,groups=0;
void Check(bool value,const char* expression,int line){++assertions;if(!value)throw std::runtime_error(std::string(expression)+" at line "+std::to_string(line));}
#define CHECK(expr) Fixture::Check(bool(expr),#expr,__LINE__)
void NoUpdate(Actor*,PlayState*){}
void Reset(int scene=SCENE_HYRULE_FIELD){
 Nxt::OnWorldDestroyed();actors.clear();settings.clear();reals.clear();
 wet=voidFloor=wall=lowCeiling=cutscene=false;pit=stepFloor=blockedMotion=reverseWallCorrection=false;
 property=exitIndex=effect=wallDamage=0;
 play=std::make_unique<PlayState>();player=std::make_unique<Player>();gPlayState=play.get();
 std::memset(&gSaveContext,0,sizeof(gSaveContext));gSaveContext.healthCapacity=48;gSaveContext.health=48;
 gSaveContext.ship.nxt.level=1;gSaveContext.gameMode=GAMEMODE_NORMAL;
 play->state.running=true;play->sceneNum=scene;play->actorCtx.actorLists[ACTORCAT_PLAYER].head=&player->actor;
 player->actor.update=NoUpdate;player->actor.id=ACTOR_PLAYER;player->actor.world.pos={-300,1,-300};
 play->damagePlayer=[](PlayState*,s32 damage)->s32 {gSaveContext.health=Nxt::Clamp(int(gSaveContext.health)+int(damage),0,int(gSaveContext.healthCapacity));Nxt::OnHealthChange(static_cast<int16_t>(damage));return 1;};
 vertices.clear();polys.clear();
 constexpr int size=20;constexpr int spacing=500;
 for(int z=0;z<=size;++z)for(int x=0;x<=size;++x)vertices.push_back({static_cast<s16>((x-size/2)*spacing),0,static_cast<s16>((z-size/2)*spacing)});
 for(int z=0;z<size;++z)for(int x=0;x<size;++x){
  u16 a=z*(size+1)+x,b=a+1,c=a+size+1,d=c+1;
  CollisionPoly p{};p.normal.y=32767;p.vtxData[0]=a;p.vtxData[1]=c;p.vtxData[2]=b;polys.push_back(p);
  p.vtxData[0]=b;p.vtxData[1]=c;p.vtxData[2]=d;polys.push_back(p);
 }
 header={};header.minBounds={-5000,0,-5000};header.maxBounds={5000,0,5000};
 header.numVertices=static_cast<u16>(vertices.size());header.vtxList=vertices.data();
 header.numPolygons=static_cast<u16>(polys.size());header.polyList=polys.data();
 floorPoly={};floorPoly.normal.y=32767;play->colCtx.colHeader=&header;
 Nxt::botActorId=700;Nxt::enemyActorId=701;Nxt::ReadOptions();Nxt::OnGameLoaded();
}
void Configure(const char* name,int value){settings[std::string("gNxt.")+name]=value;Nxt::ReadOptions();}
Actor* New(bool bot,Vec3f pos,int params=0){return Actor_Spawn(&play->actorCtx,play.get(),bot?Nxt::botActorId:Nxt::enemyActorId,pos.x,pos.y,pos.z,0,0,0,static_cast<s16>(params));}
void Run(int frames,bool ai=true){
 for(int i=0;i<frames;++i){Nxt::Tick();if(player->invincibilityTimer>0)--player->invincibilityTimer;
  if(ai){std::vector<Actor*> snapshot;for(auto& [a,u]:Nxt::units)if(!u.native&&a->update)snapshot.push_back(a);
   for(auto* a:snapshot)if(auto* u=Nxt::Find(a);u&&a->update)Nxt::UpdateUnit(*u,play.get());}
  // Engine destruction is a later pass, matching Actor_Kill's deferred semantics.
  for(auto& a:actors)if(a&&!a->update){Nxt::RemoveUnit(a.get());a.reset();--play->actorCtx.total;}
 }
}
void Group(const char* title,const std::function<void()>& test){test();++groups;std::cout<<"PASS "<<title<<'\n';}
}
namespace Nxt {
int ConfigInt(const char* key,int fallback){auto i=Fixture::settings.find(key);return i==Fixture::settings.end()?fallback:i->second;}
float ConfigFloat(const char* key,float fallback){auto i=Fixture::reals.find(key);return i==Fixture::reals.end()?fallback:i->second;}
}
// Link-time fixtures, not replacement engine headers. Signatures are checked by the real headers.
extern "C" {
void Actor_Kill(Actor* a){a->update=nullptr;}
Actor* Actor_Spawn(ActorContext* ctx,PlayState*,s16 id,f32 x,f32 y,f32 z,s16 rx,s16 ry,s16 rz,s16 params){
 auto a=std::make_unique<Actor>();a->id=id;a->params=params;a->world.pos={x,y,z};a->world.rot={rx,ry,rz};a->shape.rot={rx,ry,rz};a->update=Fixture::NoUpdate;
 Actor* result=a.get();Fixture::actors.push_back(std::move(a));++ctx->total;Nxt::AddUnit(result,id==Nxt::botActorId);return result;
}
s32 Player_InCsMode(PlayState*){return Fixture::cutscene;}
f32 BgCheck_EntityRaycastFloor3(CollisionContext*,CollisionPoly** poly,s32* bg,Vec3f* p){
 *bg=BGCHECK_SCENE;if(Fixture::voidFloor||std::fabs(p->x)>5000||std::fabs(p->z)>5000||
   (Fixture::pit&&std::fabs(p->x)<100&&std::fabs(p->z)<350)){*poly=nullptr;return BGCHECK_Y_MIN;}
 float floor=Fixture::stepFloor&&p->x>=0?36.0f:0.0f;
 if(floor>p->y){*poly=nullptr;return BGCHECK_Y_MIN;}
 *poly=&Fixture::floorPoly;return floor;
}
s32 BgCheck_EntityLineTest1(CollisionContext*,Vec3f* a,Vec3f* b,Vec3f* hit,CollisionPoly** poly,s32,s32,s32,s32,s32*){
 if(Fixture::lowCeiling&&((a->y<80&&b->y>=80)||(b->y<80&&a->y>=80))){*hit=*b;*poly=&Fixture::floorPoly;return 1;}
 if(Fixture::wall&&((a->x<0&&b->x>=0)||(b->x<0&&a->x>=0))){
  float t=-a->x/(b->x-a->x);float z=a->z+(b->z-a->z)*t;
  if(std::fabs(z)>300){*hit=*b;*poly=&Fixture::floorPoly;return 1;}
 }
 return 0;
}
s32 BgCheck_EntitySphVsWall3(CollisionContext*,Vec3f* result,Vec3f* next,Vec3f* previous,f32,CollisionPoly**,s32*,Actor*,f32){
 if(Fixture::blockedMotion){*result=*previous;return 1;}
 if(Fixture::reverseWallCorrection){*result={2*previous->x-next->x,next->y,2*previous->z-next->z};return 1;}
 *result=*next;return 0;
}
u32 SurfaceType_GetSceneExitIndex(CollisionContext*,CollisionPoly*,s32){return Fixture::exitIndex;}
u32 func_80041EA4(CollisionContext*,CollisionPoly*,s32){return Fixture::property;}
u32 SurfaceType_GetFloorEffect(CollisionContext*,CollisionPoly*,s32){return Fixture::effect;}
s32 SurfaceType_IsIgnoredByEntities(CollisionContext*,CollisionPoly*,s32){return 0;}
u32 SurfaceType_IsWallDamage(CollisionContext*,CollisionPoly*,s32){return Fixture::wallDamage;}
s32 WaterBox_GetSurface1(PlayState*,CollisionContext*,f32,f32,f32* y,WaterBox**){*y=50;return Fixture::wet;}
s16 Math_SmoothStepToS(s16* v,s16 target,s16 scale,s16 step,s16 minStep){
 const int diff=static_cast<s16>(target-*v);int amount=diff/(std::max)(1,int(scale));amount=Nxt::Clamp(amount,-int(step),int(step));
 if(std::abs(amount)<minStep)amount=diff<0?-minStep:minStep;if(std::abs(amount)>std::abs(diff))amount=diff;*v=static_cast<s16>(*v+amount);return static_cast<s16>(diff);
}
}
int main(){
 using namespace Fixture;using namespace Nxt;
 try{
 Group("level progression, caps, bounded combat and deterministic RNG",[]{
  CHECK(XpRequired(1)==154);uint32_t level=1,xp=0;CHECK(GrantXp(level,xp,154)==1);CHECK(level==2&&xp==0);
  CHECK(GrantXp(level,xp,UINT32_MAX)==98);CHECK(level==100&&xp==0);CHECK(GrantXp(level,xp,999)==0);
  uint32_t a=19,b=19;for(int i=0;i<10000;++i){CHECK(NextRandom(a)==NextRandom(b));CHECK(Random01(a)>=0);Random01(b);}
  a=42;for(int i=0;i<100000;++i){int l=1+NextRandom(a)%100;Stats s=EnemyStats(l,a,i%5==0);
   CHECK(s.maxHp>0&&s.attack>0&&s.defense>=0);CHECK(VariedLevel(l,10,a)>=1);CHECK(VariedLevel(l,10,a)<=100);
   int d=Damage(s.attack,s.defense,1.5f,true);CHECK(d>0&&d<=1000000);CHECK(HealthUnits(d,s.maxHp,48)>=1&&HealthUnits(d,s.maxHp,48)<=48);
  }
  CHECK(Damage(100,100)<Damage(100,0));CHECK(Damage(100,0,1,true)>Damage(100,0));
  CHECK(KillXp(10,10,true,false)>KillXp(10,10,false,false));CHECK(BossHealth(PlayerStats(20),12,300,1)>1500);
 });
 Group("binary-angle wrapping at positive and negative half-turn",[]{
  CHECK(DirectionYaw(0.0f,1.0f)==0);
  CHECK(DirectionYaw(1.0f,0.0f)==16384);CHECK(DirectionYaw(-1.0f,0.0f)==-16384);
  CHECK(DirectionYaw(0.0f,-1.0f)==-32768);CHECK(DirectionYaw(-0.0f,-1.0f)==-32768);
  CHECK(DirectionYaw(1.0f,1.0f)==8192);CHECK(DirectionYaw(-1.0f,1.0f)==-8192);
  CHECK(DirectionYaw(1.0f,-1.0f)==24576);CHECK(DirectionYaw(-1.0f,-1.0f)==-24576);
  CHECK(DirectionYaw(std::numeric_limits<float>::quiet_NaN(),1.0f)==0);
  CHECK(DirectionYaw(1.0f,std::numeric_limits<float>::infinity())==0);
 });
 Group("scene safety and defensive settings",[]{
  Reset();CHECK(!CombatScene(SCENE_KAKARIKO_VILLAGE));CHECK(!CombatScene(SCENE_MARKET_DAY));CHECK(!CombatScene(SCENE_LINKS_HOUSE));
  CHECK(CombatScene(SCENE_HYRULE_FIELD));CHECK(CombatScene(SCENE_FOREST_TEMPLE));CHECK(!SceneEnabled(SCENE_DEKU_TREE_BOSS));
  CHECK(DesiredBots(SCENE_HYRULE_FIELD)==30);CHECK(DesiredBots(SCENE_LINKS_HOUSE)==10);CHECK(DesiredEnemies(SCENE_HYRULE_FIELD)==60);
  Configure("BotCount",1000);CHECK(options.botCount==50);Configure("BerserkChance",-3);CHECK(options.berserkChance==0);
  reals["gNxt.XpRate"]=std::numeric_limits<float>::quiet_NaN();ReadOptions();CHECK(options.xpRate==1);
  settings["gNxt.Scene."+std::to_string(SCENE_HYRULE_FIELD)+".Bots"]=17;CHECK(DesiredBots(SCENE_HYRULE_FIELD)==17);
 });
 Group("ground, water, lethal floor, exit and ceiling rejection",[]{
  Reset();Vec3f p{0,1,0};CHECK(navigation.Ground(play.get(),p));wet=true;CHECK(!navigation.Ground(play.get(),p));wet=false;
  property=FLOOR_PROPERTY_5;CHECK(!navigation.Ground(play.get(),p));property=FLOOR_PROPERTY_12;CHECK(!navigation.Ground(play.get(),p));property=0;
  exitIndex=1;CHECK(!navigation.Ground(play.get(),p));exitIndex=0;effect=FLOOR_EFFECT_2;CHECK(!navigation.Ground(play.get(),p));effect=0;
  wallDamage=1;CHECK(!navigation.Ground(play.get(),p));wallDamage=0;voidFloor=true;CHECK(!navigation.Ground(play.get(),p));voidFloor=false;
  floorPoly.normal.y=10000;CHECK(!navigation.Ground(play.get(),p));floorPoly.normal.y=32767;
  wall=true;CHECK(!navigation.Clear(play.get(),{-200,1,1000},{200,1,1000}));CHECK(navigation.Clear(play.get(),{-200,1,0},{200,1,0}));
 });
 Group("bounded field navigation, connectivity, routing and distributed spawns",[]{
  Reset();navigation.Begin(play.get());for(int i=0;i<200&&!navigation.Ready();++i)navigation.Step(play.get());
  CHECK(navigation.Ready());CHECK(navigation.nodes.size()<=640);CHECK(navigation.ReachableCount()>300);
  int source=navigation.Nearest({-4000,1,-4000},true),target=navigation.Nearest({4000,1,4000},true);CHECK(!navigation.Route(source,target).empty());
  std::set<int> quadrants;uint32_t seed=5;for(int i=0;i<50;++i){Vec3f p{};CHECK(navigation.SpawnPoint(play.get(),seed,p));quadrants.insert((p.x>0?1:0)+(p.z>0?2:0));New(true,p);}
  CHECK(quadrants.size()==4);CHECK(LivingCount(true)==50);
  lowCeiling=true;Vec3f p{};CHECK(!navigation.SpawnPoint(play.get(),seed,p,true));
 });
#include "NxtNavigationRegression.inc"
#include "NxtDamageRegression.inc"
 Group("safe interior population, live retirement and room cleanup",[]{
  Reset(SCENE_LINKS_HOUSE);Run(220,false);CHECK(LivingCount(true)==10);CHECK(LivingCount(false)==0);
  Configure("IndoorCount",3);Run(40);CHECK(LivingCount(true)==3);
  play->roomCtx.curRoom.num=1;Run(220,false);CHECK(LivingCount(true)==3);CHECK(LivingCount(false)==0);
  Configure("Enabled",0);Run(1);CHECK(units.empty());
 });
 Group("all five bosses, separate reinforcement budget and boss departure",[]{
  Reset();Configure("Bots",0);Configure("Enemies",0);Run(250,false);
  std::array<Actor*,5> bosses{};for(auto& [a,u]:units)if(u.bossIndex>=0)bosses[u.bossIndex]=a;
  for(auto* a:bosses)CHECK(a!=nullptr);
  for(int i=0;i<5;++i)for(int j=i+1;j<5;++j)CHECK(DistanceXZ(bosses[i]->world.pos,bosses[j]->world.pos)>=1400);
  player->actor.world.pos=bosses[0]->world.pos;player->actor.world.pos.x+=200;Run(160,false);
  CHECK(LivingCount(true,true)>0);CHECK(LivingCount(true)==0);CHECK(LivingCount(true,true)<=options.raidCap);
  Unit* boss=Find(bosses[0]);boss->lastPlayerTag=frame;boss->hp=0;boss->action=Action::Dead;boss->actionTimer=30;Award(*boss);
  CHECK(gSaveContext.ship.nxt.bossCooldown[0]>0);CHECK(gSaveContext.ship.nxt.bossKills==1);
  for(auto& [a,u]:units)if(u.raid&&u.squad==0)CHECK(u.action==Action::Depart);
  Run(90);CHECK(!Find(bosses[0]));Configure("WorldBosses",0);Run(40);for(auto& [a,u]:units)CHECK(u.bossIndex<0&&!u.raid);
 });
 Group("combat damage, friendly fire, XP credit and exactly-once rewards",[]{
  Reset();Configure("Bots",0);Configure("Enemies",0);Configure("WorldBosses",0);Run(160,false);
  Actor* foe=New(false,{-270,1,-300});Unit* target=Find(foe);target->hp=1;target->hurtTimer=0;target->stats.agility=-1000;
  auto xp=gSaveContext.ship.nxt.xp;HitUnit(*target,&player->actor,2);CHECK(target->hp==0);CHECK(gSaveContext.ship.nxt.xp>xp);
  auto awardedXp=gSaveContext.ship.nxt.xp;auto kills=gSaveContext.ship.nxt.kills;Award(*target);CHECK(gSaveContext.ship.nxt.xp==awardedXp&&gSaveContext.ship.nxt.kills==kills);
  Actor* ally=New(true,{-300,1,-200});float hp=Find(ally)->hp;HitUnit(*Find(ally),&player->actor,20);CHECK(Find(ally)->hp==hp);
  Actor* another=New(false,{0,1,0});hp=Find(another)->hp;HitUnit(*Find(another),foe,20);CHECK(Find(another)->hp==hp);
  Unit& attacker=*Find(another);attacker.stats.agility=1000;DamagePlayer(play.get(),attacker,1);CHECK(gSaveContext.health<gSaveContext.healthCapacity);
  int health=gSaveContext.health;DamagePlayer(play.get(),attacker,1);CHECK(gSaveContext.health==health);
 });
 Group("regen combat delay, invulnerability and pause freeze",[]{
  Reset();Configure("Bots",0);Configure("Enemies",0);Configure("WorldBosses",0);Run(160,false);
  gSaveContext.health=10;OnHealthChange(-8);Run(6*SimHz,false);CHECK(gSaveContext.health==10);Run(3*SimHz,false);CHECK(gSaveContext.health>10);
  int before=frame,health=gSaveContext.health;play->pauseCtx.state=1;Run(100,false);CHECK(frame==before&&gSaveContext.health==health);play->pauseCtx.state=0;
  cutscene=true;Run(100,false);CHECK(frame==before);cutscene=false;
 });
 Group("20 percent berserker distribution and live scaling after combat",[]{
  Reset();Configure("Bots",0);Configure("Enemies",0);Configure("WorldBosses",0);Run(160,false);
  int berserk=0;for(int i=0;i<10000;++i){Actor* a=New(false,{1000,1,1000});berserk+=Find(a)->berserk;RemoveUnit(a);Actor_Kill(a);actors.back().reset();--play->actorCtx.total;}
  CHECK(berserk>1750&&berserk<2250);
  Configure("Enemies",1);Configure("EnemyRatio",1);Configure("EnemyCap",1);
  Actor* a=New(false,{1000,1,1000});Unit* u=Find(a);u->hp=u->stats.maxHp*.5f;u->combatTimer=10;gSaveContext.ship.nxt.level=40;
  Run(1,false);CHECK(u->scaledForLevel==1);u->combatTimer=0;Run(1,false);CHECK(u->scaledForLevel==40);CHECK(std::fabs(u->hp/u->stats.maxHp-.5f)<.001f);
 });
 Group("native collision proxy, source lifetime and scaling toggle",[]{
  Reset();Actor native{};native.id=ACTOR_EN_TEST;native.category=ACTORCAT_ENEMY;native.update=NoUpdate;native.colChkInfo.health=10;
  native.world.pos={-250,1,-300};TrackNative(&native);CHECK(Find(&native));native.colChkInfo.damage=2;
  Nxt_RecordCollision(&player->actor,&native,2);int proxy=Nxt_ApplyNativeDamage(&native);CHECK(proxy>=0&&proxy<=10);
  Actor* bot=New(true,{-230,1,-300});Nxt_RecordCollision(bot,&native,2);CHECK(RecordedHitSource(&native)==bot);RemoveUnit(bot);CHECK(RecordedHitSource(&native)==nullptr);
  Nxt_RecordCollision(&native,&player->actor,8);Configure("ScaleNative",0);CHECK(Nxt_ApplyNativeDamage(&native)==-1);CHECK(Nxt_AdjustPlayerDamage(play.get(),-8)==-8);
  CHECK(Nxt_AdjustPlayerDamage(play.get(),-48)==-48);RemoveUnit(&native);CHECK(RecordedHitSource(&player->actor)==nullptr);
 });
 Group("native resurrection and protected story-arena isolation",[]{
  Reset();Configure("Bots",0);Configure("Enemies",0);Configure("WorldBosses",0);Run(160,false);
  Actor native{};native.id=ACTOR_EN_TEST;native.category=ACTORCAT_ENEMY;native.update=NoUpdate;native.colChkInfo.health=10;
  native.world.pos={-250,1,-300};TrackNative(&native);CHECK(Find(&native));
  native.colChkInfo.health=0;Run(1,false);Award(*Find(&native));CHECK(!Alive(*Find(&native)));CHECK(Find(&native)->awarded);
  native.colChkInfo.health=10;Run(1,false);CHECK(Alive(*Find(&native)));CHECK(!Find(&native)->awarded);CHECK(Find(&native)->hp==Find(&native)->stats.maxHp);
  play->sceneNum=SCENE_FOREST_TEMPLE_BOSS;native.colChkInfo.damage=2;CHECK(Nxt_ApplyNativeDamage(&native)==-1);RemoveUnit(&native);
  TrackNative(&native);CHECK(!Find(&native));
 });
 Group("Mender casting and distinct world-boss warning abilities",[]{
  Reset();Configure("Bots",0);Configure("Enemies",0);Configure("WorldBosses",0);Run(160,false);
  Actor* healer=New(true,{-300,1,-220},2);Actor* ally=New(true,{-300,1,-180},0);
  Unit* h=Find(healer);Unit* a=Find(ally);a->hp=20;a->combatTimer=999;h->abilityTimer=0;h->thinkTimer=999;
  UpdateUnit(*h,play.get());CHECK(h->action==Action::Cast);for(int i=0;i<25;++i)UpdateUnit(*h,play.get());CHECK(a->hp>20);
  for(int i=0;i<BossCount;++i){Actor* boss=New(false,{1000,1,float(1000+i*400)},0x100|i);Unit* b=Find(boss);
   b->target=&player->actor;b->abilityTimer=0;b->thinkTimer=999;UpdateUnit(*b,play.get());
   CHECK(b->action==Action::Cast);CHECK(b->actionTimer==46);CHECK(b->castDuration==46);
   CHECK(b->castRadius==(i==3?570:i==4?330:440));
   if(i==4)CHECK(DistanceXZ(b->castOrigin,player->actor.world.pos)<1);else CHECK(DistanceXZ(b->castOrigin,boss->world.pos)<1);
   b->action=Action::Roam;b->hp=b->stats.maxHp*.2f;UpdateUnit(*b,play.get());CHECK(b->enraged);
   RemoveUnit(boss);Actor_Kill(boss);
  }
 });
 Group("scene actor budget uses category counts beyond the legacy byte counter",[]{
  Reset();Configure("BotCount",1);Configure("Enemies",0);Configure("WorldBosses",0);
  constexpr int spawnLimit=ACTOR_NUMBER_MAX-80;
  // Simulate unrelated scene actors. The byte total has already wrapped, but
  // the category lengths still account for every actor and the reserved slots.
  play->actorCtx.actorLists[ACTORCAT_BG].length=spawnLimit;
  play->actorCtx.total=static_cast<u8>(spawnLimit);
  Run(250,false);CHECK(navigation.Ready());CHECK(LivingCount(true)==0);
  play->actorCtx.actorLists[ACTORCAT_BG].length=spawnLimit+1;
  play->actorCtx.total=static_cast<u8>(spawnLimit+1);
  Run(6,false);CHECK(LivingCount(true)==0);
  // One available slot permits the requested adventurer again.
  play->actorCtx.actorLists[ACTORCAT_BG].length=spawnLimit-1;
  play->actorCtx.total=static_cast<u8>(spawnLimit-1);
  Run(6,false);CHECK(LivingCount(true)==1);
 });
 Group("AI autonomous combat and bounded crowd stress",[]{
  Reset();Configure("WorldBosses",0);Configure("BotCount",30);Configure("EnemyRatio",2);player->invincibilityTimer=-1;Run(330,false);
  CHECK(LivingCount(true)==30);CHECK(LivingCount(false)==60);
  for(auto& [a,u]:units){a->world.pos={-500+static_cast<float>((u.serial%10)*90),1,-500+static_cast<float>((u.serial/10%10)*90)};u.home=u.goal=a->world.pos;u.thinkTimer=0;}
  Run(240);CHECK(gSaveContext.ship.nxt.kills>0);CHECK(units.size()<=MaxPopulation);
  Configure("BotCount",50);Configure("EnemyCap",120);Configure("EnemyRatio",4);Run(800);CHECK(units.size()<=MaxPopulation);
  Configure("BotCount",5);Configure("EnemyCap",10);Run(80);CHECK(LivingCount(true)<=5);CHECK(LivingCount(false)<=10);
  OnWorldDestroyed();CHECK(units.empty()&&navigation.nodes.empty()&&combatText.empty());
 });
 Nxt::OnWorldDestroyed();actors.clear();std::cout<<"PASS "<<groups<<" groups, "<<assertions<<" assertions\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';Nxt::OnWorldDestroyed();return 1;}
}
