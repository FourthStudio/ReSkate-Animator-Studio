#pragma once
// The updater's steps on the disk and with Windows, apart from the game so they can be tried outside it: where ReSkate
// runs from and whether that folder takes a file, putting a new ReSkate.dll in place of the one the game loaded (the old
// one kept beside it, everything put back when a step fails), and the hidden helper that starts the game again once it
// has closed. studio_update.cpp drives them; a harness tries them on a copy of a game folder.
#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <array>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace dingosdk::anim_studio::update::files {
// The file's SHA-256 as lower-case hex, empty when it cannot be read.
inline std::string sha256_of(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0))) return {};
    std::vector<char> chunk(1 << 20);
    bool ok = true;
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<ULONG>(in.gcount());
        if (got && !BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk.data()), got, 0))) {
            ok = false;
            break;
        }
    }
    std::array<unsigned char, 32> digest{};
    ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    BCryptDestroyHash(hash);
    if (!ok) return {};
    std::string hex;
    for (const auto byte : digest) hex += std::format("{:02x}", byte);
    return hex;
}

// Whether `folder` takes a new file: a probe made and deleted at once. A read-only folder, a protected one (Program Files
// without administrator rights) or a full disk does not.
inline bool writable(const std::filesystem::path& folder) {
    const auto probe = folder / std::format(L".reskate-update-check-{}", GetCurrentProcessId());
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

// A rename, tried again for a few seconds while something holds the file a moment (an antivirus scanning a new DLL, an
// Explorer preview). Write-through, so a power cut does not leave the swap half on the disk.
inline bool rename_file(const std::filesystem::path& from, const std::filesystem::path& to, DWORD flags, DWORD& error) {
    for (int i = 0;; ++i) {
        if (MoveFileExW(from.c_str(), to.c_str(), flags | MOVEFILE_WRITE_THROUGH)) return true;
        error = GetLastError();
        const bool held = error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
        if (!(held && i < 25) && !(error == ERROR_ACCESS_DENIED && i < 3)) return false;
        Sleep(200);
    }
}
inline void not_read_only(const std::filesystem::path& file) {
    const DWORD attributes = GetFileAttributesW(file.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesW(file.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
}
inline std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// What install_dll did.
struct Swap {
    bool ok{};
    bool earlier_update{}; // ReSkate.dll was an update from earlier this session (the running one is .previous already)
    std::string why;       // why not, in words
};
// Puts `downloaded` in place of `current` (the ReSkate.dll the game loaded, by the path it was loaded from). Windows lets a
// loaded DLL be renamed but never overwritten or deleted, so:
// 1. the new one is copied in beside it as ReSkate.dll.incoming (same folder: the swap is two renames, never a copy that
//    can stop half way) and read back against its SHA-256;
// 2. the ReSkate.dll.previous of the update before goes (a read-only one too);
// 3. the running one is renamed to ReSkate.dll.previous, to go back to;
// 4. incoming is renamed to ReSkate.dll, and read back once more.
// Any step failing puts the old one back as ReSkate.dll. A second update in the same session finds .previous loaded (it is
// the running one, renamed by the first): ReSkate.dll is then the first update's file, loaded by nothing, and is simply
// replaced.
inline Swap install_dll(const std::filesystem::path& downloaded, const std::filesystem::path& current, const std::string& digest) {
    Swap out;
    const auto folder = current.parent_path();
    auto previous = current, incoming = current;
    previous += L".previous";
    incoming += L".incoming";
    const auto fail = [&](std::string why) {
        not_read_only(incoming);
        DeleteFileW(incoming.c_str());
        out.why = std::move(why);
        return out;
    };
    if (current.empty() || GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES)
        return fail("the running ReSkate.dll could not be found at " + path_text(current));
    if (!writable(folder))
        return fail("the folder ReSkate runs from (" + path_text(folder) + ") cannot be written to: it is read-only or a protected folder "
                    "(such as Program Files), or the disk is full");
    not_read_only(incoming);
    DeleteFileW(incoming.c_str());
    if (!CopyFileW(downloaded.c_str(), incoming.c_str(), FALSE))
        return fail(std::format("it could not be copied into {} (Windows error {})", path_text(folder), GetLastError()));
    if (!digest.empty() && sha256_of(incoming) != digest) return fail("the copy did not match its SHA-256 (a failing disk?), so nothing was changed");
    DWORD error{};
    if (GetFileAttributesW(previous.c_str()) != INVALID_FILE_ATTRIBUTES) {
        not_read_only(previous);
        if (!DeleteFileW(previous.c_str())) {
            error = GetLastError();
            if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
                return fail(std::format("the ReSkate.dll.previous of the last update could not be removed (Windows error {})", error));
            out.earlier_update = true; // it is the running one
        }
    }
    if (out.earlier_update) {
        not_read_only(current);
        if (!rename_file(incoming, current, MOVEFILE_REPLACE_EXISTING, error))
            return fail(std::format("the new ReSkate.dll could not be put in place (Windows error {}); nothing was changed", error));
    } else {
        if (!rename_file(current, previous, 0, error))
            return fail(std::format("the running ReSkate.dll could not be moved aside (Windows error {}); nothing was changed", error));
        if (!rename_file(incoming, current, 0, error)) {
            const auto code = error;
            // The old one back where it was; a copy of it when even the rename back will not go.
            const bool back = rename_file(previous, current, 0, error) || CopyFileW(previous.c_str(), current.c_str(), TRUE);
            return fail(back ? std::format("the new ReSkate.dll could not be put in place (Windows error {}); the old one is back", code)
                             : std::format("the new ReSkate.dll could not be put in place (Windows error {}), and the old one could not be put back: "
                                           "close the game and rename ReSkate.dll.previous to ReSkate.dll",
                                           code));
        }
    }
    // What is at ReSkate.dll now, read back.
    if (!digest.empty() && sha256_of(current) != digest) {
        if (!out.earlier_update) (void)rename_file(previous, current, MOVEFILE_REPLACE_EXISTING, error);
        return fail("the installed ReSkate.dll did not read back as it should, so the old one was put back");
    }
    out.ok = true;
    return out;
}

// The launcher to start the game again with: the one beside ReSkate.dll (the launcher only runs a ReSkate.dll beside it),
// else the process that started this game when it is a launcher in that same folder (one renamed by its player).
inline std::filesystem::path launcher_for(const std::filesystem::path& dll) {
    if (dll.empty()) return {};
    std::error_code error;
    const auto folder = dll.parent_path();
    if (const auto beside = folder / L"ReSkateLauncher.exe"; std::filesystem::is_regular_file(beside, error)) return beside;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    DWORD parent = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        if (entry.th32ProcessID == GetCurrentProcessId()) parent = entry.th32ParentProcessID;
    CloseHandle(snapshot);
    if (!parent) return {};
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent);
    if (!process) return {};
    std::array<wchar_t, 32768> image{};
    DWORD size = static_cast<DWORD>(image.size());
    // Started before this game, or the number is another process's now.
    FILETIME parent_created{}, ours{}, unused{};
    const bool older = GetProcessTimes(process, &parent_created, &unused, &unused, &unused) &&
                       GetProcessTimes(GetCurrentProcess(), &ours, &unused, &unused, &unused) && CompareFileTime(&parent_created, &ours) <= 0;
    const bool named = QueryFullProcessImageNameW(process, 0, image.data(), &size);
    CloseHandle(process);
    if (!older || !named) return {};
    const std::filesystem::path launcher(std::wstring(image.data(), size));
    std::wstring name = launcher.filename().wstring();
    for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
    if (!name.starts_with(L"reskate") || launcher.extension() != L".exe") return {};
    return std::filesystem::equivalent(launcher.parent_path(), folder, error) && !error ? launcher : std::filesystem::path{};
}

// The hidden PowerShell that starts the game again: it waits for this game to close (at most five minutes: the game may
// ask before it closes, and be told no), then starts the launcher as its Play does (--no-gui, the saved launch options)
// without its own update check (--no-update: that would put base ReSkate's DLL back over this one). Paths are quoted for
// PowerShell (a quote in them doubled).
inline std::wstring restart_script(DWORD game, const std::filesystem::path& launcher) {
    const auto quoted = [](const std::wstring& text) {
        std::wstring out = L"'";
        for (const wchar_t c : text) out += c == L'\'' ? std::wstring(L"''") : std::wstring(1, c);
        return out + L"'";
    };
    const auto id = std::to_wstring(game);
    return L"$ErrorActionPreference = 'SilentlyContinue'; Wait-Process -Id " + id + L" -Timeout 300; if (Get-Process -Id " + id +
           L") { exit 1 }; Start-Sleep -Seconds 2; Start-Process -FilePath " + quoted(launcher.wstring()) +
           L" -ArgumentList '--no-gui','--no-update' -WorkingDirectory " + quoted(launcher.parent_path().wstring());
}
// Starts `script` in a PowerShell that never shows a window (CREATE_NO_WINDOW: no console window, nor one for anything it
// runs), outside any job the game is in when it may be (so closing the game does not end it), else as it is. 0 or the
// Windows error.
inline DWORD start_hidden(const std::wstring& script, const std::filesystem::path& folder) {
    std::array<wchar_t, MAX_PATH> system{};
    const UINT n = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    if (!n || n >= system.size()) return ERROR_PATH_NOT_FOUND;
    const std::wstring shell = std::wstring(system.data(), n) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command = L"\"" + shell + L"\" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"" + script + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB, nullptr, folder.c_str(), &startup, &process);
    if (!started)
        started = CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
                                 folder.c_str(), &startup, &process);
    if (!started) return GetLastError();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
}
