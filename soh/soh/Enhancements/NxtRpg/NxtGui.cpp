#include "NxtWorld.h"
#include "NxtGui.h"
#include "NxtNavigation.h"
#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

extern "C" { extern PlayState* gPlayState; }
namespace Nxt {
static std::shared_ptr<Ship::GuiWindow> settingsWindow;
static constexpr ImU32 Ink=IM_COL32(14,24,23,228),Border=IM_COL32(165,151,104,180);
static constexpr ImU32 White=IM_COL32(247,242,221,255),Muted=IM_COL32(192,199,182,255);
static constexpr ImU32 Gold=IM_COL32(242,197,102,255),Teal=IM_COL32(93,218,185,255),Red=IM_COL32(244,99,102,255);
static bool dirty=false;
static int lastOverlayFrame=-1000,visiblePlateCount=0;
static void Tip(const char* text) { if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",text); }
static void Toggle(const char* label,const char* cvar,bool value,const char* tip=nullptr) {
    bool selected=CVarGetInteger(cvar,value?1:0)!=0;
    if(ImGui::Checkbox(label,&selected)){CVarSetInteger(cvar,selected);dirty=true;}
    if(tip)Tip(tip);
}
static void Integer(const char* label,const char* cvar,int value,int lo,int hi,const char* tip=nullptr) {
    int selected=Clamp(CVarGetInteger(cvar,value),lo,hi);
    if(ImGui::SliderInt(label,&selected,lo,hi)){CVarSetInteger(cvar,selected);dirty=true;}
    if(tip)Tip(tip);
}
static void Real(const char* label,const char* cvar,float value,float lo,float hi,const char* format="%.2f") {
    float selected=CVarGetFloat(cvar,value);if(!std::isfinite(selected))selected=value;selected=Clamp(selected,lo,hi);
    if(ImGui::SliderFloat(label,&selected,lo,hi,format)){CVarSetFloat(cvar,selected);dirty=true;}
}
static void Section(const char* text) { ImGui::Spacing();ImGui::SeparatorText(text); }
static void Preset(int bots,int enemies,int raid) {
    CVarSetInteger(NXT_CVAR("BotCount"),bots);CVarSetInteger(NXT_CVAR("EnemyCap"),enemies);
    CVarSetInteger(NXT_CVAR("RaidSize"),raid);CVarSetInteger(NXT_CVAR("RaidCap"),raid*3);
    dirty=true;
}
class SettingsWindow final:public Ship::GuiWindow {
public:
    SettingsWindow():GuiWindow(NXT_CVAR("SettingsWindow"),"Ocarina of Time NXT | RPG control panel",ImVec2(760,650)){}
    void InitElement()override{}
    void UpdateElement()override{}
    void DrawElement()override {
        ImGui::TextColored(ImVec4(0.95f,0.78f,0.4f,1),"OCARINA OF TIME NXT");
        ImGui::TextDisabled("Single-player living-world RPG");
        Toggle("Enable NXT framework",NXT_CVAR("Enabled"),true,"Disable to remove NXT actors and stop all NXT combat/progression. Original story actors are not deleted.");
        ImGui::SameLine();ImGui::TextDisabled("Progress is stored in the current game save.");
        if(ImGui::BeginTabBar("NxtTabs")){
            if(ImGui::BeginTabItem("Character")){
                const Stats stats=CurrentPlayerStats();const auto& save=gSaveContext.ship.nxt;
                Section("Your adventurer");
                if(gPlayState&&gSaveContext.gameMode==GAMEMODE_NORMAL){
                    ImGui::Text("LEVEL %d / %d",stats.level,MaxLevel);
                    char progress[80];std::snprintf(progress,sizeof(progress),"%u / %u XP",save.xp,XpRequired(stats.level));
                    ImGui::ProgressBar(stats.level==MaxLevel?1.0f:float(save.xp)/(std::max)(1U,XpRequired(stats.level)),ImVec2(-1,22),stats.level==MaxLevel?"MAX LEVEL":progress);
                    if(ImGui::BeginTable("Stats",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH)){
                        const char* names[]={"Maximum HP","Attack","Defense","Spirit","Agility","Critical chance"};
                        for(int i=0;i<6;++i){
                            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(names[i]);ImGui::TableNextColumn();
                            if(i==5)ImGui::Text("%.1f%%",stats.crit*100);
                            else {const int values[]={stats.maxHp,stats.attack,stats.defense,stats.spirit,stats.agility};ImGui::Text("%d",values[i]);}
                        }ImGui::EndTable();
                    }
                    ImGui::Text("Credited victories: %u     World boss victories: %u",save.kills,save.bossKills);
                }else ImGui::TextWrapped("Load a game to view your adventurer. Existing saves begin at level 1; no story progress is reset.");
                Section("Progression");
                Toggle("Player levels and RPG damage",NXT_CVAR("Rpg"),true);
                Toggle("Scale original combat enemies",NXT_CVAR("ScaleNative"),true,"Adds RPG health and damage to ordinary original enemies. Story bosses retain their original mechanics.");
                Toggle("Share nearby bot kill XP",NXT_CVAR("ShareXp"),true,"A nearby untagged bot victory grants 35% XP. Participating grants full XP; no party is required.");
                Real("Experience multiplier",NXT_CVAR("XpRate"),1,0.1f,10,"%.2fx");
                Real("Incoming damage multiplier",NXT_CVAR("DamageRate"),1,0.25f,4,"%.2fx");
                Integer("Enemy / bot level variation",NXT_CVAR("LevelSpread"),3,0,10);
                Section("Recovery");
                Toggle("Regenerate player health",NXT_CVAR("Regen"),true);
                Integer("Out-of-combat delay (seconds)",NXT_CVAR("RegenDelay"),7,0,30);
                Real("Base health restored per second",NXT_CVAR("RegenPercent"),3,0.25f,15,"%.2f%%");
                ImGui::TextWrapped("Hearts remain the authoritative health meter. RPG HP is mapped to that meter, preserving heart containers, healing items and normal saves. Leveling fully restores health.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Population")){
                Section("Adventurers");
                Toggle("Roaming adventurers",NXT_CVAR("Bots"),true);
                Integer("Outdoor / combat-map bots",NXT_CVAR("BotCount"),30,0,50);
                Integer("Safe interior bots",NXT_CVAR("IndoorCount"),10,0,50);
                Real("Adventurer attack strength",NXT_CVAR("BotPower"),1,0.25f,3,"%.2fx");
                if(gPlayState&&SceneEnabled(gPlayState->sceneNum)){
                    const std::string key="gNxt.Scene."+std::to_string(gPlayState->sceneNum)+".Bots";
                    Integer("Current map bot override (-1 = inherit)",key.c_str(),-1,-1,50);
                    ImGui::Text("Scene %d / room %d: %s",gPlayState->sceneNum,gPlayState->roomCtx.curRoom.num,SceneKind(gPlayState->sceneNum));
                    ImGui::Text("Current targets: %d adventurers / %d added enemies",DesiredBots(gPlayState->sceneNum),DesiredEnemies(gPlayState->sceneNum));
                }
                Section("Additional encounters");
                Toggle("Added combat enemies",NXT_CVAR("Enemies"),true);
                Integer("Added enemies per adventurer",NXT_CVAR("EnemyRatio"),2,0,4);
                Integer("Maximum added regular enemies",NXT_CVAR("EnemyCap"),90,0,120);
                Integer("Enemy respawn delay (seconds)",NXT_CVAR("EnemyRespawn"),35,5,300);
                Integer("Berserker spawn chance",NXT_CVAR("BerserkChance"),20,0,100,"Applies when additional enemies spawn. Berserkers have faster AI/animation, more health and attack, and red-orange particles.");
                ImGui::TextWrapped("Settlements and houses never receive added enemies. Original scene actors remain untouched. Populations are simulated in the loaded scene, not in unloaded maps.");
                Section("Population presets");
                if(ImGui::Button("Light"))Preset(15,35,8);ImGui::SameLine();
                if(ImGui::Button("Balanced"))Preset(30,90,12);ImGui::SameLine();
                if(ImGui::Button("Crowded"))Preset(50,110,16);
                Tip("The shared NXT safety budget is 220 tracked actors. Spawn targets can be lower when there is not enough safe terrain.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("World bosses")){
                Section("Hyrule Field encounters");
                Toggle("Five world bosses",NXT_CVAR("WorldBosses"),true);
                Integer("Reinforcements per engaged boss",NXT_CVAR("RaidSize"),12,0,24);
                Integer("Total reinforcement budget",NXT_CVAR("RaidCap"),36,0,48);
                Integer("Boss health duration target (seconds)",NXT_CVAR("BossDuration"),300,60,900);
                Real("Boss health multiplier",NXT_CVAR("BossHpRate"),1,0.25f,5,"%.2fx");
                Integer("Boss respawn (active-play minutes)",NXT_CVAR("BossRespawn"),20,2,120);
                ImGui::TextWrapped("Raid helpers use a separate population. They arrive when you approach an encounter and depart afterward. Regular adventurers continue their patrols. Duration is a health-budget estimate, not a guaranteed kill time. Boss health settings apply at spawn or out-of-combat rescaling.");
                Section("Field roster");
                for(int i=0;i<BossCount;++i){
                    const auto& boss=bossViews[i];
                    ImGui::Text("%s",BossName(i));
                    if(boss.alive){
                        char line[96];std::snprintf(line,sizeof(line),"Lv %d | %.0f / %.0f HP | %d raiders",boss.level,boss.hp,boss.maxHp,boss.raiders);
                        ImGui::ProgressBar(Clamp(boss.hp/boss.maxHp,0.0f,1.0f),ImVec2(-1,18),line);
                    }else {
                        const uint32_t cooldown=gSaveContext.ship.nxt.bossCooldown[i];
                        if(cooldown)ImGui::TextDisabled("Respawn in %u:%02u active-play minutes",cooldown/60,cooldown%60);
                        else ImGui::TextDisabled("Ready - spawns on surveyed, reachable Hyrule Field terrain");
                    }
                }
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Display")){
                Section("HUD");
                Toggle("Player level / health / XP HUD",NXT_CVAR("Hud"),true);
                Toggle("World nameplates",NXT_CVAR("Nameplates"),true);
                Toggle("Friendly nameplates",NXT_CVAR("FriendlyPlates"),true);
                Toggle("Floating combat numbers",NXT_CVAR("CombatText"),true);
                Toggle("Boss encounter panel",NXT_CVAR("BossHud"),true);
                Integer("Maximum visible nameplates",NXT_CVAR("PlateLimit"),24,4,64);
                Integer("Nameplate view distance",NXT_CVAR("PlateDistance"),1500,300,4000);
                Real("HUD scale",NXT_CVAR("HudScale"),1,0.65f,1.6f,"%.2fx");
                Real("HUD vertical offset",NXT_CVAR("HudOffset"),24,0,160,"%.0f px");
                ImGui::TextWrapped("Nameplates follow visible actors, keep clear of the player HUD, and prioritize your target. Friendly roles and hostile states have distinct colors. Display works at native resolution, with resolution scaling, and with MSAA.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Diagnostics")){
                Section("Loaded scene");
                ImGui::TextWrapped("%s",Status().c_str());
                ImGui::Text("Adventurers %d | Additional enemies %d | Raid helpers %d",LivingCount(true),LivingCount(false),LivingCount(true,true));
                ImGui::Text("Tracked actors %zu / %d | Navigation nodes %zu (%d connected)",units.size(),MaxPopulation,navigation.nodes.size(),navigation.ReachableCount());
                ImGui::Text("Game overlay: %s | Visible nameplates: %d",
                    ImGui::GetFrameCount()-lastOverlayFrame<=2?"active":"waiting for a game viewport",visiblePlateCount);
                ImGui::TextWrapped("Repopulation retires NXT actors and rebuilds navigation. It does not erase levels, reset original enemies or bypass saved world-boss cooldowns.");
                if(ImGui::Button("Repopulate current scene"))RequestRepopulate();
                Section("Operational limits");
                ImGui::TextWrapped("Bots use ground navigation: they do not swim, climb ladders, enter crawlspaces or solve puzzles. Scripted cinematics and story-boss arenas are protected. Small rooms may fit fewer bots than requested. All simulation pauses during menus, dialogue and transitions.");
                ImGui::TextWrapped("This is a source implementation, not a multiplayer network server. NXT actor rendering, every scene and large-population performance require in-game testing on your build.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        // Do not write the config on every slider tick; commit after the interaction ends.
        if(dirty&&!ImGui::IsAnyItemActive()){CVarSave();dirty=false;}
    }
};
struct View { ImVec2 pos,size;float aspect=4.0f/3.0f; };
static float HudScale(const View& view) {
    float scale=CVarGetFloat(NXT_CVAR("HudScale"),1);
    if(!std::isfinite(scale))scale=1;
    scale=Clamp(scale,0.65f,1.6f)*Clamp(view.size.y/600.0f,0.85f,1.0f);
    // Preserve text at native resolution while ensuring an enlarged HUD still
    // fits a narrow game viewport. The old height/600 rule produced 4px names.
    return (std::min)(scale,(view.size.x-24.0f)/360.0f);
}
static bool Project(PlayState* play,const View& view,Vec3f pos,ImVec2& result) {
    Vec3f clip{};float w=0;
    SkinMatrix_Vec3fMtxFMultXYZW(&play->viewProjectionMtxF,&pos,&clip,&w);
    if(!std::isfinite(w)||w<1.0f)return false;
    float nx=clip.x/w*(4.0f/3.0f)/view.aspect,ny=clip.y/w;
    if(!std::isfinite(nx)||!std::isfinite(ny)||nx<-1.05f||nx>1.05f||ny<-1.05f||ny>1.05f)return false;
    result={view.pos.x+(nx+1)*view.size.x*0.5f,view.pos.y+(1-ny)*view.size.y*0.5f};return true;
}
static void Text(ImDrawList* draw,ImVec2 pos,ImU32 color,const std::string& text,float size) {
    draw->AddText(ImGui::GetFont(),size,ImVec2(pos.x+1,pos.y+1),IM_COL32(0,0,0,230),text.c_str());
    draw->AddText(ImGui::GetFont(),size,pos,color,text.c_str());
}
static float TextWidth(const std::string& text,float size) {
    return ImGui::GetFont()->CalcTextSizeA(size,10000,0,text.c_str()).x;
}
static void FittedText(ImDrawList* draw,ImVec2 pos,ImU32 color,std::string text,float size,float width) {
    // Keep labels readable. Long names are shortened instead of shrinking to a
    // few pixels on native-resolution or small docked game viewports.
    if(TextWidth(text,size)>width){
        while(!text.empty()&&TextWidth(text+"...",size)>width)text.pop_back();
        text+="...";
    }
    Text(draw,pos,color,text,size);
}
static void Panel(ImDrawList* draw,ImVec2 p,ImVec2 size,ImU32 border=Border) {
    draw->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),Ink,6);
    draw->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),border,6);
}
static void Bar(ImDrawList* draw,ImVec2 p,float width,float height,float fraction,ImU32 color) {
    fraction=Clamp(fraction,0.0f,1.0f);
    draw->AddRectFilled(p,ImVec2(p.x+width,p.y+height),IM_COL32(33,43,58,240),3);
    if(fraction>0)draw->AddRectFilled(p,ImVec2(p.x+width*fraction,p.y+height),color,3);
}
static ImRect PlayerHudBounds(const View& view,float scale) {
    const float width=(std::min)(360*scale,view.size.x-24),height=84*scale;
    float offset=CVarGetFloat(NXT_CVAR("HudOffset"),24);
    if(!std::isfinite(offset))offset=24;
    offset=Clamp(offset,0.0f,(std::max)(0.0f,view.size.y-height-8));
    ImVec2 p{view.pos.x+(view.size.x-width)*0.5f,view.pos.y+view.size.y-height-offset};
    return ImRect(p,ImVec2(p.x+width,p.y+height));
}
static void PlayerHud(ImDrawList* draw,const View& view,float scale) {
    if(!options.hud)return;
    const Stats stats=CurrentPlayerStats();const auto& save=gSaveContext.ship.nxt;
    const ImRect bounds=PlayerHudBounds(view,scale);
    const ImVec2 p=bounds.Min;
    const float width=bounds.GetWidth(),height=bounds.GetHeight();
    Panel(draw,p,ImVec2(width,height));
    Text(draw,{p.x+12*scale,p.y+8*scale},Gold,"NXT  /  LEVEL "+std::to_string(stats.level),15*scale);
    float health=Clamp(float(gSaveContext.health)/(std::max)(1,int(gSaveContext.healthCapacity)),0.0f,1.0f);
    char hp[80];std::snprintf(hp,sizeof(hp),"HP %d / %d",int(std::ceil(stats.maxHp*health)),stats.maxHp);
    Text(draw,{p.x+width-12*scale-TextWidth(hp,13*scale),p.y+8*scale},White,hp,13*scale);
    Bar(draw,{p.x+12*scale,p.y+31*scale},width-24*scale,8*scale,health,health<0.25f?Red:Teal);
    float xp=stats.level==MaxLevel?1:float(save.xp)/(std::max)(1U,XpRequired(stats.level));
    Bar(draw,{p.x+12*scale,p.y+48*scale},width-24*scale,5*scale,xp,Gold);
    std::string line=stats.level==MaxLevel?"MAX LEVEL":"XP "+std::to_string(save.xp)+" / "+std::to_string(XpRequired(stats.level));
    Text(draw,{p.x+12*scale,p.y+60*scale},Muted,line,12*scale);
    Text(draw,{p.x+width-142*scale,p.y+60*scale},Muted,std::to_string(LivingCount(true))+" adventurers",12*scale);
}
static Unit* ClosestBoss(PlayState* play) {
    if(!CVarGetInteger(NXT_CVAR("BossHud"),1))return nullptr;
    Unit* closest=nullptr;float nearest=2600;
    for(auto& [actor,u]:units)if(u.bossIndex>=0&&Alive(u)){
        const float distance=DistanceXZ(actor->world.pos,GET_PLAYER(play)->actor.world.pos);
        if(distance<nearest){nearest=distance;closest=&u;}
    }
    return closest;
}
static ImRect BossHudBounds(const View& view,float scale,bool casting) {
    const float width=(std::min)(440*scale,view.size.x-28);
    const ImVec2 p{view.pos.x+(view.size.x-width)/2,view.pos.y+22*scale};
    return ImRect(p,ImVec2(p.x+width,p.y+(casting?123.0f:76.0f)*scale));
}
static void Plates(ImDrawList* draw,PlayState* play,const View& view,float scale) {
    visiblePlateCount=0;
    if(!options.plates)return;
    struct Candidate {Unit* unit;float priority;ImVec2 anchor;};std::vector<Candidate> candidates;
    Actor* focused=GET_PLAYER(play)->focusActor;
    for(auto& [actor,u]:units){
        if(!Alive(u)||!u.visible||(u.bot&&!options.friendlyPlates))continue;
        const float distance=DistanceXZ(actor->world.pos,GET_PLAYER(play)->actor.world.pos);
        if(distance>options.plateDistance)continue;
        Vec3f world=actor->world.pos;world.y+=u.height+16;ImVec2 anchor;
        if(!Project(play,view,world,anchor))continue;
        candidates.push_back({&u,distance-(actor==focused?10000.0f:0)-(u.bossIndex>=0?2000.0f:0),anchor});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
        return a.priority==b.priority?a.unit->serial<b.unit->serial:a.priority<b.priority;
    });
    std::vector<ImRect> occupied;
    if(options.hud)occupied.push_back(PlayerHudBounds(view,scale));
    if(const Unit* boss=ClosestBoss(play))occupied.push_back(BossHudBounds(view,scale,boss->action==Action::Cast));
    int shown=0;
    for(auto& candidate:candidates){
        if(shown>=options.plateLimit)break;
        Unit& u=*candidate.unit;const bool target=u.actor==focused;
        const float width=(u.bossIndex>=0?250.0f:186.0f)*scale,height=54*scale;
        const ImVec2 base{
            Clamp(candidate.anchor.x-width/2,view.pos.x+3,view.pos.x+view.size.x-width-3),
            Clamp(candidate.anchor.y-height,view.pos.y+3,view.pos.y+view.size.y-height-3)};
        ImVec2 p=base;bool placed=false;
        for(int attempt=0;attempt<7;++attempt){
            // Try above and below the head in alternating rows; the previous
            // upward-only search discarded even the target at the top edge.
            const int row=attempt==0?0:(attempt%2?-((attempt+1)/2):attempt/2);
            p.y=base.y+row*(height+3*scale);
            ImRect rect(p,ImVec2(p.x+width,p.y+height));bool collision=false;
            for(const auto& used:occupied)if(rect.Overlaps(used)){collision=true;break;}
            if(!collision&&p.y>=view.pos.y&&p.y+height<=view.pos.y+view.size.y&&p.x>=view.pos.x&&p.x+width<=view.pos.x+view.size.x){occupied.push_back(rect);placed=true;break;}
        }
        if(!placed)continue;++shown;++visiblePlateCount;
        ImU32 accent=u.bot?Teal:u.bossIndex>=0?Gold:u.berserk?IM_COL32(255,126,50,255):Red;
        Panel(draw,p,ImVec2(width,height),target?Gold:Border);
        if(std::fabs(p.y+height-candidate.anchor.y)>4||std::fabs(p.x+width/2-candidate.anchor.x)>4)
            draw->AddLine({Clamp(candidate.anchor.x,p.x+6,p.x+width-6),candidate.anchor.y<p.y?p.y:p.y+height},candidate.anchor,Border);
        std::string name="Lv "+std::to_string(u.stats.level)+"  "+u.name;
        FittedText(draw,{p.x+6*scale,p.y+5*scale},target?Gold:White,name,12*scale,width-12*scale);
        std::string detail=u.bot?RoleName(u.role):u.enraged?"ENRAGED":u.berserk?"BERSERKER":u.bossIndex>=0?"WORLD BOSS":"HOSTILE";
        detail+="  /  ";detail+=ActionName(u.action);
        FittedText(draw,{p.x+6*scale,p.y+21*scale},accent,detail,10*scale,width-12*scale);
        Bar(draw,{p.x+6*scale,p.y+38*scale},width-12*scale,9*scale,u.displayHp/(std::max)(1,u.stats.maxHp),accent);
        char health[64];std::snprintf(health,sizeof(health),"%.0f / %d",(std::max)(0.0f,u.hp),u.stats.maxHp);
        const float textWidth=ImGui::GetFont()->CalcTextSizeA(9*scale,10000,0,health).x;
        Text(draw,{p.x+(width-textWidth)/2,p.y+37*scale},White,health,9*scale);
    }
}
static void BossHud(ImDrawList* draw,PlayState* play,const View& view,float scale) {
    const Unit* closest=ClosestBoss(play);
    if(!closest)return;const Unit& boss=*closest;
    const ImRect bounds=BossHudBounds(view,scale,false);
    const ImVec2 p=bounds.Min;const float width=bounds.GetWidth();
    Panel(draw,p,ImVec2(width,76*scale),Gold);
    Text(draw,{p.x+12*scale,p.y+8*scale},Gold,"WORLD BOSS  /  LEVEL "+std::to_string(boss.stats.level),11*scale);
    FittedText(draw,{p.x+12*scale,p.y+24*scale},White,boss.name,15*scale,width-24*scale);
    Bar(draw,{p.x+12*scale,p.y+48*scale},width-24*scale,10*scale,boss.displayHp/(std::max)(1,boss.stats.maxHp),Red);
    char line[128];std::snprintf(line,sizeof(line),"%.0f / %d HP   |   %d raid allies%s",boss.hp,boss.stats.maxHp,
        bossViews[boss.bossIndex].raiders,boss.enraged?"   |   ENRAGED":"");
    FittedText(draw,{p.x+12*scale,p.y+62*scale},Muted,line,10*scale,width-24*scale);
    if(boss.action==Action::Cast){
        const char* names[]={"BONEQUAKE - MOVE OUT","RENDING ARC - GET BEHIND","BREAKER SHOCK - MOVE OUT","MOON HOWL - RETREAT","CINDER ERUPTION - LEAVE THE RING"};
        Panel(draw,{p.x,p.y+82*scale},ImVec2(width,41*scale),Red);
        FittedText(draw,{p.x+10*scale,p.y+89*scale},Gold,names[boss.bossIndex],12*scale,width-20*scale);
        Bar(draw,{p.x+10*scale,p.y+110*scale},width-20*scale,5*scale,1-float(boss.actionTimer)/(std::max)(1,boss.castDuration),Gold);
    }
}
class HudWindow final:public Ship::GuiWindow {
public:
    HudWindow():GuiWindow("",true,"NXT world overlay"){}
    void InitElement()override{}
    void UpdateElement()override{}
    void DrawElement()override{}
    void Draw()override {
        lastOverlayFrame=ImGui::GetFrameCount();visiblePlateCount=0;
        if(!gPlayState||!Running(gPlayState))return;
        auto* context=Ship::Context::GetRawInstance();if(!context||!context->GetWindow())return;
        auto gui=std::dynamic_pointer_cast<Fast::Fast3dGui>(context->GetWindow()->GetGui());if(!gui)return;
        View view{gui->GetGameImagePosition(),gui->GetGameImageSize(),gui->GetGameImageAspect()};
        if(!std::isfinite(view.pos.x)||!std::isfinite(view.pos.y)||!std::isfinite(view.size.x)||
           !std::isfinite(view.size.y)||!std::isfinite(view.aspect)||view.size.x<100||view.size.y<100||view.aspect<=0)return;
        const float scale=HudScale(view);
        // The overlay callback runs inside Main Game after its optional framebuffer
        // image, so use that live draw list directly (also in direct-render mode).
        ImDrawList* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(view.pos,ImVec2(view.pos.x+view.size.x,view.pos.y+view.size.y),true);
        Plates(draw,gPlayState,view,scale);
        if(CVarGetInteger(NXT_CVAR("CombatText"),1))for(const auto& text:combatText){
            Vec3f world=text.pos;world.y+=70;ImVec2 p;if(!Project(gPlayState,view,world,p))continue;
            uint32_t alpha=static_cast<uint32_t>(255*Clamp(text.life/12.0f,0.0f,1.0f));
            Text(draw,p,(text.color&0x00FFFFFF)|(alpha<<24),text.text,15*scale);
        }
        PlayerHud(draw,view,scale);BossHud(draw,gPlayState,view,scale);
        draw->PopClipRect();
    }
};
void OpenSettings(){if(settingsWindow)settingsWindow->Show();}
void RegisterGui(){
    auto* context=Ship::Context::GetRawInstance();if(!context||!context->GetWindow()||!context->GetWindow()->GetGui())return;
    settingsWindow=std::make_shared<SettingsWindow>();
    context->GetWindow()->GetGui()->AddGuiWindow(settingsWindow);
    if(auto fast=std::dynamic_pointer_cast<Fast::Fast3dGui>(context->GetWindow()->GetGui())){
        auto overlay=std::make_shared<HudWindow>();
        fast->AddGameImageOverlay([overlay](){overlay->Draw();});
    }
}
} // namespace Nxt
