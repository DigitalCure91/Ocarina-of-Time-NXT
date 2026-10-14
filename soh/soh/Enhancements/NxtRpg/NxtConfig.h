#pragma once
#include "NxtWorld.h"
namespace Nxt {
// Narrow configuration boundary keeps simulation independent of UI / JSON / logging libraries.
int ConfigInt(const char* name,int fallback);
float ConfigFloat(const char* name,float fallback);
void ReadOptions();
void Tick();
void OnHealthChange(int16_t amount);
void OnGameLoaded();
void OnWorldDestroyed();
void TrackNative(Actor* actor);
}
