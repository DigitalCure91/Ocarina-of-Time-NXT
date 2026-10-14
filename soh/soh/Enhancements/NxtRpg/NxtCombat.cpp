#include "NxtWorld.h"
#include "NxtNavigation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
namespace Nxt {
static bool TargetValid(const Unit& unit,PlayState* play){
    if(!unit.target)return false;
    if(unit.target==&GET_PLAYER(play)->actor)return gSaveContext.health>0;
    Unit* target=Find(unit.target);
    return target&&Alive(*target)&&target->serial==unit.targetSerial;
}
static void SetTarget(Unit& u,Actor* target){
    if(u.target!=target){
        u.path.clear();u.pathCursor=0;u.directPath=false;u.pathRefreshTimer=0;
        u.stuckTimer=u.progressTimer=0;u.progressPos=u.actor->world.pos;
    }
    u.target=target;Unit* other=Find(target);u.targetSerial=other?other->serial:0;
}
static void ChooseTarget(Unit& u,PlayState* play){
    Actor* player=&GET_PLAYER(play)->actor;
    if(u.raid){
        for(auto& [a,other]:units)if(other.bossIndex==u.squad&&Alive(other)){SetTarget(u,a);return;}
        u.action=Action::Depart;u.actionTimer=60;SetTarget(u,nullptr);return;
    }
    if(!CombatScene(play->sceneNum)){SetTarget(u,nullptr);return;}
    const float sight=u.bossIndex>=0?1100.0f:620.0f;
    float best=sight;
    Actor* result=nullptr;
    // Keep a valid opponent until a materially closer one appears. A doorway
    // briefly hiding the target should trigger pathfinding, not a 180-degree
    // turn toward a different enemy at every think interval.
    if(TargetValid(u,play)){
        Unit* current=Find(u.target);
        const bool hostile=u.target==player?!u.bot:current&&current->bot!=u.bot;
        float distance=DistanceXZ(u.actor->world.pos,u.target->world.pos);
        if(hostile&&distance<sight*1.4f&&std::fabs(u.actor->world.pos.y-u.target->world.pos.y)<200){
            best=distance*0.72f;result=u.target;
        }
    }
    if(!u.bot&&DistanceXZ(u.actor->world.pos,player->world.pos)<best&&
        std::fabs(u.actor->world.pos.y-player->world.pos.y)<200&&navigation.Clear(play,u.actor->world.pos,player->world.pos)){
        best=DistanceXZ(u.actor->world.pos,player->world.pos)*(frame-u.lastPlayerTag<160?0.50f:1.0f);result=player;
    }
    for(auto& [a,other]:units){
        if(a==u.actor||!Alive(other)||other.bot==u.bot)continue;
        if(u.bot&&other.bossIndex>=0)continue; // Keep ordinary adventurers on their own patrols.
        if(!u.bot&&other.native)continue;
        if(std::fabs(a->world.pos.y-u.actor->world.pos.y)>150)continue;
        float distance=DistanceXZ(a->world.pos,u.actor->world.pos);
        if(distance<best&&navigation.Clear(play,u.actor->world.pos,a->world.pos)){
            best=distance;result=a;
        }
    }
    SetTarget(u,result);
}
static void ChooseWander(Unit& u,PlayState* play){
    u.wanderRetryTimer=30;
    int here=navigation.Connect(play,u.actor->world.pos,true);
    if(here<0){u.path.clear();u.directPath=false;u.speed=0;return;}
    // Explore the connected component once. Random walks often ended on their
    // start node or repeatedly traversed the same two edges in small rooms.
    std::vector<bool> seen(navigation.nodes.size(),false);
    std::queue<int> pending;pending.push(here);seen[here]=true;
    int goal=-1;float best=-1;
    while(!pending.empty()){
        int at=pending.front();pending.pop();
        const Vec3f& point=navigation.nodes[at].pos;
        float travel=DistanceXZ(point,u.actor->world.pos);
        bool inRange=u.bot?travel<=1800:DistanceXZ(point,u.home)<=700;
        if(at!=here&&travel>90&&inRange){
            float score=(std::min)(travel,900.0f)*(0.75f+Random01(u.seed)*0.5f);
            // Prefer continuing through a room over immediately returning to
            // the previous stop, but allow the way back out of a dead end.
            score+=(std::min)(DistanceXZ(point,u.previousGoal),700.0f)*0.30f;
            if(score>best){best=score;goal=at;}
        }
        for(int next:navigation.nodes[at].edges)if(!seen[next]){
            seen[next]=true;pending.push(next);
        }
    }
    if(goal<0){
        // Very small rooms still have purposeful movement without selecting
        // an unreachable point outside their collision geometry.
        for(int next:navigation.nodes[here].edges){
            if(DistanceXZ(navigation.nodes[next].pos,u.actor->world.pos)>35){goal=next;break;}
        }
    }
    if(goal<0){u.path.clear();u.directPath=false;u.speed=0;return;}
    u.previousGoal=u.goal;u.goal=navigation.nodes[goal].pos;
    u.path=navigation.Route(here,goal);u.pathCursor=0;u.pathGoal=u.goal;
    u.directPath=false;u.pathRefreshTimer=20;u.stuckTimer=u.progressTimer=0;
    u.progressPos=u.actor->world.pos;
}
static void BuildRoute(Unit& u,PlayState* play,const Vec3f& destination){
    u.pathGoal=destination;u.pathRefreshTimer=20+static_cast<int>(u.serial%9);
    u.directPath=u.stuckTimer<12&&navigation.Walkable(play,u.actor->world.pos,destination);
    if(u.directPath){u.path.clear();u.pathCursor=0;return;}
    int begin=navigation.Connect(play,u.actor->world.pos),end=navigation.Connect(play,destination);
    u.path=navigation.Route(begin,end);u.pathCursor=0;
}
static void MoveToward(Unit& u,PlayState* play,const Vec3f& destination,float speed){
    if(u.pathRefreshTimer>0)--u.pathRefreshTimer;
    const bool movedGoal=DistanceXZ(destination,u.pathGoal)>100||std::fabs(destination.y-u.pathGoal.y)>45;
    if((u.path.empty()&&!u.directPath&&u.pathRefreshTimer==0)||
       (u.pathRefreshTimer==0&&(movedGoal||u.stuckTimer>=12)))BuildRoute(u,play,destination);
    Vec3f aim=destination;
    if(!u.directPath){
        // Advance only; do not reset the cursor on a periodic timer. This
        // prevents the visible backwards leg toward an old route start.
        while(u.pathCursor<u.path.size()){
            const Vec3f& node=navigation.nodes[u.path[u.pathCursor]].pos;
            if(DistanceXZ(u.actor->world.pos,node)>32||std::fabs(u.actor->world.pos.y-node.y)>35)break;
            ++u.pathCursor;
        }
        if(u.pathCursor<u.path.size())aim=navigation.nodes[u.path[u.pathCursor]].pos;
        else if(!navigation.Walkable(play,u.actor->world.pos,destination)){
            u.speed=0;
            if(u.pathRefreshTimer==0)BuildRoute(u,play,destination);
            if(!u.target&&u.wanderRetryTimer==0)ChooseWander(u,play);
            return;
        }else u.directPath=true;
    }
    float dx=aim.x-u.actor->world.pos.x,dz=aim.z-u.actor->world.pos.z;
    float distance=std::sqrt(dx*dx+dz*dz);
    if(distance<5){u.speed=0;return;}
    dx/=distance;dz/=distance;
    // Small separation forces keep melee groups readable and stop bots stacking on one point.
    for(const auto& [a,other]:units){
        if(a==u.actor||other.native||!Alive(other)||std::fabs(a->world.pos.y-u.actor->world.pos.y)>60)continue;
        float gap=DistanceXZ(a->world.pos,u.actor->world.pos);
        float desired=u.radius+other.radius+8;
        if(gap>0.1f&&gap<desired){
            float weight=(desired-gap)/desired*0.45f;
            dx+=(u.actor->world.pos.x-a->world.pos.x)/gap*weight;
            dz+=(u.actor->world.pos.z-a->world.pos.z)/gap*weight;
        }
    }
    float directionLength=std::sqrt(dx*dx+dz*dz);if(directionLength<0.01f){u.speed=0;return;}
    dx/=directionLength;dz/=directionLength;
    float travel=(std::min)(speed,distance);
    Vec3f next{u.actor->world.pos.x+dx*travel,u.actor->world.pos.y,u.actor->world.pos.z+dz*travel};
    Vec3f before=u.actor->world.pos;
    bool moved=navigation.Move(play,u.actor,next);
    // Keep a preferred passing side instead of changing it every frame.
    const float preferred=u.serial%2?1.0f:-1.0f;
    const float signs[2]={preferred,-preferred};
    for(float sign:signs){
        if(moved)break;
        next={before.x+(dx+dz*sign)*travel*0.70f,before.y,before.z+(dz-dx*sign)*travel*0.70f};
        moved=navigation.Move(play,u.actor,next);
    }
    u.speed=moved?DistanceXZ(before,u.actor->world.pos):0;
    if(moved){
        int16_t yaw=DirectionYaw(u.actor->world.pos.x-before.x,u.actor->world.pos.z-before.z);
        Math_SmoothStepToS(&u.actor->shape.rot.y,yaw,3,0x1800,0x80);
        u.actor->world.rot.y=u.actor->shape.rot.y;
    }else ++u.stuckTimer;
    if(++u.progressTimer>=20){
        if(DistanceXZ(u.actor->world.pos,u.progressPos)<speed*3)u.stuckTimer+=20;
        else u.stuckTimer=0;
        u.progressPos=u.actor->world.pos;u.progressTimer=0;
    }
    if(u.stuckTimer>=60&&!u.target&&u.wanderRetryTimer==0)ChooseWander(u,play);
}
static void Face(Unit& u,const Vec3f& target){
    int16_t yaw=DirectionYaw(target.x-u.actor->world.pos.x,target.z-u.actor->world.pos.z);
    Math_SmoothStepToS(&u.actor->shape.rot.y,yaw,2,0x2000,0x100);
    u.actor->world.rot.y=u.actor->shape.rot.y;
}
static void BeginAttack(Unit& u){
    u.action=Action::Attack;u.attackDuration=u.berserk?8:14;
    u.actionTimer=u.attackDuration;u.animation=-1;u.animFrame=0;u.speed=0;
}
static void Strike(Unit& u,PlayState* play){
    if(!TargetValid(u,play))return;
    Unit* target=Find(u.target);
    float targetRadius=target?target->radius:20;
    float range=u.radius+targetRadius+45;
    if(DistanceXZ(u.actor->world.pos,u.target->world.pos)>range||
        std::fabs(u.actor->world.pos.y-u.target->world.pos.y)>u.height+40||
        !navigation.Clear(play,u.actor->world.pos,u.target->world.pos))return;
    if(u.target==&GET_PLAYER(play)->actor)DamagePlayer(play,u,u.bossIndex>=0?1.35f:1.0f);
    else if(target&&!target->native)HitUnit(*target,u.actor,2);
    // Native targets use the bot's physical AT collider, registered at the same point of this swing.
}
static bool InAbility(const Unit& boss,const Vec3f& pos){
    if(DistanceXZ(pos,boss.castOrigin)>boss.castRadius||std::fabs(pos.y-boss.castOrigin.y)>170)return false;
    if(boss.bossIndex==1){
        int16_t angle=DirectionYaw(pos.x-boss.actor->world.pos.x,pos.z-boss.actor->world.pos.z);
        int16_t difference=static_cast<int16_t>(angle-boss.actor->shape.rot.y);
        return std::abs(static_cast<int>(difference))<0x3800;
    }
    return true;
}
static void BossAbility(Unit& u,PlayState* play){
    float power=u.bossIndex==3?1.2f:2.2f;
    if(u.enraged)power*=1.25f;
    for(auto& [a,other]:units){
        if(!other.bot||!Alive(other)||!InAbility(u,a->world.pos)||!navigation.Clear(play,u.castOrigin,a->world.pos))continue;
        other.hurtTimer=0;HitUnit(other,u.actor,2,power);
        if(u.bossIndex==3){other.action=Action::Recover;other.actionTimer=28;}
    }
    auto* player=GET_PLAYER(play);
    if(InAbility(u,player->actor.world.pos)&&navigation.Clear(play,u.castOrigin,player->actor.world.pos))DamagePlayer(play,u,power);
    AddText(u.castOrigin,u.bossIndex==0?"BONEQUAKE":u.bossIndex==1?"RENDING ARC":
        u.bossIndex==2?"BREAKER SHOCK":u.bossIndex==3?"MOON HOWL":"CINDER ERUPTION",0xFF96BFFF);
}
static bool Evade(Unit& u,PlayState* play){
    if(!u.bot)return false;
    for(const auto& [_,boss]:units){
        if(boss.bossIndex<0||boss.action!=Action::Cast||!Alive(boss)||!InAbility(boss,u.actor->world.pos))continue;
        float dx=u.actor->world.pos.x-boss.castOrigin.x,dz=u.actor->world.pos.z-boss.castOrigin.z;
        float length=std::sqrt(dx*dx+dz*dz);
        if(length<1){dx=std::sin(u.serial*1.6f);dz=std::cos(u.serial*1.6f);length=1;}
        Vec3f escape{boss.castOrigin.x+dx/length*(boss.castRadius+100),u.actor->world.pos.y,
                     boss.castOrigin.z+dz/length*(boss.castRadius+100)};
        u.action=Action::Pursue;MoveToward(u,play,escape,7.0f);return true;
    }
    return false;
}
void UpdateUnit(Unit& u,PlayState* play){
    if(!Running(play))return;
    if(u.action==Action::Dead||u.action==Action::Depart){
        u.speed=0;if(--u.actionTimer<=0)Actor_Kill(u.actor);return;
    }
    if(u.hurtTimer>0)--u.hurtTimer;
    if(u.cooldown>0)--u.cooldown;
    if(u.abilityTimer>0)--u.abilityTimer;
    if(u.combatTimer>0)--u.combatTimer;
    if(u.thinkTimer>0)--u.thinkTimer;
    if(u.wanderRetryTimer>0)--u.wanderRetryTimer;
    if(!TargetValid(u,play))SetTarget(u,nullptr);
    if(u.knockbackTimer>0){
        Vec3f before=u.actor->world.pos;
        Vec3f pushed{before.x+u.knockbackVelocity.x,before.y,before.z+u.knockbackVelocity.z};
        if(!navigation.Move(play,u.actor,pushed))u.knockbackTimer=0;
        else --u.knockbackTimer;
        u.knockbackVelocity.x*=0.68f;u.knockbackVelocity.z*=0.68f;
        u.speed=DistanceXZ(before,u.actor->world.pos);
        return;
    }
    if(Evade(u,play))return;
    if(u.action==Action::Cast){
        u.speed=0;
        if(--u.actionTimer<=0){
            if(u.bot){Unit* ally=Find(u.target);if(ally&&Alive(*ally)&&DistanceXZ(u.actor->world.pos,ally->actor->world.pos)<500){
                int amount=u.stats.spirit*3;ally->hp=(std::min)(float(ally->stats.maxHp),ally->hp+amount);
                AddText(ally->actor->world.pos,"+"+std::to_string(amount),0xFFB8FF83);
            }}else BossAbility(u,play);
            u.action=Action::Recover;u.actionTimer=u.bot?8:18;u.cooldown=25;u.abilityTimer=u.bot?120:(u.enraged?130:200);
            SetTarget(u,nullptr);
        }
        return;
    }
    if(u.action==Action::Attack){
        u.speed=0;if(TargetValid(u,play))Face(u,u.target->world.pos);
        if(--u.actionTimer==u.attackDuration/2)Strike(u,play);
        if(u.actionTimer<=0){u.action=Action::Recover;u.actionTimer=u.berserk?3:7;
            u.cooldown=u.berserk?7:u.enraged?10:u.bot&&u.role==Role::Skirmisher?15:22;}
        return;
    }
    if(u.action==Action::Recover&&u.actionTimer>0){u.speed=0;--u.actionTimer;return;}
    if(u.hp<u.stats.maxHp&&u.combatTimer==0)u.hp=(std::min)(float(u.stats.maxHp),u.hp+u.stats.maxHp*0.035f/SimHz);
    if(u.bossIndex>=0&&!u.enraged&&u.hp<u.stats.maxHp*0.30f){
        u.enraged=true;AddText(u.actor->world.pos,"ENRAGED",0xFF8585FF);
    }
    if(u.bossIndex>=0&&u.target&&u.abilityTimer==0){
        u.action=Action::Cast;u.castDuration=u.enraged?32:46;u.actionTimer=u.castDuration;
        u.castOrigin=u.bossIndex==4?u.target->world.pos:u.actor->world.pos;
        u.castRadius=u.bossIndex==3?570.0f:u.bossIndex==4?330.0f:440.0f;
        u.animation=-1;u.animFrame=0;u.speed=0;return;
    }
    if(u.bot&&u.role==Role::Mender&&u.abilityTimer==0){
        Unit* wounded=nullptr;float ratio=0.70f;
        for(auto& [a,ally]:units)if(ally.bot&&Alive(ally)&&DistanceXZ(u.actor->world.pos,a->world.pos)<400&&
            ally.hp/ally.stats.maxHp<ratio&&navigation.Clear(play,u.actor->world.pos,a->world.pos)){
            ratio=ally.hp/ally.stats.maxHp;wounded=&ally;
        }
        if(wounded){SetTarget(u,wounded->actor);u.action=Action::Cast;u.castDuration=u.actionTimer=24;u.animation=-1;return;}
        u.abilityTimer=20;
    }
    if(u.thinkTimer<=0){
        u.thinkTimer=16+static_cast<int>(NextRandom(u.seed)%9);
        if(u.action!=Action::Depart)ChooseTarget(u,play);
        if(u.action==Action::Depart)return;
        if(!u.target&&u.wanderRetryTimer==0&&
           (DistanceXZ(u.actor->world.pos,u.goal)<40||(u.path.empty()&&!u.directPath)))ChooseWander(u,play);
    }
    if(u.target){
        float leash=u.bossIndex>=0?1600.0f:u.bot?6000.0f:1200.0f;
        if(DistanceXZ(u.actor->world.pos,u.home)>leash){SetTarget(u,nullptr);u.goal=u.home;u.action=Action::Roam;}
        else{
            u.combatTimer=12*SimHz;
            Unit* other=Find(u.target);float range=u.radius+(other?other->radius:20)+27;
            if(DistanceXZ(u.actor->world.pos,u.target->world.pos)<range&&navigation.Clear(play,u.actor->world.pos,u.target->world.pos)){
                Face(u,u.target->world.pos);u.speed=0;u.action=Action::Pursue;
                if(u.cooldown==0)BeginAttack(u);return;
            }
            u.action=Action::Pursue;
            Vec3f approach=u.target->world.pos;
            // Raid positions are distributed around the target's hit cylinder, not its centre.
            if(u.raid&&other&&other->bossIndex>=0){
                float angle=u.serial*2.39996323f;float orbit=other->radius+18;
                approach.x+=std::sin(angle)*orbit;approach.z+=std::cos(angle)*orbit;
            }
            float speed=u.bot?(u.raid?5.0f:4.3f):u.berserk?7.5f:u.enraged?4.7f:3.2f;
            MoveToward(u,play,approach,speed);return;
        }
    }
    u.action=Action::Roam;
    if(DistanceXZ(u.actor->world.pos,u.goal)>35)MoveToward(u,play,u.goal,u.bot?2.8f:u.berserk?4.8f:1.7f);
    else{u.speed=0;u.action=Action::Recover;u.actionTimer=15+static_cast<int>(NextRandom(u.seed)%45);}
}
} // namespace Nxt
