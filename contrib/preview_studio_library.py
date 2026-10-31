"""Render the production Library drawing code with fixture data; no game process or installation is used."""
from pathlib import Path
import subprocess,sys,struct,zlib
repo=Path(__file__).resolve().parents[1]
out=(Path(sys.argv[1]) if len(sys.argv)>1 else repo/'build/library-preview').resolve()
out.mkdir(parents=True,exist_ok=True)
surface=(repo/'Extension/UI/Overlay/animation_studio_overlay.cpp').read_text(encoding='utf-8-sig')
rows=(repo/'Extension/AnimationStudio/studio_settings_rows.cpp').read_text(encoding='utf-8-sig')
def function(source,signature):
    start=source.index(signature)
    params=source.index('(',start); depth=1; at=params+1
    while depth:
        depth+=(source[at]=='(')-(source[at]==')'); at+=1
    opening=source.index('{',at)
    depth=1; at=opening+1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}'); at+=1
    return source[start:at]+'\n'
prefix=r'''
#include <imgui.h>
#include "Extension/AnimationStudio/anim_studio.h"
#include "Extension/AnimationStudio/playlist_management.h"
#include "Extension/AnimationStudio/library_navigation.h"
#include "Extension/AnimationStudio/native_asset_names.h"
#include "Extension/AnimationStudio/studio_settings_rows.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace studio=dingosdk::anim_studio;
studio::View fixture;
bool preview_native_stances{};
int copied_stance{-1};
bool preview_accept=true;int preview_ends{},legacy_pack_opens{};std::string opened_save;
studio::ModifierBinding binding{"Specials",1,true};
std::map<std::string,std::vector<studio::PlaylistEntry>> fixture_lists;
void sync_lists(){fixture.playlists.clear();for(const auto& [name,entries]:fixture_lists) fixture.playlists.push_back({name,entries.size()});fixture.playlist=fixture_lists[fixture.playlist_open];}
namespace dingosdk::anim_studio {
bool make_stance_version(const std::string&,int version,std::string&,std::string&){copied_stance=version;return false;}
View view(){return fixture;}
bool preview_saved(const std::string&,std::string&){return preview_accept;}
void end_preview(){++preview_ends;}
ModifierBinding modifier_binding(){return binding;}
void set_modifier_binding(ModifierBinding value){binding=std::move(value);}
void set_play_in_game(bool on){fixture.play_in_game=on;}
void set_pack_favorite(const std::string& name,bool on){for(auto& pack:fixture.packs)if(pack.name==name)pack.favorite=on;}
PlaylistsNow playlists_now(){PlaylistsNow now;now.on=fixture.playlist_playing;for(const auto&[name,entries]:fixture_lists)now.lists.push_back({name,entries.size(),PlaylistMode::shuffle});return now;}
void open_playlist(const std::string& name){fixture.playlist_open=name;sync_lists();}
void play_playlist(const std::string& name,std::string&){fixture.playlist_playing=name;}
void set_playlist_mode(const std::string&,PlaylistMode mode,std::string&){fixture.playlist_mode=mode;}
void playlist_from_modifier_tags(std::string&){}
void playlist_move(std::size_t,int){}
void playlist_remove(std::size_t i){if(i<fixture.playlist.size())fixture_lists[fixture.playlist_open].erase(fixture_lists[fixture.playlist_open].begin()+static_cast<std::ptrdiff_t>(i));sync_lists();}
bool new_playlist(const std::string& wanted,std::string&){auto name=wanted;while(!name.empty()&&name.back()==' ')name.pop_back();while(!name.empty()&&name.front()==' ')name.erase(name.begin());if(name.empty()||fixture_lists.contains(name))return false;fixture_lists[name]={};open_playlist(name);return true;}
bool new_playlist(std::string& m){return new_playlist("New playlist",m);}
bool rename_playlist(const std::string&,std::string&){return true;}
bool delete_playlist(std::string&){return true;}
bool delete_playlist(const std::string& name,std::string&){fixture_lists.erase(name);if(fixture.playlist_playing==name)fixture.playlist_playing.clear();if(binding.playlist==name)binding.playlist.clear();if(fixture.playlist_open==name)fixture.playlist_open=fixture_lists.empty()?"":fixture_lists.begin()->first;sync_lists();return true;}
bool undo_playlist_entries(const std::string&,std::string&){return true;}
bool duplicate_playlist(const std::string&,std::string&){return true;}
bool clear_playlist_entries(const std::string&,std::string&){return true;}
std::vector<PresetPack> packs(){std::vector<PresetPack> p;for(const auto& x:fixture.packs)p.push_back(x.pack);return p;}
bool edit_playlist_member(const std::string& target,const PlaylistEntry& source,bool on,std::string&){bool changed=set_playlist_member(fixture_lists[target],packs(),source,on);sync_lists();return changed;}
bool edit_playlist_pack(const std::string& target,const std::string& pack,bool on,std::string&){bool changed=set_playlist_pack(fixture_lists[target],pack,on);sync_lists();return changed;}
std::vector<std::string> playlists_containing(const std::string& animation){std::vector<std::string> names;for(const auto&[name,entries]:fixture_lists)if(playlist_membership(entries,packs(),animation).included())names.push_back(name);return names;}
std::string add_to_playlist(const std::string& target,const PlaylistEntry& source,std::string&){if(source.animation.empty())set_playlist_pack(fixture_lists[target],source.pack,true);else set_playlist_member(fixture_lists[target],packs(),source,true);sync_lists();return target;}
}
namespace dingosdk::anim_studio::settings_rows {
'''
setting_code=''.join(function(rows,signature) for signature in ['Setting choice(', 'Setting one_of(', 'Setting on_off(', 'Setting modifier_button(', 'Setting modifier_playlist(', 'Setting modifier_native_button('])+'}\n'
ui=r'''
struct Fonts {ImFont *body,*bold,*heading,*title;float k;};
constexpr ImU32 white=IM_COL32(243,243,239,255),soft=IM_COL32(168,170,176,255),faint=IM_COL32(112,114,120,255),blue=IM_COL32(1,131,255,255),blue_light=IM_COL32(90,180,255,255),blue_dim=IM_COL32(1,131,255,110),shop_ink=IM_COL32(11,12,15,255),row_selected=IM_COL32(62,66,74,236),row_fill_alt=IM_COL32(37,38,41,222);
struct Ui {
 studio::LibraryNavigation library;std::optional<studio::LibraryNavigation> preview_library_return;
 std::optional<studio::PlaylistEntry> library_pending_source;
 bool menu{true},pack_page{},menu_after_preview{};int menu_tab{1},preview_return_tab{-1};
 bool preview_return_pack_page{},preview_from_pack{};ImGuiID preview_return_focus{};
 std::string preview_pack,preview_name,preview_market,market_open;
 float library_modal_scroll{};
 std::array<char,64> playlist_name{};std::string playlist_name_for,playlist_for;studio::PlaylistEntry playlist_for_entry;
} preview_ui;
Ui& ui(){return preview_ui;}
ImFont *preview_anton{}, *preview_bold{};
ImFont* sharp(ImFont* f,float){return f==preview_anton?preview_bold:f;}
ImFont* mont(const Fonts& f,float){return f.bold;}
ImFont* anton(const Fonts& f,float){return f.heading;}
ImFont* body(const Fonts& f,float){return f.body;}
std::string upper(std::string x){for(auto& c:x)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));return x;}
void say(const std::string&){}
void make_pack(const studio::View&){}


void open_saved(const std::string& name){opened_save=name;}
void open_pack_page(const std::string&){++legacy_pack_opens;ui().pack_page=true;}
void open_menu(int tab=-1){ui().menu=true;if(tab>=0)ui().menu_tab=tab;}
std::string trick_label(const std::string& trick){return trick=="kickflip"?"Kickflip":trick=="360flip"?"360 Flip":trick=="ollie"?"Ollie":trick=="manual"?"Manual":trick;}
'''
helpers=function(surface,'ImVec2 text_size(')+function(surface,'void text(ImDrawList*')+function(surface,'std::pair<float, float> caps_of(')+function(surface,'unsigned seed_of(')
navstart=surface.index('struct Spot {')
navend=surface.index('// ---- the pieces the pages are made of',navstart)
helpers+=surface[navstart:navend]
helpers+=function(surface,'std::string elide(')+function(surface,'void chosen_outline(')
helpers+='enum class Look { dark, blue, paper, key, danger, ghost };\n'
helpers+=''.join(function(surface,sig) for sig in ['bool action(ImDrawList*','int segmented(', 'bool field(', 'float paragraph(', 'const studio::Preset* preset_named(', 'const studio::View::PackView* pack_named(', 'bool saved_trick(', 'std::string shown_name(', 'std::string listed(', 'void tick_box(', 'void page_heading(', 'void flat_rect('])
helpers+=function(surface,'void preview_trick(')+function(surface,'void open_or_preview(')+function(surface,'void leave_preview(')
helpers+=''.join(function(surface,sig) for sig in ['bool nollie_trick(', 'const char* stance_word(', 'std::array<std::string, 4> stance_versions_of(', 'float stance_versions('])
art=r'''
void pack_cover(ImDrawList*d,const Fonts&f,const studio::View::PackView&p,ImVec2 a,ImVec2 b){
 unsigned seed=0;for(char c:p.name)seed=seed*33+static_cast<unsigned char>(c);
 const ImU32 color=IM_COL32(40+seed%60,55+(seed>>6)%80,80+(seed>>12)%90,255);
 d->AddRectFilled(a,b,color);d->AddQuadFilled(ImVec2(a.x,a.y+(b.y-a.y)*.60f),ImVec2(b.x,a.y+(b.y-a.y)*.20f),ImVec2(b.x,a.y+(b.y-a.y)*.40f),ImVec2(a.x,a.y+(b.y-a.y)*.80f),IM_COL32(1,131,255,210));
 text(d,f.heading,36*f.k,ImVec2(a.x+12*f.k,a.y+24*f.k),white,p.name.substr(0,2));
}
std::pair<ImVec2,ImVec2> card_box(ImDrawList*d,const Fonts&f,ImVec2 display,float w,float h,const std::string& title,ImU32 = blue){
 const ImVec2 a((display.x-w)*.5f,(display.y-h)*.5f),b(a.x+w,a.y+h);d->AddRectFilled(a,b,IM_COL32(14,15,18,255));d->AddRectFilled(a,{b.x,a.y+4*f.k},blue);text(d,f.heading,28*f.k,{a.x+26*f.k,a.y+24*f.k},white,title);return {a,b};
}
'''
renderer=r'''
void render_image(const std::filesystem::path& path,int width,int height,unsigned char* atlas,int aw,int ah){
 std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*3,22);
 const auto edge=[](ImVec2 a,ImVec2 b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);};
 const auto* data=ImGui::GetDrawData();
 for(int li=0;li<data->CmdListsCount;++li){const auto* list=data->CmdLists[li];for(const auto& cmd:list->CmdBuffer){
  if(cmd.UserCallback)continue;
  for(unsigned i=0;i+2<cmd.ElemCount;i+=3){
   const auto& a=list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset+i]+cmd.VtxOffset];const auto& b=list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset+i+1]+cmd.VtxOffset];const auto& c=list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset+i+2]+cmd.VtxOffset];
   const float area=edge(b.pos,c.pos,a.pos.x,a.pos.y);if(std::abs(area)<.00001f)continue;
   int x0=std::max(0,static_cast<int>(std::floor(std::max(cmd.ClipRect.x,std::min({a.pos.x,b.pos.x,c.pos.x}))))),x1=std::min(width-1,static_cast<int>(std::ceil(std::min(cmd.ClipRect.z,std::max({a.pos.x,b.pos.x,c.pos.x})))));
   int y0=std::max(0,static_cast<int>(std::floor(std::max(cmd.ClipRect.y,std::min({a.pos.y,b.pos.y,c.pos.y}))))),y1=std::min(height-1,static_cast<int>(std::ceil(std::min(cmd.ClipRect.w,std::max({a.pos.y,b.pos.y,c.pos.y})))));
   for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const float w0=edge(b.pos,c.pos,x+.5f,y+.5f)/area,w1=edge(c.pos,a.pos,x+.5f,y+.5f)/area,w2=1-w0-w1;if(w0<0||w1<0||w2<0)continue;
    const int u=std::clamp(static_cast<int>((a.uv.x*w0+b.uv.x*w1+c.uv.x*w2)*aw),0,aw-1),v=std::clamp(static_cast<int>((a.uv.y*w0+b.uv.y*w1+c.uv.y*w2)*ah),0,ah-1);const auto* tex=atlas+(static_cast<std::size_t>(v)*aw+u)*4;
    const auto color=[&](int shift){return static_cast<float>((a.col>>shift)&255)*w0+static_cast<float>((b.col>>shift)&255)*w1+static_cast<float>((c.col>>shift)&255)*w2;};
    const float alpha=color(24)/255*tex[3]/255;auto* dst=pixels.data()+(static_cast<std::size_t>(y)*width+x)*3;
    for(int channel=0;channel<3;++channel)dst[channel]=static_cast<unsigned char>(std::clamp(color(channel*8)*tex[channel]/255*alpha+dst[channel]*(1-alpha),0.f,255.f));
   }
  }
 }}
 std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
}
int main(int argc,char**argv){
 const std::filesystem::path root=argc>1?argv[1]:".";
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;io.DisplaySize={1920,1080};io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
 const std::string fonts=std::string(SOURCE_ROOT)+"/External/fonts/";
 Fonts f{io.Fonts->AddFontFromFileTTF((fonts+"Montserrat-SemiBold.ttf").c_str(),15),io.Fonts->AddFontFromFileTTF((fonts+"Montserrat-ExtraBold.ttf").c_str(),15),io.Fonts->AddFontFromFileTTF((fonts+"Anton-Regular.ttf").c_str(),32),nullptr,1};f.title=f.heading;preview_anton=f.heading;preview_bold=f.bold;
 unsigned char* atlas{};int aw{},ah{};io.Fonts->GetTexDataAsRGBA32(&atlas,&aw,&ah);io.Fonts->SetTexID(1);
 fixture.playlist_open="Specials";fixture.playlist_playing="Everyday";fixture.playlist_mode=studio::PlaylistMode::shuffle;fixture.modifier_ready=true;fixture.play_in_game=true;
 const std::array<std::string,5> pack_names{"Street Essentials","Boned & Beyond","Rail Theory","One Foot Sessions","Flow State"};
 for(std::size_t pi=0;pi<pack_names.size();++pi){studio::View::PackView pack;pack.name=pack_names[pi];pack.made_by=pi%2?"heatz":"fourth";pack.pack.name=pack.name;pack.installed=pi%2==0;
  for(int t=0;t<9;++t){studio::Preset p;p.name="Animation "+std::to_string(pi*9+static_cast<std::size_t>(t));p.label=t%3==0?"Dolphin Flip":t%3==1?"Boned Ollie":"Steezy Tre Flip";p.trick=t%3==0?"kickflip":t%3==1?"ollie":"360flip";p.pop=t<3?"low":t<6?"mid":"high";p.stance="regular";p.foreign=pi%2==0;p.modifier_hint=pi==1;p.label+=" "+std::to_string(t);fixture.library.push_back(p.name);fixture.presets.push_back(p);pack.members.push_back(p.name);pack.pack.slots[p.trick+"|"+p.pop].push_back(p.name);}
  fixture.packs.push_back(pack);
 }
 fixture_lists["Everyday"]={{"Street Essentials",""},{"Rail Theory",""}};fixture_lists["Specials"]={{"Boned & Beyond",""},{"","Animation 1"}};fixture_lists["Filming sessions"]={{"One Foot Sessions",""}};fixture_lists["A playlist with a much longer name"]={};sync_lists();
 const auto frame=[&](std::string name,int width,int height,std::function<void()> before=[]{}){io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};f.k=std::min(height/1080.f,width/1700.f);ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);ImGui::Begin("fixture",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings);nav_frame();before();
  auto*d=ImGui::GetWindowDrawList();for(int i=0;i<20;++i)d->AddLine({0.f,static_cast<float>(i*90)},{static_cast<float>(width),static_cast<float>(i*90-350)},IM_COL32(32,34,39,255),30);
  text(d,f.bold,18*f.k,{48*f.k,40*f.k},soft,"TRICKS       LIBRARY       MARKET       SETTINGS       ABOUT");
  const auto view=fixture;const auto began=std::chrono::steady_clock::now();
  if(preview_native_stances){auto [a,b]=card_box(d,f,io.DisplaySize,820*f.k,280*f.k,"NATIVE ANIMATION / STANCES");
   text(d,f.bold,18*f.k,{a.x+26*f.k,a.y+86*f.k},white,"Manual / Normal and switch versions");
   stance_versions(d,f,view,"Native manual",{a.x+26*f.k,a.y+134*f.k},760*f.k);
   paragraph(d,f.bold,14*f.k,{a.x+26*f.k,a.y+194*f.k},760*f.k,soft,"Create a normal and a switch version. Fakie uses a separate native animation. Each version can go in your normal or modifier playlist.");
  }else{library_workspace(d,f,view,{48*f.k,124*f.k},{width-48*f.k,height-76*f.k});if(!ui().playlist_for.empty())playlist_card(d,f,view,io.DisplaySize);}
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();nav_end_frame();ImGui::End();ImGui::Render();render_image(root/(name+".ppm"),width,height,atlas,aw,ah);std::cout<<name<<" rendered (UI "<<elapsed<<"ms, "<<ImGui::GetDrawData()->TotalVtxCount<<" vertices)\n";
 };

 const auto press=[&](std::initializer_list<const char*> scopes,const char* id){for(auto scope:scopes)ImGui::PushID(scope);nav().focus=ImGui::GetID(id);for(std::size_t i=0;i<scopes.size();++i)ImGui::PopID();nav().keys=true;nav().press=true;};
 const auto numeric=[&](int id,const char* button){ImGui::PushID(id);nav().focus=ImGui::GetID(button);ImGui::PopID();nav().keys=true;nav().press=true;};
 const auto expect=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
 frame("playlists-1080",1920,1080);frame("playlists-768",1366,768);
 fixture.modifier_ready=false;frame("modifier-inactive",1366,768);fixture.modifier_ready=true;
 fixture.play_in_game=false;frame("playback-paused",1366,768);
 frame("action-resume-playback",1920,1080,[&]{press({},"##resumeplayback");});
 expect(fixture.play_in_game,"Paused playback has a direct Resume action without assigning another playlist");
 const auto normal=fixture_lists["Everyday"];
 frame("action-open-picker",1920,1080,[&]{press({},"##libraryaddanimations");});
 expect(ui().library.adding_to=="Specials"&&fixture.playlist_playing=="Everyday","Adding has an explicit destination and never changes what plays");
 frame("packs-1080",1920,1080);frame("packs-768",1366,768);
 frame("action-browse",1920,1080,[&]{press({"picker","Boned & Beyond"},"##browsepack");});
 expect(ui().library.picker.pack=="Boned & Beyond"&&fixture_lists["Everyday"]==normal,"Opening a pack never adds it or changes the default playlist");
 frame("action-expand",1920,1080,[&]{press({"picker","Kickflip"},"##group");});
 expect(ui().library.picker.expanded.contains("Kickflip"),"Animation groups can be opened");
 frame("choose-pack-tricks",1920,1080);
 frame("action-remove-one",1920,1080,[&]{press({"picker","Kickflip","Boned & Beyond|Animation 9"},"##membership");});
 expect(!studio::playlist_membership(fixture_lists["Specials"],studio::packs(),"Animation 9").included(),"Remove excludes one animation from its destination without changing a source pack");
 expect(fixture.packs[1].pack.has("Animation 9")&&!fixture.packs[1].pack.off.contains("Animation 9")&&fixture_lists["Everyday"]==normal,"Other playlists and source packs stay intact");
 frame("action-restore-one",1920,1080,[&]{press({"picker","Kickflip","Boned & Beyond|Animation 9"},"##membership");});
 expect(studio::playlist_membership(fixture_lists["Specials"],studio::packs(),"Animation 9").included(),"An excluded animation can be added again independently");
 // The real production Preview and Back functions must restore the new Library, not call the legacy pack opener.
 ui().library.picker.expanded={"Kickflip","Ollie","360 Flip"};ui().library.picker.scroll=140;
 frame("preview-bookmark-ready",1920,1080);
 frame("action-preview",1920,1080,[&]{press({"picker","Kickflip","Boned & Beyond|Animation 9"},"##preview");});
 expect(!ui().menu&&ui().preview_library_return.has_value(),"Preview bookmarks its real Library route");
 const auto bookmark=*ui().preview_library_return;const auto focus=ui().preview_return_focus;
 ui().library.show_page(1);leave_preview();
 expect(ui().menu&&ui().menu_tab==1&&!ui().pack_page&&legacy_pack_opens==0&&ui().library==bookmark&&nav().focus==focus,
  "Back from Preview restores the exact picker, source, search, expansions, scroll and focus without opening the old menu");
 const auto before_failure=ui().library;preview_accept=false;preview_trick("Boned & Beyond","Animation 9",true);preview_accept=true;
 expect(ui().menu&&ui().library==before_failure&&!ui().preview_library_return,"A failed preview leaves the current route untouched");
 frame("preview-return",1920,1080);
 frame("action-done-adding",1920,1080,[&]{press({},"##doneadding");});
 expect(!ui().library.adding()&&fixture.playlist_open=="Specials"&&fixture.playlist_playing=="Everyday","Done returns to playlist contents, not playback activation");
 frame("playlist-after-adding",1920,1080);
 ui().library.show_page(1);frame("collection-1080",1920,1080);
 ui().library.collection.source=1;ui().library.collection.expanded={"Kickflip","Ollie"};frame("personal-tricks",1920,1080);
 std::snprintf(ui().library.collection.query.data(),ui().library.collection.query.size(),"Dolphin");frame("action-search",1920,1080);
 expect(ui().library.collection.expanded.contains("Kickflip"),"Search reveals matching groups");
 frame("action-collapse-search",1920,1080,[&]{press({"collection"},"##collapseall");});
 frame("action-stay-collapsed",1920,1080);
 expect(ui().library.collection.expanded.empty(),"Collapsed search results remain collapsed");
 frame("action-collection-open-group",1920,1080,[&]{press({"collection","Kickflip"},"##group");});
 fixture.presets[9].locked=true;ui().library.collection.source=2;
 frame("action-locked-view",1920,1080,[&]{press({"collection","Kickflip","|Animation 9"},"##edit");});
 expect(!ui().menu&&ui().preview_library_return&&ui().preview_library_return->page==1,"Viewing a locked animation also bookmarks Collection");
 leave_preview();fixture.presets[9].locked=false;
 expect(ui().library.page==1&&std::string(ui().library.collection.query.data())=="Dolphin"&&legacy_pack_opens==0,"Locked preview returns to the same Collection search");
 ui().library.modifier_setup=true;ui().library.settings_scroll=0;frame("modifier-setup",1920,1080);frame("modifier-setup-768",1366,768);
 fixture.modifier_ready=false;frame("modifier-setup-inactive",1366,768);fixture.modifier_ready=true;
 const auto safe=fixture_lists["Specials"];
 frame("action-help-blocks-browser",1920,1080,[&]{press({"collection","Kickflip","|Animation 9"},"##membership");});
 expect(fixture_lists["Specials"]==safe&&ui().playlist_for.empty(),"Hidden collection controls cannot be activated through setup");
 ui().library.modifier_setup=false;ui().library.page=0;ui().library.confirmation="delete:Specials";frame("delete-confirm",1920,1080);
 frame("action-confirm-blocks-content",1920,1080,[&]{press({},"##librarynormalrole");});
 expect(fixture.playlist_playing=="Everyday","A confirmation blocks underlying playback controls");ui().library.confirmation.clear();
 ui().playlist_for="Animation 9";ui().playlist_for_entry={"Boned & Beyond","Animation 9"};ui().library_modal_scroll=0;
 for(int i=0;i<13;++i)fixture_lists["Test playlist "+std::to_string(i)]={};sync_lists();frame("playlist-chooser",1920,1080);
 frame("action-add-to-thirteenth",1920,1080,[&]{press({"Test playlist 12"},"##playlistmembership");});
 expect(studio::playlist_membership(fixture_lists["Test playlist 12"],studio::packs(),"Animation 9").included(),"The chooser reaches more than six playlists");
 frame("action-remove-from-existing",1920,1080,[&]{press({"Specials"},"##playlistmembership");});
 expect(!studio::playlist_membership(fixture_lists["Specials"],studio::packs(),"Animation 9").included()&&studio::playlist_membership(fixture_lists["Test playlist 12"],studio::packs(),"Animation 9").included(),"Removing here preserves membership elsewhere");
 ui().playlist_for="Boned & Beyond";ui().playlist_for_entry={"Boned & Beyond",""};
 frame("action-add-pack-via-chooser",1920,1080,[&]{press({"Test playlist 11"},"##playlistmembership");});
 expect(studio::playlist_has_pack(fixture_lists["Test playlist 11"],"Boned & Beyond"),"A Collection pack can be assigned without opening an old pack page");
 ui().playlist_for="Animation 9";ui().playlist_for_entry={"Boned & Beyond","Animation 9"};
 frame("action-new-from-chooser",1920,1080,[&]{press({},"##newdestination");});
 expect(ui().library.creating&&ui().library_pending_source,"A new destination asks for its name before creating or adding anything");
 std::snprintf(ui().library.new_name.data(),ui().library.new_name.size(),"Street session");
 frame("create-playlist",1920,1080);
 frame("action-create-named",1920,1080,[&]{press({},"##createnamedplaylist");});
 expect(fixture.playlist_open=="Street session"&&studio::playlist_membership(fixture_lists["Street session"],studio::packs(),"Animation 9").included()&&fixture.playlist_playing=="Everyday","A named destination gets only the chosen animation and stays inactive");
 fixture.playlist_open="Specials";sync_lists();ui().library.select("Specials");ui().library.add_to("Specials","Boned & Beyond");ui().library.picker.expanded={"Kickflip"};ui().library.picker.query.fill(0);ui().library.picker.seen_query.clear();fixture.presets[9].available=false;
 frame("missing-clip",1920,1080,[&]{press({"picker","Kickflip","Boned & Beyond|Animation 9"},"##membership");});
 expect(!studio::playlist_membership(fixture_lists["Specials"],studio::packs(),"Animation 9").included(),"Missing clips cannot be added through disabled controls");
 for(int pi=0;pi<40;++pi){studio::View::PackView pack;pack.name="Large pack "+std::to_string(pi);pack.pack.name=pack.name;
  for(int ti=0;ti<80;++ti){studio::Preset p;p.name="Large save "+std::to_string(pi)+" "+std::to_string(ti);p.label="Saved variation "+std::to_string(ti);p.trick=ti%3==0?"kickflip":ti%3==1?"ollie":"360flip";p.pop="mid";p.stance="regular";fixture.presets.push_back(p);fixture.library.push_back(p.name);pack.members.push_back(p.name);pack.pack.slots[p.trick].push_back(p.name);}fixture.packs.push_back(pack);
 }
 ui().library.show_page(1);ui().library.collection={};ui().library.collection.source=1;ui().library.collection.expand_all=true;frame("large-collection-3200",1920,1080);
 fixture_lists.clear();fixture.packs.clear();fixture.presets.clear();fixture.library.clear();fixture.playlist_open.clear();fixture.playlist_playing.clear();binding={};sync_lists();fixture_lists.clear();fixture.playlists.clear();fixture.playlist.clear();ui().library={};
 frame("empty-library",1920,1080);frame("empty-library-768",1366,768);
 studio::Preset native;native.name="Native manual";native.trick="native:manual";native.stance="regular";fixture.presets.push_back(native);fixture.library.push_back(native.name);fixture.own_lead_right=false;preview_native_stances=true;
 frame("native-stances",1920,1080);
 frame("action-native-switch",1920,1080,[&]{numeric(2,"##stance");});expect(copied_stance==2,"Native switch copies remain accessible");copied_stance=-1;
 frame("action-native-no-fakie",1366,768,[&]{numeric(1,"##stance");});expect(copied_stance==-1,"Native pieces hide invalid whole-trick fakie controls");
 std::cout<<"Library navigation and playlist interaction checks passed\n";
 ImGui::DestroyContext();return 0;
}
'''
cpp=out/'preview.cpp'
cpp.write_text(prefix+setting_code+ui+helpers+art+'\n#include "Extension/UI/Overlay/studio_library_workspace.inl"\n'+function(surface,'void playlist_card(')+renderer,encoding='utf-8')
build=repo/'build/ci-windows/Release'
command=['cl','/nologo','/std:c++20','/EHsc','/O2','/W3',f'/DSOURCE_ROOT="{repo.as_posix()}"',f'/I{repo}',f'/I{repo}/External/imgui',f'/I{repo}/External/rapidjson/include',str(cpp),f'/Fe:{out / "preview.exe"}',f'/Fo:{out / "preview.obj"}',str(build/'dingosdk_imgui.lib'),str(build/'dingosdk_json.lib'),'user32.lib','gdi32.lib','imm32.lib','dwmapi.lib','d3d12.lib','dxgi.lib','d3dcompiler.lib']
r=subprocess.run(command,cwd=out,capture_output=True,text=True)
print(r.stdout[-6000:],r.stderr[-1000:])
if r.returncode:sys.exit(r.returncode)
r=subprocess.run([str(out/'preview.exe'),str(out)],cwd=out)
if r.returncode:sys.exit(r.returncode)
def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
for p in out.glob('*.ppm'):
    with p.open('rb') as f:
        assert f.readline()==b'P6\n';w,h=map(int,f.readline().split());assert f.readline()==b'255\n';data=f.read()
    scan=b''.join(b'\0'+data[y*w*3:(y+1)*w*3] for y in range(h))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
    p.with_suffix('.png').write_bytes(png)
print('Previews:',out)
