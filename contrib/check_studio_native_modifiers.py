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
 struct Held{int playing{-1},candidate{-1};bool mirrored{};float time{};double off_since{-1};} held;
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
 auto new_style=said_loop(grab,snap,.016f,Live::HeldKind::grab,rank,"japan","left");
 check(old_style&&old_style->piece==0&&new_style&&new_style->piece==1,"The native grab style can change during one hold");
 // Choosing a moving source only once cannot lock its first variant forever across later attempts.
 special.clip=plain;alternate.clip=plain;l.piece_play={};l.variant_counts.clear();snap.front_foot=true;
 auto moving=native_piece_version(l,snap,{0,false,1,0},true);check(moving&&moving->piece==1,"A recognised native piece selects its modifier version");
 l.piece_play={l.piece_tricks[moving->piece],false,1,1};
 auto same=native_piece_version(l,snap,{0,false,1.1f,0},true);check(same&&same->piece==moving->piece,"The moving piece keeps its running variant");
 auto restart=native_piece_version(l,snap,{0,false,.1f,0},true);check(restart&&restart->piece==2,"A new occurrence can advance to the next native variant");
 std::cout<<"Production native modifiers: manual/grab matching, fallback, clocks, style changes and independent variants passed\n";
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
