"""Compile the production Steam-front hook against a cached-pad provider. Requires a VS developer shell."""
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
source = (repo / 'Extension/UI/Overlay/input_capture.cpp').read_text(encoding='utf-8-sig')
start = source.index('template<std::size_t I>\nDWORD WINAPI captured_front(')
end = source.index('\nvoid keep_xinput_capture_first()', start)
front = source[start:end]
fixture = r'''
#include <windows.h>
#include <Xinput.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#if __has_include("Extension/UI/Overlay/polled_gamepad.h")
#include "Extension/UI/Overlay/polled_gamepad.h"
using dingosdk::overlay::detail::finish_gamepad_read;
#endif
unsigned overlay_input_access{};
struct OverlayInputAccess { OverlayInputAccess(){++overlay_input_access;} ~OverlayInputAccess(){--overlay_input_access;} };
std::atomic<unsigned> prompt_xinput{};
std::atomic<std::uint16_t> mode_hidden_buttons{};
std::uint16_t modifier_mask = 0x0200;
bool menu_capture{}, full_capture{};
bool block_polled_input(){return !overlay_input_access && full_capture;}
bool suppress_challenge_menu(){return !overlay_input_access && menu_capture;}
std::uint16_t modifier_hidden_buttons(){return overlay_input_access ? 0 : modifier_mask;}
std::uint16_t game_hidden_buttons(){return overlay_input_access ? 0 : static_cast<std::uint16_t>(mode_hidden_buttons.load() | modifier_hidden_buttons() | (suppress_challenge_menu()?XINPUT_GAMEPAD_START:0));}
void note_prompt_read(std::atomic<unsigned>&){}
void note_xinput(DWORD,const XINPUT_STATE&){}
bool scripted_pad(DWORD,XINPUT_STATE&){return false;}
void game_pad_read(DWORD,XINPUT_STATE*){}
namespace game { inline WORD owned_menu_buttons(WORD buttons,bool on){return on ? static_cast<WORD>(buttons & ~XINPUT_GAMEPAD_START):buttons;} }
struct Hooks { std::array<int,8> xinput_front{}; } hooks;
Hooks& input_hooks(){return hooks;}
DWORD WINAPI steam_cached(DWORD,XINPUT_STATE* out){
    *out={};out->dwPacketNumber=7;out->Gamepad.wButtons=XINPUT_GAMEPAD_RIGHT_SHOULDER|XINPUT_GAMEPAD_A;
    out->Gamepad.sThumbRX=12000;out->Gamepad.bLeftTrigger=91;return ERROR_SUCCESS;
}
template<class Function> Function original(int){return &steam_cached;}
'''
tests = r'''
int main(){
    XINPUT_STATE out{};
    captured_front<0>(0,&out);
    std::cout << "steam front game buttons=" << std::hex << out.Gamepad.wButtons << "\n";
    if(out.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) return 1;
    if(!(out.Gamepad.wButtons & XINPUT_GAMEPAD_A) || out.Gamepad.sThumbRX!=12000 || out.Gamepad.bLeftTrigger!=91) return 2;
    {OverlayInputAccess access;captured_front<0>(0,&out);}
    if(!(out.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER)) return 3;
    modifier_mask=0;captured_front<0>(0,&out);
    if(!(out.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER)) return 4;
    return 0;
}
'''
out = repo / 'build' / 'modifier-steam-repro'
out.mkdir(parents=True, exist_ok=True)
cpp = out / 'repro.cpp'
cpp.write_text(fixture + front + tests, encoding='utf-8')
result = subprocess.run(['cl', '/nologo','/std:c++20','/EHsc','/W4','/WX',f'/I{repo}',str(cpp),f'/Fe:{out / "repro.exe"}',f'/Fo:{out / "repro.obj"}'], cwd=out, capture_output=True,text=True)
print(result.stdout, result.stderr)
if result.returncode: sys.exit(result.returncode)
sys.exit(subprocess.run([str(out / 'repro.exe')],cwd=out).returncode)
