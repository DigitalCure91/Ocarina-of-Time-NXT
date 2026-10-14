#include "NxtWorld.h"
#include "NxtConfig.h"
#include "soh/ShipInit.hpp"
#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <cstring>
namespace Nxt {
int ConfigInt(const char* name,int fallback){return CVarGetInteger(name,fallback);}
float ConfigFloat(const char* name,float fallback){return CVarGetFloat(name,fallback);}
static void InitializeSave(bool){std::memset(&gSaveContext.ship.nxt,0,sizeof(gSaveContext.ship.nxt));gSaveContext.ship.nxt.level=1;}
static void LoadSave(){
    auto& s=gSaveContext.ship.nxt;auto* manager=SaveManager::Instance;
    manager->LoadData("level",s.level,uint32_t{1});manager->LoadData("xp",s.xp,uint32_t{0});
    manager->LoadData("kills",s.kills,uint32_t{0});manager->LoadData("bossKills",s.bossKills,uint32_t{0});
    manager->LoadArray("bossCooldown",BossCount,[&](size_t i){manager->LoadData("",s.bossCooldown[i],uint32_t{0});
        s.bossCooldown[i]=Clamp(s.bossCooldown[i],0U,7200U);});
    s.level=Clamp(s.level,1U,static_cast<uint32_t>(MaxLevel));
    s.xp=s.level==MaxLevel?0:Clamp(s.xp,0U,XpRequired(s.level)-1);
}
static void SaveProgress(const SaveContext& snapshot,int,bool){
    const auto& s=snapshot.ship.nxt;auto* manager=SaveManager::Instance;
    manager->SaveData("level",s.level);manager->SaveData("xp",s.xp);manager->SaveData("kills",s.kills);
    manager->SaveData("bossKills",s.bossKills);
    manager->SaveArray("bossCooldown",BossCount,[&](size_t i){manager->SaveData("",s.bossCooldown[i]);});
}
static void Register(){
    static bool registered=false;if(registered)return;registered=true;
    ReadOptions();InitActors();RegisterGui();
    SaveManager::Instance->AddInitFunction(InitializeSave);
    SaveManager::Instance->AddLoadFunction("nxtRpg",1,LoadSave);
    SaveManager::Instance->AddSaveFunction("nxtRpg",1,SaveProgress,true,SECTION_PARENT_NONE);
    auto* gi=GameInteractor::Instance;
    gi->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(Tick);
    gi->RegisterGameHook<GameInteractor::OnActorInit>([](void* a){TrackNative(static_cast<Actor*>(a));});
    gi->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* a){RemoveUnit(static_cast<Actor*>(a));});
    gi->RegisterGameHook<GameInteractor::OnEnemyDefeat>([](void* a){if(Unit* u=Find(static_cast<Actor*>(a)))Award(*u);});
    gi->RegisterGameHook<GameInteractor::OnPlayerHealthChange>(OnHealthChange);
    gi->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t){OnGameLoaded();});
    gi->RegisterGameHook<GameInteractor::OnPlayDestroy>(OnWorldDestroyed);
}
static RegisterShipInitFunc registerNxt(Register);
} // namespace Nxt
