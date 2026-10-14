// Included inside the shared fixture's main() after its using declarations.
Group("independent bot recipients and native hazard contact strength",[]{
 Reset();Configure("Bots",0);Configure("Enemies",0);Configure("WorldBosses",0);
 Actor boulder{};boulder.id=ACTOR_EN_GOROIWA;boulder.update=NoUpdate;boulder.world.pos={0,1,0};
 Actor* actor=New(true,{50,1,0});Unit* bot=Find(actor);bot->stats.agility=-100;
 CHECK(Nxt_IsBotActor(actor));CHECK(!Nxt_IsBotActor(&player->actor));CHECK(!Nxt_IsBotActor(&boulder));
 CHECK(!Nxt_IsBotActor(nullptr));
 const int health=gSaveContext.health;player->knockbackType=7;player->knockbackDamage=9;
 const Vec3f playerPos=player->actor.world.pos;
 const float hp=bot->hp;bot->seed=501;Nxt_RecordCollision(&boulder,actor,4);
 HitUnit(*bot,RecordedHitSource(actor),RecordedTouchDamage(actor));
 const float smallHit=hp-bot->hp;CHECK(smallHit>0);CHECK(bot->knockbackTimer==6);
 CHECK(bot->knockbackVelocity.x>0);CHECK(bot->knockbackVelocity.z==0);
 bot->hp=hp;bot->hurtTimer=0;bot->seed=501;Nxt_RecordCollision(&boulder,actor,8);
 HitUnit(*bot,RecordedHitSource(actor),RecordedTouchDamage(actor));
 CHECK(hp-bot->hp>smallHit);CHECK(gSaveContext.health==health);
 CHECK(player->knockbackType==7&&player->knockbackDamage==9);
 CHECK(DistanceXZ(player->actor.world.pos,playerPos)==0);
 const float before=actor->world.pos.x;UpdateUnit(*bot,play.get());CHECK(actor->world.pos.x>before);
 CHECK(bot->knockbackTimer==5);CHECK(gSaveContext.health==health);
 Configure("Enabled",0);CHECK(Nxt_IsBotActor(actor)); // Pending collision lists remain safe.
});
Group("destroyed projectiles preserve bot damage and recoil without dangling pointers",[]{
 Reset();Actor* actor=New(true,{40,1,0});Unit* bot=Find(actor);bot->stats.agility=-100;
 auto projectile=std::make_unique<Actor>();projectile->id=ACTOR_EN_NUTSBALL;projectile->update=NoUpdate;
 projectile->world.pos={0,1,0};Nxt_RecordCollision(projectile.get(),actor,4);
 CHECK(RecordedHitSource(actor)==projectile.get());RemoveUnit(projectile.get());projectile.reset();
 CHECK(RecordedHitSource(actor)==nullptr);CHECK(RecordedTouchDamage(actor)==4);
 const float hp=bot->hp;const int health=gSaveContext.health;
 HitUnit(*bot,RecordedHitSource(actor),RecordedTouchDamage(actor));
 CHECK(bot->hp<hp);CHECK(bot->knockbackTimer==6&&bot->knockbackVelocity.x>0);
 CHECK(gSaveContext.health==health);frame+=8;
 CHECK(RecordedTouchDamage(actor)==0&&RecordedHitSource(actor)==nullptr);
});
Group("retired bot attacks never become nearby player damage attribution",[]{
 Reset();Actor native{};native.id=ACTOR_EN_TEST;native.category=ACTORCAT_ENEMY;
 native.update=NoUpdate;native.colChkInfo.health=10;native.colChkInfo.damage=2;
 native.world.pos=player->actor.world.pos;native.world.pos.x+=20;TrackNative(&native);
 Unit* target=Find(&native);CHECK(target);target->stats.agility=-100;
 Actor* bot=New(true,{native.world.pos.x+20,1,native.world.pos.z});
 Nxt_RecordCollision(bot,&native,2);RemoveUnit(bot);
 CHECK(RecordedHitSource(&native)==nullptr);CHECK(RecordedTouchDamage(&native)==2);
 CHECK(Nxt_ApplyNativeDamage(&native)>=0);CHECK(target->lastPlayerTag<0);
 RemoveUnit(&native);
});
Group("hostile arrows damage bots while player arrows remain friendly",[]{
 Reset();Actor* actor=New(true,{40,1,0});Unit* bot=Find(actor);bot->stats.agility=-100;
 Actor arrow{};arrow.id=ACTOR_EN_ARROW;arrow.params=-1;arrow.update=NoUpdate;arrow.world.pos={0,1,0};
 const float hp=bot->hp;Nxt_RecordCollision(&arrow,actor,4);
 CHECK(RecordedHitSource(actor)==&arrow);HitUnit(*bot,RecordedHitSource(actor),RecordedTouchDamage(actor));
 CHECK(bot->hp<hp);CHECK(gSaveContext.health==48);
 bot->hurtTimer=0;const float after=bot->hp;arrow.params=0;
 Nxt_RecordCollision(&arrow,actor,4);CHECK(RecordedHitSource(actor)==&player->actor);
 HitUnit(*bot,RecordedHitSource(actor),RecordedTouchDamage(actor));CHECK(bot->hp==after);
 RemoveUnit(&arrow);
});
Group("bot recoil cannot cross lethal floor or interrupt real Link movement",[]{
 Reset();Actor* actor=New(true,{40,1,0});Unit* bot=Find(actor);bot->knockbackVelocity={6,0,0};
 bot->knockbackTimer=6;voidFloor=true;const Vec3f start=actor->world.pos;
 UpdateUnit(*bot,play.get());CHECK(DistanceXZ(actor->world.pos,start)==0);CHECK(bot->knockbackTimer==0);
 CHECK(gSaveContext.health==48);
});
