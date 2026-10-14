// Included inside main: these cases exercise the real navigation/combat code
// against explicit synthetic obstacles, not ROM scene geometry.
Group("route attachment rejects a nearer node behind a wall",[]{
 Reset(SCENE_LINKS_HOUSE);wall=true;
 navigation.nodes={{{20,1,1000},{},true},{{-170,1,1000},{},true}};
 Vec3f position{-20,1,1000};
 CHECK(navigation.Nearest(position)==0);
 CHECK(navigation.Connect(play.get(),position)==1);
 Vec3f invalid{std::numeric_limits<float>::quiet_NaN(),1,0};
 CHECK(!navigation.Ground(play.get(),invalid));
 CHECK(!navigation.Clear(nullptr,position,position));
});
Group("persistent route follows doorway instead of resetting backwards",[]{
 Reset(SCENE_LINKS_HOUSE);wall=true;
 navigation.nodes={{{-400,1,900},{1},true},{{-250,1,0},{0,2},true},
                   {{250,1,0},{1,3},true},{{400,1,900},{2},true}};
 Actor* actor=New(true,{-400,1,900});Unit& bot=*Find(actor);
 bot.goal=navigation.nodes[3].pos;bot.path={0,1,2,3};bot.pathGoal=bot.goal;
 bot.thinkTimer=INT_MAX;bot.abilityTimer=INT_MAX;bot.wanderRetryTimer=INT_MAX;
 float nearest=DistanceXZ(actor->world.pos,bot.goal);int backwards=0;size_t cursor=0;
 Vec3f previous=actor->world.pos;
 for(int i=0;i<1000&&DistanceXZ(actor->world.pos,bot.goal)>40;++i){
  ++frame;UpdateUnit(bot,play.get());
  CHECK(bot.pathCursor>=cursor);cursor=bot.pathCursor;
  if(previous.x<0&&actor->world.pos.x>=0)CHECK(std::fabs(actor->world.pos.z)<=300);
  if(actor->world.pos.x<-260&&actor->world.pos.z>previous.z+0.1f)++backwards;
  CHECK(DistanceXZ(previous,actor->world.pos)<4.1f);previous=actor->world.pos;
  nearest=(std::min)(nearest,DistanceXZ(actor->world.pos,bot.goal));
 }
 CHECK(nearest<40);CHECK(backwards==0);
});
Group("visible target across a pit takes the safe graph detour",[]{
 Reset();pit=true;
 navigation.nodes={{{-400,1,0},{1},true},{{-200,1,500},{0,2},true},
                   {{200,1,500},{1,3},true},{{400,1,0},{2},true}};
 Actor* actor=New(true,{-400,1,0});Actor* foe=New(false,{400,1,0});
 Unit& bot=*Find(actor);bot.target=foe;bot.targetSerial=Find(foe)->serial;
 bot.thinkTimer=INT_MAX;bot.abilityTimer=INT_MAX;Find(foe)->hp=100000;
 CHECK(navigation.Clear(play.get(),actor->world.pos,foe->world.pos));
 CHECK(!navigation.Walkable(play.get(),actor->world.pos,foe->world.pos));
 float furthestZ=0;
 for(int i=0;i<500&&actor->world.pos.x<300;++i){
  ++frame;UpdateUnit(bot,play.get());furthestZ=(std::max)(furthestZ,actor->world.pos.z);
  CHECK(!(std::fabs(actor->world.pos.x)<100&&std::fabs(actor->world.pos.z)<350));
 }
 CHECK(actor->world.pos.x>300);CHECK(furthestZ>400);
});
Group("wall correction cannot masquerade as successful movement",[]{
 Reset();Actor* actor=New(true,{-400,1,0});Vec3f start=actor->world.pos;
 Vec3f next{-397,1,0};blockedMotion=true;
 CHECK(!navigation.Move(play.get(),actor,next));CHECK(DistanceXZ(start,actor->world.pos)==0);
 blockedMotion=false;reverseWallCorrection=true;
 CHECK(!navigation.Move(play.get(),actor,next));CHECK(DistanceXZ(start,actor->world.pos)==0);
 reverseWallCorrection=false;CHECK(navigation.Move(play.get(),actor,next));
 CHECK(actor->world.pos.x>start.x);
});
Group("bidirectional routes reject a drop with an impossible return climb",[]{
 Reset();stepFloor=true;
 const Vec3f low{-60,1,0},high{60,37,0};
 CHECK(navigation.Walkable(play.get(),high,low));
 CHECK(!navigation.Walkable(play.get(),low,high));
 navigation.Reset();navigation.nodes={{high,{},false},{low,{},false}};
 navigation.Step(play.get());CHECK(navigation.Ready());
 CHECK(navigation.nodes[0].edges.empty()&&navigation.nodes[1].edges.empty());
});
Group("roaming explores successive stops without an immediate U-turn",[]{
 Reset(SCENE_LINKS_HOUSE);
 navigation.nodes={{{0,1,0},{1},true},{{400,1,0},{0,2},true},
                   {{800,1,0},{1,3},true},{{1200,1,0},{2},true}};
 Actor* actor=New(true,{0,1,0});Unit& bot=*Find(actor);bot.abilityTimer=INT_MAX;bot.thinkTimer=0;
 UpdateUnit(bot,play.get());CHECK(bot.goal.x>=800);const Vec3f firstGoal=bot.goal;
 for(int i=0;i<500&&DistanceXZ(actor->world.pos,firstGoal)>40;++i){++frame;UpdateUnit(bot,play.get());}
 CHECK(actor->world.pos.x>=firstGoal.x-40);
 // In a dead-end corridor returning is valid; it must be a deliberate new
 // destination after reaching the far end, not a periodic cursor reset.
 CHECK(bot.pathCursor>0);
});
Group("close rival candidates do not repeatedly steal the same target",[]{
 Reset();Actor* actor=New(true,{0,1,0});Actor* first=New(false,{400,1,0});
 Actor* challenger=New(false,{-370,1,0});Unit& bot=*Find(actor);
 bot.target=first;bot.targetSerial=Find(first)->serial;bot.abilityTimer=INT_MAX;
 for(int i=0;i<8;++i){bot.thinkTimer=0;UpdateUnit(bot,play.get());CHECK(bot.target==first);}
 challenger->world.pos={actor->world.pos.x-80,1,0};bot.thinkTimer=0;
 UpdateUnit(bot,play.get());CHECK(bot.target==challenger);
});
Group("knockback moves only its bot and stops at an unsafe floor",[]{
 Reset();Actor* actor=New(true,{-105,1,0});Unit& bot=*Find(actor);
 const Vec3f playerStart=player->actor.world.pos;bot.knockbackVelocity={4,0,0};bot.knockbackTimer=6;
 pit=true;UpdateUnit(bot,play.get());CHECK(actor->world.pos.x>-105);
 for(int i=0;i<6;++i)UpdateUnit(bot,play.get());
 CHECK(actor->world.pos.x<=-100);CHECK(bot.knockbackTimer==0);
 CHECK(DistanceXZ(playerStart,player->actor.world.pos)==0);
});
