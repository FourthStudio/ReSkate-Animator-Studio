"""Exercise production native-playback selection with controlled pose scores, without opening the game.

Run from a Visual Studio developer shell. The ordinary animation_studio CTest suite covers pose matching;
this checks the actual runtime's route choice, clock continuity and variant cycling around those scores.
"""
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
source = (repo / 'Extension/AnimationStudio/anim_studio.cpp').read_text(encoding='utf-8-sig')
start = source.index('PieceMatch native_variant(')
end = source.index('// A grab of the game', start)
production = source[start:end]
fixture = r'''
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "Extension/AnimationStudio/held_loop_selection.h"
#include "Extension/AnimationStudio/native_modifier_selection.h"
#include "Extension/AnimationStudio/anim_runtime_rules.h"
#include "Extension/AnimationStudio/grab_types.h"
using namespace dingosdk::anim_studio;
namespace logging { enum class Level{info};enum class Channel{skater};template<class...T>void log(T&&...){} }
using logging::Level;using logging::Channel;
enum class PlaylistMode{fixed,turns,shuffle};
struct Clip {float score{};bool mirrored{};float duration()const{return 2;}};
struct PieceMatch{std::size_t piece{};bool mirrored{};float time{},score{};};
struct HeldPose{std::size_t piece{};bool mirrored{};float time{},score{};};
std::vector<HeldPose> held_pose_fits(const std::vector<const Clip*>& clips,const std::vector<int>&,bool){
 std::vector<HeldPose> fits;for(std::size_t i=0;i<clips.size();++i)fits.push_back({i,clips[i]->mirrored,0,clips[i]->score});
 std::stable_sort(fits.begin(),fits.end(),[](const auto&a,const auto&b){return a.score<b.score;});return fits;
}
struct CustomTrick{std::string trick,name;std::shared_ptr<const Clip> clip;bool modifier_route{},leads_right{};int order{};PlaylistMode route_mode{PlaylistMode::turns};};
struct Snapshot{bool front_foot{true};};
struct Live{
 enum class HeldKind{none,ground,grind,grab};
 struct Held{int playing{-1},candidate{-1};bool mirrored{};float time{},better_for{};double off_since{-1};} held;
 struct PiecePlay{const CustomTrick*trick{};bool mirrored{};float time{},weight{};} piece_play;
 struct Manual{int manual{};bool fakie{};};
 std::vector<Manual>piece_manuals;int manual_now{1};std::optional<bool>fakie_now{false};
 std::vector<const CustomTrick*>piece_tricks;std::vector<std::pair<std::size_t,HeldKind>>held_loops;
 std::vector<int>pose;std::map<std::string,std::size_t>variant_counts;NativeModifierHold native_modifier;
};
void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
'''
tests = r'''
int main(){try{
 Snapshot snap;
 auto plain=std::make_shared<Clip>(),mirror=std::make_shared<Clip>();mirror->mirrored=true;
 CustomTrick normal{"native:manual","normal",plain,false,false,0},special{"native:manual","special",plain,true,false,1},alternate{"native:manual","alternate",plain,true,false,2};
 Live l;l.piece_tricks={&normal,&special,&alternate};l.piece_manuals={{1,false},{1,false},{1,false}};
 l.held_loops={{0,Live::HeldKind::ground},{1,Live::HeldKind::ground},{2,Live::HeldKind::ground}};
 l.native_modifier.observe(2,1,true,true,false);
 auto first=manual_play(l,snap,.016f);check(first&&first->piece==1,"Modifier manual starts from its playlist");
 l.native_modifier.observe(2,1,false,true,false);
 auto keep=manual_play(l,snap,.2f);check(keep&&keep->piece==1&&keep->time>.19f,"Releasing the button preserves the native hold and its clock");
 l.manual_now=0;check(!manual_play(l,snap,.016f),"Leaving the manual releases its loop");l.native_modifier.observe(0,0,false,true,false);
 l.manual_now=1;l.native_modifier.observe(2,1,false,true,false);
 auto regular=manual_play(l,snap,.016f);check(regular&&regular->piece==0,"Next unmodified manual uses the normal playlist");
 l.manual_now=0;manual_play(l,snap,.016f);l.native_modifier.observe(0,0,false,true,false);
 l.manual_now=1;l.native_modifier.observe(2,1,true,true,false);
 auto second=manual_play(l,snap,.016f);check(second&&second->piece==2,"In turns advances modifier variants independently of normal attempts");
 // Exact stance must reject the special route without restarting a valid normal fallback every frame.
 l.held={};special.clip=mirror;alternate.clip=mirror;snap.front_foot=false;
 auto fallback=manual_play(l,snap,.016f);auto fallback_keep=manual_play(l,snap,.2f);
 check(fallback&&fallback->piece==0&&fallback_keep&&fallback_keep->time>.19f,"An ineligible modifier stance falls back and keeps normal time");
 snap.front_foot=true;
 l.piece_manuals={{1,false},{2,false},{2,false}};l.held={};
 auto wrong_manual=manual_play(l,snap,.016f);check(wrong_manual&&wrong_manual->piece==0,"A nose-manual modifier cannot replace a tail manual");
 // The native grab's style still outranks an already running fallback, in either route.
 CustomTrick melon{"native:melon","melon",plain,false,false,0},japan{"native:japan","japan",plain,false,false,1};
 Live grab;grab.piece_tricks={&melon,&japan};grab.held_loops={{0,Live::HeldKind::grab},{1,Live::HeldKind::grab}};
 const GrabLoop loops[]={grab_loop_of("c_proto_onb_inair_bs_grab_melon_mid_loop"),grab_loop_of("c_proto_onb_inair_bs_grab_japan_mid_loop")};
 int style=4;const auto rank=[&](std::size_t i){return grab_loop_rank(loops[i],2,style,nullptr);};
 auto old_style=said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"melon","left");style=3;
 auto flicker=said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"japan","left");
 check(old_style&&old_style->piece==0&&flicker&&flicker->piece==0&&flicker->time>0,"A single better-style frame keeps the running fallback and its clock");
 style=4;said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"melon","left");
 check(grab.held.better_for==0,"Returning to the current style resets the better-style debounce");style=3;
 for(int i=0;i<8;++i)said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"japan","left");
 check(grab.held.playing==1,"A stable native grab style changes within the same hold after the debounce");
 grab.held={};japan.clip=mirror;snap.front_foot=false;style=4;
 said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"melon","left");style=3;
 for(int i=0;i<16;++i)said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"japan","left");
 check(grab.held.playing==0&&grab.held.time>.24f,"A better loop that fits only mirrored cannot interrupt an exact-stance fallback");
 const auto not_named=[](std::size_t){return false;};
 const auto unknown_rank=[](std::size_t){return -1;};
 auto unnamed=said_loop(grab,snap,.2f,Live::HeldKind::grab,unknown_rank,"one foot","left",not_named);
 check(unnamed&&unnamed->piece==0&&unnamed->time>.4f,"A pose-selected unnamed grab keeps its loop despite having no native rank");
 snap.front_foot=true;japan.clip=plain;
 melon.modifier_route=japan.modifier_route=true;grab.held={};grab.native_modifier.observe(1,5,true,true,false);style=4;
 said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"melon","left");style=3;
 for(int i=0;i<8;++i)said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"japan","left");
 check(grab.held.playing==1,"The same stable-style handoff works inside the modifier playlist");
 melon.modifier_route=japan.modifier_route=false;
 // Choosing a moving source only once cannot lock its first variant forever across later attempts.
 special.clip=plain;alternate.clip=plain;l.piece_play={};l.variant_counts.clear();snap.front_foot=true;
 auto moving=native_piece_version(l,snap,{0,false,1,0},true);check(moving&&moving->piece==1,"A recognised native piece selects its modifier version");
 l.piece_play={l.piece_tricks[moving->piece],false,1,1};
 auto same=native_piece_version(l,snap,{0,false,1.1f,0},true);check(same&&same->piece==moving->piece,"The moving piece keeps its running variant");
 auto restart=native_piece_version(l,snap,{0,false,.1f,0},true);check(restart&&restart->piece==2,"A new occurrence can advance to the next native variant");
 for(const char* asset:{"native:manual","native:grind","native:grab","native:invert","native:fingerflip","native:transition"}){
  CustomTrick own{asset,"own",mirror,false,false,0},sw{asset,"switch",plain,false,true,1},
      mod{asset,"modifier-own",mirror,true,false,2},modsw{asset,"modifier-switch",plain,true,true,3},modsw2{asset,"modifier-switch-2",plain,true,true,4};
  Live native;native.piece_tricks={&own,&sw,&mod,&modsw,&modsw2};
  auto right=native_piece_version(native,snap,{0,true,.3f,0},false);
  check(right&&right->piece==1&&!right->mirrored,"A native normal route uses its authored switch save before a mirrored normal save");
  auto special_right=native_piece_version(native,snap,{0,true,.3f,0},true);
  check(special_right&&special_right->piece==3&&!special_right->mirrored,"Switch chooses an exact modifier save within the modifier route");
  auto special_left=native_piece_version(native,snap,{0,false,.3f,0},true);
  check(special_left&&special_left->piece==2&&!special_left->mirrored,"The next opposite-foot move uses its own normal-foot save");
  auto next_right=native_piece_version(native,snap,{0,true,.3f,0},true);
  check(next_right&&next_right->piece==4,"Normal-foot and switch modifier variants have independent In-turns counters");
  native.piece_play={&modsw2,false,.3f,1};
  auto running=native_piece_version(native,snap,{0,true,.5f,0},true);
  check(running&&running->piece==4&&running->time==.5f,"The chosen switch variant keeps its time through the move");
  native.piece_play={};native.piece_tricks={&own,&sw,&mod};snap.front_foot=false;
  auto missing=native_piece_version(native,snap,{0,true,.3f,0},true);
  check(missing&&missing->piece==1,"Exact stance falls back to the normal switch save when the modifier only has the other foot");
  snap.front_foot=true;
  auto mirrored_special=native_piece_version(native,snap,{0,true,.3f,0},true);
  check(mirrored_special&&mirrored_special->piece==2&&mirrored_special->mirrored,"Front-foot mode may mirror the modifier when no authored switch modifier exists");
  native.piece_tricks={&own,&sw,&mod,&modsw,&modsw2};native.held_loops={{0,Live::HeldKind::grind},{1,Live::HeldKind::grind},{2,Live::HeldKind::grind},{3,Live::HeldKind::grind},{4,Live::HeldKind::grind}};
  native.native_modifier.observe(1,12,true,true,false);
  auto loop=said_loop(native,snap,.016f,Live::HeldKind::grind,[](std::size_t){return 0;},"grind","left");
  check(loop&&loop->piece>=3&&!loop->mirrored,"A held native loop also prefers the authored switch version within its modifier route");
 }
 std::cout<<"Production native modifiers: manual/grab matching, fallback, clocks, switch routes, style changes and independent variants passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
'''
out = repo / 'build' / 'native-modifier-check'
out.mkdir(parents=True, exist_ok=True)
cpp = out / 'check.cpp'
cpp.write_text(fixture + production + tests, encoding='utf-8')
result = subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/DNOMINMAX', f'/I{repo}', str(cpp),
                         f'/Fe:{out / "check.exe"}', f'/Fo:{out / "check.obj"}'], cwd=out, capture_output=True, text=True)
print(result.stdout, result.stderr)
if result.returncode:
    sys.exit(result.returncode)
sys.exit(subprocess.run([str(out / 'check.exe')], cwd=out).returncode)
