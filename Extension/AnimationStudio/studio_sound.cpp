#include "studio_sound.h"
#include "Engine/Core/Log/logging.h"
#include <Windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace dingosdk::anim_studio {
namespace {
using Microsoft::WRL::ComPtr;
using logging::Channel;
using logging::Level;

struct Sound {
    std::mutex mutex;
    std::condition_variable wake;
    bool wanted{}; // muted, as the studio asks
    bool worker{}; // the thread that does it is running
    // The marker from an earlier run (the game ended with the studio open, its sound muted): -1 not looked for yet, 1 found
    // and not dealt with yet, 0 none. `stale_tries`: how often its sound was looked for (none so early in a start).
    int stale{-1}, stale_tries{};
    std::filesystem::path marker;
    // The sessions muted here, by their instance id, and whether each was muted already (left muted when put back).
    std::map<std::wstring, bool> muted;
};
Sound& sound() {
    static auto* s = new Sound;
    return *s;
}

// Every audio session of this process on every playback device, given to `each` with its instance id.
template <typename Each> void our_sessions(Each&& each) {
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDeviceCollection> outputs;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) ||
        FAILED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &outputs)))
        return;
    UINT device_count{};
    outputs->GetCount(&device_count);
    const DWORD self = GetCurrentProcessId();
    for (UINT d = 0; d < device_count; ++d) {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(outputs->Item(d, &device)) ||
            FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(manager.GetAddressOf()))) ||
            FAILED(manager->GetSessionEnumerator(&sessions)))
            continue;
        int count{};
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> detail;
            ComPtr<ISimpleAudioVolume> volume;
            DWORD owner{};
            LPWSTR id{};
            if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&detail)) || FAILED(detail->GetProcessId(&owner)) || owner != self ||
                FAILED(control.As(&volume)) || FAILED(detail->GetSessionInstanceIdentifier(&id)) || !id)
                continue;
            std::wstring instance(id);
            CoTaskMemFree(id);
            each(instance, volume.Get());
        }
    }
}

void worker() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto& s = sound();
    std::error_code error;
    for (;;) {
        bool wanted{}, stale{};
        std::filesystem::path marker;
        {
            std::lock_guard lock(s.mutex);
            marker = s.marker;
            if (s.stale < 0) s.stale = std::filesystem::exists(marker, error) ? 1 : 0;
            wanted = s.wanted;
            stale = s.stale == 1;
        }
        if (wanted) {
            if (!std::filesystem::exists(marker, error)) std::ofstream(marker) << "The game's sound is muted while the Animator Studio is open.\n";
            our_sessions([&](const std::wstring& id, ISimpleAudioVolume* volume) {
                BOOL already{};
                volume->GetMute(&already);
                std::lock_guard lock(s.mutex);
                // Muted by the studio at the end of the last run is not the player's own: it is unmuted on closing.
                if (!s.muted.contains(id)) s.muted[id] = already != FALSE && !stale;
                if (!already) volume->SetMute(TRUE, nullptr);
            });
            std::lock_guard lock(s.mutex);
            if (!s.muted.empty()) s.stale = 0;
        } else {
            std::map<std::wstring, bool> before;
            {
                std::lock_guard lock(s.mutex);
                before.swap(s.muted);
            }
            if (!before.empty()) {
                our_sessions([&](const std::wstring& id, ISimpleAudioVolume* volume) {
                    if (const auto found = before.find(id); found != before.end()) volume->SetMute(found->second ? TRUE : FALSE, nullptr);
                });
                std::filesystem::remove(marker, error);
            }
            if (stale) {
                int found = 0;
                our_sessions([&](const std::wstring&, ISimpleAudioVolume* volume) { found += SUCCEEDED(volume->SetMute(FALSE, nullptr)); });
                std::lock_guard lock(s.mutex);
                if (found || ++s.stale_tries >= 40) {
                    s.stale = 0;
                    std::filesystem::remove(marker, error);
                    if (found) logging::log(Level::info, Channel::skater, "Animation Studio: the game's sound, left muted by the studio last time, is on again.");
                }
            }
        }
        std::unique_lock lock(s.mutex);
        if (!s.wanted && s.muted.empty() && s.stale == 0) {
            s.worker = false;
            break;
        }
        // While muted it looks again now and then, for a sound the game starts while the studio is open; a change of what is
        // asked is done at once.
        s.wake.wait_for(lock, std::chrono::milliseconds(wanted ? 1500 : 3000), [&] { return s.wanted != wanted; });
    }
    if (SUCCEEDED(com)) CoUninitialize();
}
} // namespace

void studio_sound(bool studio_open, const std::filesystem::path& marker) {
    auto& s = sound();
    std::lock_guard lock(s.mutex);
    s.marker = marker;
    const bool changed = s.wanted != studio_open;
    s.wanted = studio_open;
    if (s.worker) {
        if (changed) s.wake.notify_all();
        return;
    }
    if (!changed && s.stale == 0) return;
    s.worker = true;
    try {
        std::thread(worker).detach();
    } catch (...) {
        s.worker = false;
    }
}
} // namespace dingosdk::anim_studio
