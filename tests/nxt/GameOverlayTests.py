#!/usr/bin/env python3
"""Compile and exercise the real DrawGame body against a renderer-boundary fixture.

No ImGui/SDL installation or game assets are required. This catches the direct
framebuffer regression and verifies the actual callback/bounds code, rather than
reimplementing it in the test. It does not validate pixels or GPU drivers.
Standalone MSVC use requires a Visual Studio developer shell. --emit-source
generates one C++ fixture for the project's normal CMake compiler environment.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r'''
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
struct ImVec2 { float x,y; ImVec2(float a=0,float b=0):x(a),y(b){} };
struct ImVec4 { ImVec4(float,float,float,float){} };
using ImTextureID=void*;
using ImGuiWindowFlags=int;
enum {ImGuiStyleVar_WindowPadding,ImGuiStyleVar_WindowBorderSize,ImGuiStyleVar_ChildBorderSize,
      ImGuiCol_ChildBg,ImGuiWindowFlags_NoTitleBar=1,ImGuiWindowFlags_NoCollapse=2,
      ImGuiWindowFlags_NoResize=4,ImGuiWindowFlags_NoMove=8,ImGuiWindowFlags_NoBackground=16};
#define CVAR_LOW_RES_MODE "low"
#define CVAR_PREFIX_ADVANCED_RESOLUTION "advanced"
namespace Fixture {
ImVec2 position{100,50}, available{1280,720}, cursor;
std::vector<std::string> order;
bool inside=false;
int checks=0;
void Check(bool value,const char* what) {
 ++checks; if(!value)throw std::runtime_error(what);
}
void Near(float a,float b) { Check(std::fabs(a-b)<0.01f,"current image bounds/aspect"); }
}
namespace ImGui {
void PushStyleVar(int,ImVec2){} void PushStyleVar(int,float){} void PushStyleColor(int,ImVec4){}
void Begin(const char*,void*,int){ Fixture::inside=true; }
void End(){ Fixture::inside=false; }
void PopStyleVar(int){} void PopStyleColor(){}
ImVec2 GetWindowPos(){return Fixture::position;}
ImVec2 GetContentRegionAvail(){return Fixture::available;}
void SetCursorPos(ImVec2 p){Fixture::cursor=p;}
ImVec2 GetCursorScreenPos(){return {Fixture::position.x+Fixture::cursor.x,Fixture::position.y+Fixture::cursor.y};}
void Image(ImTextureID id,ImVec2){Fixture::Check(id!=nullptr,"never submit a null image");Fixture::order.push_back("image");}
}
namespace Ship {
struct Variables {
 std::map<std::string,int> values;
 int GetInteger(const char* key,int fallback){auto i=values.find(key);return i==values.end()?fallback:i->second;}
};
struct Window { uintptr_t framebuffer=0; uintptr_t GetGfxFrameBuffer(){return framebuffer;} };
struct Context {
 Variables vars; Window window;
 static Context* GetRawInstance(){static Context context;return &context;}
 Variables* GetConsoleVariables(){return &vars;} Window* GetWindow(){return &window;}
};
}
namespace Fast {
struct Interpreter {struct {uint32_t width=1280,height=720;} mCurDimensions;};
struct GameOverlay {void Draw(){Fixture::order.push_back("existing overlay");}};
struct Fast3dGui {
 std::weak_ptr<Interpreter> mInterpreter;
 GameOverlay baseOverlay;
 std::vector<std::function<void()>> mGameImageOverlays;
 ImVec2 mGameImagePosition,mGameImageSize;
 float mGameImageAspect=0;
 int integerFactor=2;
 GameOverlay* GetGameOverlay(){return &baseOverlay;}
 int GetIntegerScaleFactor(){return integerFactor;}
 void DrawGame();
};
'''

SUFFIX = r'''
}
int main() {
 try {
  Fast::Fast3dGui gui;
  auto interpreter=std::make_shared<Fast::Interpreter>();gui.mInterpreter=interpreter;
  auto* context=Ship::Context::GetRawInstance();
  int draws=0;
  gui.mGameImageOverlays.push_back({}); // Empty callbacks are explicitly supported.
  gui.mGameImageOverlays.push_back([&](){
   Fixture::Check(Fixture::inside,"overlay executes inside current Main Game draw list");
   Fixture::Check(gui.mGameImageSize.x>0&&gui.mGameImageSize.y>0,"bounds exist before callback");
   ++draws;Fixture::order.push_back("nxt overlay");
  });
  auto run=[&](uintptr_t fb,int expected){
   context->window.framebuffer=fb;Fixture::order.clear();const int before=draws;gui.DrawGame();
   Fixture::Check(draws==before+expected,"overlay runs once regardless of framebuffer texture");
   if(expected){
    Fixture::Check(Fixture::order.back()=="nxt overlay","overlay follows optional framebuffer image");
    Fixture::Check(Fixture::order.size()==(fb?3U:2U),"direct path does not submit a texture");
   }
   Fixture::Check(!Fixture::inside,"Main Game window ended");
  };
  // Direct renderer at window resolution, and same-size MSAA resolve, both use fb=0.
  run(0,1);Fixture::Near(gui.mGameImagePosition.x,100);Fixture::Near(gui.mGameImagePosition.y,50);
  Fixture::Near(gui.mGameImageSize.x,1280);Fixture::Near(gui.mGameImageAspect,1280.0f/720);
  run(123,1); // Off-screen framebuffer / resolution scaling.
  Fixture::position={20,80};Fixture::available={960,540};
  interpreter->mCurDimensions={960,540};run(0,1);
  Fixture::Near(gui.mGameImagePosition.x,20);Fixture::Near(gui.mGameImagePosition.y,80);
  Fixture::Near(gui.mGameImageSize.x,960);Fixture::Near(gui.mGameImageSize.y,540);
  // Original N64 aspect centered inside the actual game window.
  context->vars.values["low"]=1;interpreter->mCurDimensions={320,240};run(123,1);
  Fixture::Near(gui.mGameImagePosition.x,140);Fixture::Near(gui.mGameImageSize.x,720);
  Fixture::Near(gui.mGameImageAspect,4.0f/3.0f);
  context->vars.values["low"]=0;context->vars.values["advanced.Enabled"]=1;
  Fixture::available={1280,720};interpreter->mCurDimensions={640,480};run(123,1);
  Fixture::Near(gui.mGameImagePosition.x,180);Fixture::Near(gui.mGameImageSize.x,960);
  Fixture::Near(gui.mGameImageSize.y,720);
  // Letterbox and intentionally stretched images retain source projection aspect.
  Fixture::available={800,800};interpreter->mCurDimensions={640,360};run(123,1);
  Fixture::Near(gui.mGameImagePosition.y,255);Fixture::Near(gui.mGameImageSize.y,450);
  context->vars.values["advanced.IgnoreAspectCorrection"]=1;run(123,1);
  Fixture::Near(gui.mGameImageSize.x,800);Fixture::Near(gui.mGameImageSize.y,800);
  Fixture::Near(gui.mGameImageAspect,16.0f/9.0f);
  context->vars.values["advanced.PixelPerfectMode"]=1;gui.integerFactor=2;
  interpreter->mCurDimensions={320,240};Fixture::available={1280,720};run(123,1);
  Fixture::Near(gui.mGameImagePosition.x,340);Fixture::Near(gui.mGameImagePosition.y,200);
  Fixture::Near(gui.mGameImageSize.x,640);Fixture::Near(gui.mGameImageSize.y,480);
  context->vars.values.clear();Fixture::available={0,0};run(0,0);
  Fixture::Near(gui.mGameImageSize.x,0);Fixture::Near(gui.mGameImageSize.y,0);
  std::cout<<"PASS actual Fast3dGui::DrawGame: "<<Fixture::checks<<" checks (direct, texture, resize, native, aspect, integer, zero viewport)\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
 return 0;
}
'''


LAYOUT_PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
struct ImVec2 {float x,y;ImVec2(float a=0,float b=0):x(a),y(b){}};
struct ImRect {
 ImVec2 Min,Max;
 ImRect(ImVec2 a,ImVec2 b):Min(a),Max(b){}
 float GetWidth()const{return Max.x-Min.x;} float GetHeight()const{return Max.y-Min.y;}
};
struct Vec3f {float x,y,z;};
struct PlayState {struct {float w=2;} viewProjectionMtxF;};
void SkinMatrix_Vec3fMtxFMultXYZW(decltype(PlayState::viewProjectionMtxF)* m,Vec3f* p,Vec3f* out,float* w){*out=*p;*w=m->w;}
std::map<std::string,float> config;
float CVarGetFloat(const char* key,float fallback){auto i=config.find(key);return i==config.end()?fallback:i->second;}
#define NXT_CVAR(name) "gNxt." name
namespace Nxt {
template<class T> T Clamp(T value,T lo,T hi){return (std::min)(hi,(std::max)(lo,value));}
struct View {ImVec2 pos,size;float aspect=4.0f/3.0f;};
'''

LAYOUT_SUFFIX = r'''
}
int main(){
 try{
  using namespace Nxt;
  int checks=0;
  auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("HUD projection/scale/bounds regression");};
  auto near=[&](float a,float b){check(std::fabs(a-b)<0.001f);};
  View native{{10,20},{320,240},4.0f/3.0f};
  float scale=HudScale(native);check(scale>=0.8f);check(scale*360<=296);
  config["gNxt.HudScale"]=1.6f;near(HudScale(native),scale);
  View wide{{40,60},{1920,1080},16.0f/9.0f};near(HudScale(wide),1.6f);
  config["gNxt.HudScale"]=std::numeric_limits<float>::quiet_NaN();near(HudScale(wide),1);
  config.clear();
  for(float offset:{-100.0f,0.0f,24.0f,160.0f,10000.0f,std::numeric_limits<float>::quiet_NaN()}){
   config["gNxt.HudOffset"]=offset;
   for(const auto& view:{native,wide}){
    const float s=HudScale(view);const auto rect=PlayerHudBounds(view,s);
    check(std::isfinite(rect.Min.y));check(rect.Min.x>=view.pos.x);check(rect.Max.x<=view.pos.x+view.size.x);
    check(rect.Min.y>=view.pos.y);check(rect.Max.y<=view.pos.y+view.size.y);
    const auto boss=BossHudBounds(view,s,false),cast=BossHudBounds(view,s,true);
    near(boss.Min.x,cast.Min.x);near(boss.Min.y,cast.Min.y);near(boss.GetWidth(),cast.GetWidth());
    near(cast.GetHeight(),123*s);check(cast.Max.y<=view.pos.y+view.size.y);
   }
  }
  PlayState play;ImVec2 screen;
  check(Project(&play,native,{0,0,0},screen));near(screen.x,170);near(screen.y,140);
  check(Project(&play,native,{2,2,0},screen));near(screen.x,330);near(screen.y,20);
  check(Project(&play,wide,{2,0,0},screen));near(screen.x,1720);near(screen.y,600);
  check(!Project(&play,wide,{100,0,0},screen));
  check(!Project(&play,wide,{std::numeric_limits<float>::quiet_NaN(),0,0},screen));
  play.viewProjectionMtxF.w=-1;check(!Project(&play,wide,{0,0,0},screen));
  play.viewProjectionMtxF.w=std::numeric_limits<float>::quiet_NaN();check(!Project(&play,wide,{0,0,0},screen));
  std::cout<<"PASS actual NxtGui helpers: "<<checks<<" checks (native readability, narrow fit, HUD and cast bounds, widescreen projection)\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
 return 0;
}
'''


def compile_and_run(compiler, contents, directory, name):
    unit = directory / (name + ".cpp")
    unit.write_text(contents)
    binary = directory / (name + (".exe" if os.name == "nt" else ""))
    if Path(compiler[0]).stem.lower() in ("cl", "clang-cl"):
        command = compiler + ["/nologo", "/std:c++17", "/EHsc", str(unit), "/Fe:" + str(binary),
                              "/Fo:" + str(directory / (name + ".obj"))]
    else:
        command = compiler + ["-std=c++17", "-Wall", "-Wextra", "-Wno-unused-but-set-variable",
                              str(unit), "-o", str(binary)]
    subprocess.run(command, cwd=directory, check=True)
    subprocess.run([str(binary)], check=True)


def layout_source(source_root):
    gui_source = (source_root / "soh/soh/Enhancements/NxtRpg/NxtGui.cpp").read_text()
    helpers = "\n".join(function_body(gui_source, signature) for signature in (
        "static float HudScale(", "static bool Project(",
        "static ImRect PlayerHudBounds(", "static ImRect BossHudBounds("))
    return LAYOUT_PREFIX + helpers + LAYOUT_SUFFIX


def combined_source(renderer, layout):
    # Keep each fixture's boundary types isolated, while all standard includes
    # remain at global scope. CMake can compile this as an ordinary single target.
    def split_includes(source):
        includes, lines = [], []
        for line in source.splitlines():
            (includes if line.startswith("#include ") else lines).append(line)
        return includes, "\n".join(lines).replace("int main()", "int Run()")
    a_includes, a_body = split_includes(renderer)
    b_includes, b_body = split_includes(layout)
    includes = "\n".join(sorted(set(a_includes + b_includes)))
    return (includes + "\nnamespace OverlayBoundary {\n" + a_body +
            "\n}\nnamespace LayoutBoundary {\n" + b_body +
            "\n}\nint main(){if(OverlayBoundary::Run()!=0)return 1;return LayoutBoundary::Run();}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compiler", default=os.environ.get("CXX"))
    parser.add_argument("--emit-source", type=Path, help="write one generated C++ fixture and exit without compiling")
    args = parser.parse_args()
    source = (args.source_root / "libultraship/src/fast/Fast3dGui.cpp").read_text()
    body = function_body(source, "void Fast3dGui::DrawGame()")
    renderer = PREFIX + body + SUFFIX
    if args.emit_source:
        args.emit_source.parent.mkdir(parents=True, exist_ok=True)
        args.emit_source.write_text(combined_source(renderer, layout_source(args.source_root)))
        return
    compiler = ([args.compiler] if args.compiler and Path(args.compiler).is_file() else
                shlex.split(args.compiler) if args.compiler else next(
                    ([value] for name in ("c++", "g++", "clang++", "cl") if (value := shutil.which(name))), None))
    if not compiler:
        parser.error("a C++ compiler is required; pass --compiler or set CXX")
    with tempfile.TemporaryDirectory(prefix="nxt-game-overlay-") as directory:
        directory = Path(directory)
        compile_and_run(compiler, renderer, directory, "GameOverlayTests")
        compile_and_run(compiler, layout_source(args.source_root), directory, "HudLayoutTests")


if __name__ == "__main__":
    main()
