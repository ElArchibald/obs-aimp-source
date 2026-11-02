#include "aimp-controller.hpp"
#include "aimp-source.hpp"

#include <obs.h>

#include <QCoreApplication>
#include <QTimer>

#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "shlwapi.lib")

static const wchar_t *AIMP_MSG_WINDOW_CLASS = L"ObsAimpControllerMsgClass";

AimpController &AimpController::instance()
{
    static AimpController inst;
    return inst;
}

AimpController::AimpController()
{
    createMessageWindow();

    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &AimpController::pollTimerTick);
    pollTimer_->start(500); // 500 ms heartbeat & position refresh

    pollTimerTick();
}

AimpController::~AimpController()
{
    if (pollTimer_) {
        pollTimer_->stop();
    }
    disconnectFromAimp();
    destroyMessageWindow();
}

void AimpController::createMessageWindow()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = AimpController::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = AIMP_MSG_WINDOW_CLASS;
    RegisterClassExW(&wc);

    messageHwnd_ = CreateWindowExW(0, AIMP_MSG_WINDOW_CLASS, L"ObsAimpMessageListener", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), this);
    if (messageHwnd_) {
        ChangeWindowMessageFilterEx(messageHwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(messageHwnd_, WM_AIMP_NOTIFY, MSGFLT_ALLOW, nullptr);
    }
}

void AimpController::destroyMessageWindow()
{
    if (messageHwnd_) {
        DestroyWindow(messageHwnd_);
        messageHwnd_ = nullptr;
    }
    UnregisterClassW(AIMP_MSG_WINDOW_CLASS, GetModuleHandle(nullptr));
}

LRESULT CALLBACK AimpController::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_CREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto *self = reinterpret_cast<AimpController *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) {
        return self->handleMessage(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT AimpController::handleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_AIMP_NOTIFY) {
        switch (wParam) {
        case AIMP_RA_NOTIFY_TRACK_INFO:
        case AIMP_RA_NOTIFY_TRACK_START:
            updateTrackInfo();
            requestAlbumArt();
            break;
        case AIMP_RA_NOTIFY_PROPERTY:
            switch (lParam) {
            case AIMP_RA_PROPERTY_PLAYER_STATE:
                updatePlayerState();
                break;
            case AIMP_RA_PROPERTY_PLAYER_POSITION:
            case AIMP_RA_PROPERTY_PLAYER_DURATION:
                updatePosition();
                break;
            case AIMP_RA_PROPERTY_VOLUME:
            case AIMP_RA_PROPERTY_MUTE:
                updateVolume();
                break;
            case AIMP_RA_PROPERTY_TRACK_REPEAT:
            case AIMP_RA_PROPERTY_TRACK_SHUFFLE:
                updateModes();
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return 0;
    } else if (uMsg == WM_COPYDATA) {
        auto *cds = reinterpret_cast<COPYDATASTRUCT *>(lParam);
        if (cds && cds->dwData == WM_AIMP_COPYDATA_ALBUMART_ID) {
            QPixmap pix;
            if (cds->lpData && cds->cbData > 0) {
                QByteArray bytes(reinterpret_cast<const char *>(cds->lpData), static_cast<int>(cds->cbData));
                pix.loadFromData(bytes);
            }

            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                albumArt_ = pix;
            }

            dispatchCallbacks([&pix](const OwnerCallbacks &cb) {
                if (cb.artCb) cb.artCb(pix);
            });
            emit albumArtChanged(pix);
            return TRUE;
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void AimpController::dispatchCallbacks(const std::function<void(const OwnerCallbacks &)> &dispatchFn)
{
    std::vector<OwnerCallbacks> localCbs;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        localCbs = callbacks_;
    }
    for (const auto &cb : localCbs) {
        dispatchFn(cb);
    }
}

void AimpController::connectToAimp(HWND aimpHwnd)
{
    aimpHwnd_.store(aimpHwnd);
    DWORD pid = 0;
    GetWindowThreadProcessId(aimpHwnd, &pid);
    aimpPid_.store(pid);

    if (messageHwnd_) {
        SendMessageW(aimpHwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_REGISTER_NOTIFY, reinterpret_cast<LPARAM>(messageHwnd_));
    }

    updatePlayerState();
    updateTrackInfo();
    updatePosition();
    updateVolume();
    updateModes();
    requestAlbumArt();

    emit connectionChanged(true);
}

void AimpController::disconnectFromAimp()
{
    HWND hwnd = aimpHwnd_.exchange(nullptr);
    if (hwnd && messageHwnd_) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_UNREGISTER_NOTIFY, reinterpret_cast<LPARAM>(messageHwnd_));
    }
    aimpPid_.store(0);

    AimpTrackInfo emptyTrack;
    QPixmap emptyArt;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        state_ = AimpPlayerState::Disconnected;
        trackInfo_ = emptyTrack;
        albumArt_ = emptyArt;
        positionMs_ = 0;
        durationMs_ = 0;
    }

    dispatchCallbacks([&emptyTrack, &emptyArt](const OwnerCallbacks &cb) {
        if (cb.stateCb) cb.stateCb(AimpPlayerState::Disconnected);
        if (cb.trackCb) cb.trackCb(emptyTrack);
        if (cb.posCb) cb.posCb(0, 0);
        if (cb.artCb) cb.artCb(emptyArt);
    });

    emit stateChanged(AimpPlayerState::Disconnected);
    emit trackChanged(emptyTrack);
    emit positionChanged(0, 0);
    emit albumArtChanged(emptyArt);
    emit connectionChanged(false);
}

struct EnumDesktopContext {
    HWND foundHwnd = nullptr;
};

static BOOL CALLBACK EnumDesktopProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<EnumDesktopContext *>(lParam);
    char className[128] = {};
    if (GetClassNameA(hwnd, className, sizeof(className))) {
        if (strcmp(className, AIMPRemoteAccessClass) == 0) {
            ctx->foundHwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

void AimpController::pollTimerTick()
{
    HWND found = FindWindowA(AIMPRemoteAccessClass, AIMPRemoteAccessClass);
    if (!found) {
        found = FindWindowA(AIMPRemoteAccessClass, nullptr);
    }

    // Try desktop search (input desktop first, then "Default" desktop)
    if (!found) {
        HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_ENUMERATE);
        if (!hDesk) {
            hDesk = OpenDesktopW(L"Default", 0, FALSE, DESKTOP_READOBJECTS | DESKTOP_ENUMERATE);
        }
        if (hDesk) {
            EnumDesktopContext ctx;
            EnumDesktopWindows(hDesk, EnumDesktopProc, reinterpret_cast<LPARAM>(&ctx));
            CloseDesktop(hDesk);
            if (ctx.foundHwnd) {
                found = ctx.foundHwnd;
            }
        }
    }

    // Always ensure AIMP process PID is resolved
    if (aimpPid_.load() == 0) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"AIMP.exe") == 0) {
                        aimpPid_.store(pe.th32ProcessID);
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    HWND currentHwnd = aimpHwnd_.load();
    if (found) {
        if (found != currentHwnd || !IsWindow(currentHwnd)) {
            connectToAimp(found);
        } else {
            updatePlayerState();
            updateTrackInfo();
            if (getState() == AimpPlayerState::Playing) {
                updatePosition();
            }
        }
    } else {
        // Fallback: Check if shared memory exists
        HANDLE hFile = OpenFileMappingA(FILE_MAP_READ, TRUE, AIMPRemoteAccessClass);
        if (hFile) {
            CloseHandle(hFile);
            updateTrackInfo();
            if (currentHwnd == nullptr) {
                std::lock_guard<std::mutex> lock(dataMutex_);
                AimpPlayerState newState = trackInfo_.active ? AimpPlayerState::Playing : AimpPlayerState::Stopped;
                if (state_ != newState) {
                    state_ = newState;
                    emit connectionChanged(true);
                    emit stateChanged(state_);
                }
            }
        } else if (currentHwnd != nullptr || getState() != AimpPlayerState::Disconnected) {
            disconnectFromAimp();
        }
    }
}

void AimpController::updatePlayerState()
{
    HWND hwnd = aimpHwnd_.load();
    if (!hwnd) return;

    LRESULT val = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_PLAYER_STATE | AIMP_RA_PROPVALUE_GET, 0);
    AimpPlayerState newState = AimpPlayerState::Stopped;
    if (val == 1) newState = AimpPlayerState::Paused;
    else if (val == 2) newState = AimpPlayerState::Playing;

    bool hasStateChanged = false;
    uint32_t dur = 0;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (state_ != newState) {
            state_ = newState;
            hasStateChanged = true;
        }
        if (newState == AimpPlayerState::Stopped) {
            positionMs_ = 0;
        }
        dur = durationMs_;
    }

    if (hasStateChanged) {
        dispatchCallbacks([newState](const OwnerCallbacks &cb) {
            if (cb.stateCb) cb.stateCb(newState);
        });
        emit stateChanged(newState);
    }

    if (newState == AimpPlayerState::Stopped) {
        dispatchCallbacks([dur](const OwnerCallbacks &cb) {
            if (cb.posCb) cb.posCb(0, dur);
        });
        emit positionChanged(0, dur);
    }
}

void AimpController::updateTrackInfo()
{
    HANDLE hFile = OpenFileMappingA(FILE_MAP_READ, TRUE, AIMPRemoteAccessClass);
    if (!hFile) return;

    auto *pInfo = reinterpret_cast<PAIMPRemoteFileInfo>(MapViewOfFile(hFile, FILE_MAP_READ, 0, 0, AIMPRemoteAccessMapFileSize));
    if (!pInfo) {
        CloseHandle(hFile);
        return;
    }

    AimpTrackInfo info;
    const BYTE *basePtr = reinterpret_cast<const BYTE *>(pInfo);
    const BYTE *bufPtr = basePtr + sizeof(TAIMPRemoteFileInfo);
    const BYTE *maxPtr = basePtr + AIMPRemoteAccessMapFileSize;

    auto readString = [&](DWORD lenChars) -> std::wstring {
        if (lenChars == 0) return std::wstring();
        size_t byteLen = static_cast<size_t>(lenChars) * sizeof(wchar_t);
        if (bufPtr + byteLen > maxPtr) return std::wstring();
        std::wstring str(reinterpret_cast<const wchar_t *>(bufPtr), lenChars);
        bufPtr += byteLen;
        return str;
    };

    info.album = readString(pInfo->AlbumLength);
    info.artist = readString(pInfo->ArtistLength);
    info.date = readString(pInfo->DateLength);
    info.fileName = readString(pInfo->FileNameLength);
    info.genre = readString(pInfo->GenreLength);
    info.title = readString(pInfo->TitleLength);

    info.durationMs = pInfo->Duration;
    info.bitRate = pInfo->BitRate;
    info.sampleRate = pInfo->SampleRate;
    info.channels = pInfo->Channels;
    info.fileSize = pInfo->FileSize;
    info.active = (pInfo->Active != 0);

    UnmapViewOfFile(pInfo);
    CloseHandle(hFile);

    bool changed = false;
    uint32_t curPos = 0;
    uint32_t curDur = 0;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (trackInfo_ != info) {
            trackInfo_ = info;
            durationMs_ = info.durationMs;
            curPos = positionMs_;
            curDur = durationMs_;
            changed = true;
        }
    }

    if (changed) {
        dispatchCallbacks([&info, curPos, curDur](const OwnerCallbacks &cb) {
            if (cb.trackCb) cb.trackCb(info);
            if (cb.posCb) cb.posCb(curPos, curDur);
        });
        emit trackChanged(info);
        emit positionChanged(curPos, curDur);
        requestAlbumArt();
    }
}

void AimpController::updatePosition()
{
    HWND hwnd = aimpHwnd_.load();
    if (!hwnd) return;

    LRESULT pos = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_PLAYER_POSITION | AIMP_RA_PROPVALUE_GET, 0);
    LRESULT dur = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_PLAYER_DURATION | AIMP_RA_PROPVALUE_GET, 0);

    bool changed = false;
    uint32_t newPos = static_cast<uint32_t>(pos > 0 ? pos : 0);
    uint32_t newDur = 0;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        newDur = (dur > 0) ? static_cast<uint32_t>(dur) : durationMs_;
        if (positionMs_ != newPos || durationMs_ != newDur) {
            positionMs_ = newPos;
            durationMs_ = newDur;
            changed = true;
        }
    }

    if (changed) {
        dispatchCallbacks([newPos, newDur](const OwnerCallbacks &cb) {
            if (cb.posCb) cb.posCb(newPos, newDur);
        });
        emit positionChanged(newPos, newDur);
    }
}

void AimpController::updateVolume()
{
    HWND hwnd = aimpHwnd_.load();
    if (!hwnd) return;

    LRESULT vol = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_VOLUME | AIMP_RA_PROPVALUE_GET, 0);
    LRESULT mute = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_MUTE | AIMP_RA_PROPVALUE_GET, 0);

    bool changed = false;
    int newVol = static_cast<int>(vol);
    bool newMute = (mute != 0);

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (volume_ != newVol || isMuted_ != newMute) {
            volume_ = newVol;
            isMuted_ = newMute;
            changed = true;
        }
    }

    if (changed) {
        emit volumeChanged(newVol, newMute);
    }
}

void AimpController::updateModes()
{
    HWND hwnd = aimpHwnd_.load();
    if (!hwnd) return;

    LRESULT rep = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_TRACK_REPEAT | AIMP_RA_PROPVALUE_GET, 0);
    LRESULT shuf = SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_TRACK_SHUFFLE | AIMP_RA_PROPVALUE_GET, 0);

    bool changed = false;
    bool newRep = (rep != 0);
    bool newShuf = (shuf != 0);

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (isRepeat_ != newRep || isShuffle_ != newShuf) {
            isRepeat_ = newRep;
            isShuffle_ = newShuf;
            changed = true;
        }
    }

    if (changed) {
        emit playbackModesChanged(newRep, newShuf);
    }
}

void AimpController::requestAlbumArt()
{
    HWND hwnd = aimpHwnd_.load();
    if (!hwnd || !messageHwnd_) return;

    LRESULT res = SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_GET_ALBUMART, reinterpret_cast<LPARAM>(messageHwnd_));
    if (res == 0) {
        // No cover art available from player
        QPixmap emptyArt;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            albumArt_ = emptyArt;
        }
        dispatchCallbacks([&emptyArt](const OwnerCallbacks &cb) {
            if (cb.artCb) cb.artCb(emptyArt);
        });
        emit albumArtChanged(emptyArt);
    }
}

void AimpController::play()
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_PLAY, 0);
        updatePlayerState();
    }
}

void AimpController::pause()
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_PAUSE, 0);
        updatePlayerState();
    }
}

void AimpController::playPause()
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_PLAYPAUSE, 0);
        updatePlayerState();
    }
}

void AimpController::stop()
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_STOP, 0);
        uint32_t dur = 0;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            state_ = AimpPlayerState::Stopped;
            positionMs_ = 0;
            dur = durationMs_;
        }
        dispatchCallbacks([dur](const OwnerCallbacks &cb) {
            if (cb.stateCb) cb.stateCb(AimpPlayerState::Stopped);
            if (cb.posCb) cb.posCb(0, dur);
        });
        emit stateChanged(AimpPlayerState::Stopped);
        emit positionChanged(0, dur);
        updatePlayerState();
    }
}

void AimpController::next()
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_NEXT, 0);
    }
}

void AimpController::prev()
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_COMMAND, AIMP_RA_CMD_PREV, 0);
    }
}

void AimpController::setPosition(uint32_t positionMs)
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_PLAYER_POSITION | AIMP_RA_PROPVALUE_SET, static_cast<LPARAM>(positionMs));
        uint32_t dur = 0;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            positionMs_ = positionMs;
            dur = durationMs_;
        }
        emit positionChanged(positionMs, dur);
    }
}

void AimpController::setVolume(int volume)
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_VOLUME | AIMP_RA_PROPVALUE_SET, static_cast<LPARAM>(volume));
        bool muted = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            volume_ = volume;
            muted = isMuted_;
        }
        emit volumeChanged(volume, muted);
    }
}

void AimpController::setMute(bool mute)
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_MUTE | AIMP_RA_PROPVALUE_SET, mute ? 1 : 0);
        int vol = 0;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            isMuted_ = mute;
            vol = volume_;
        }
        emit volumeChanged(vol, mute);
    }
}

void AimpController::setTrackRepeat(bool repeat)
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_TRACK_REPEAT | AIMP_RA_PROPVALUE_SET, repeat ? 1 : 0);
        bool shuf = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            isRepeat_ = repeat;
            shuf = isShuffle_;
        }
        emit playbackModesChanged(repeat, shuf);
    }
}

void AimpController::setTrackShuffle(bool shuffle)
{
    HWND hwnd = aimpHwnd_.load();
    if (hwnd) {
        SendMessageW(hwnd, WM_AIMP_PROPERTY, AIMP_RA_PROPERTY_TRACK_SHUFFLE | AIMP_RA_PROPVALUE_SET, shuffle ? 1 : 0);
        bool rep = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            isShuffle_ = shuffle;
            rep = isRepeat_;
        }
        emit playbackModesChanged(rep, shuffle);
    }
}

std::wstring AimpController::findAimpExecutable()
{
    // 1. Check registry HKCU / HKLM
    const HKEY rootKeys[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const wchar_t *subKeys[] = {
        L"Software\\AIMP\\AIMP3",
        L"Software\\AIMP",
        L"Software\\WOW6432Node\\AIMP"
    };

    for (HKEY root : rootKeys) {
        for (const wchar_t *sub : subKeys) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(root, sub, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                wchar_t path[MAX_PATH] = {};
                DWORD size = sizeof(path);
                if (RegQueryValueExW(hKey, L"AIMP_AppPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS ||
                    RegQueryValueExW(hKey, L"Path", nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    if (PathFileExistsW(path)) return path;
                }
                RegCloseKey(hKey);
            }
        }
    }

    // 2. Check App Paths in Registry
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\AIMP.exe", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (PathFileExistsW(path)) return path;
        }
        RegCloseKey(hKey);
    }

    // 3. Check Standard Program Files paths
    const wchar_t *standardPaths[] = {
        L"C:\\Program Files\\AIMP\\AIMP.exe",
        L"C:\\Program Files (x86)\\AIMP\\AIMP.exe",
        L"D:\\Program Files\\AIMP\\AIMP.exe",
        L"E:\\Program Files\\AIMP\\AIMP.exe",
        L"C:\\AIMP\\AIMP.exe",
        L"D:\\AIMP\\AIMP.exe"
    };

    for (const wchar_t *p : standardPaths) {
        if (PathFileExistsW(p)) {
            return p;
        }
    }

    return L"";
}

bool AimpController::launchAimp()
{
    std::wstring exePath = findAimpExecutable();
    if (exePath.empty()) {
        HINSTANCE hInst = ShellExecuteW(nullptr, L"open", L"AIMP.exe", nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(hInst) > 32;
    }

    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(hInst) > 32;
}

bool AimpController::isRunning() const
{
    HWND hwnd = aimpHwnd_.load();
    return hwnd != nullptr && IsWindow(hwnd);
}

HWND AimpController::getAimpWindow() const
{
    return aimpHwnd_.load();
}

DWORD AimpController::getAimpProcessId()
{
    DWORD pid = aimpPid_.load();
    if (pid == 0) {
        HWND hwnd = aimpHwnd_.load();
        if (hwnd && IsWindow(hwnd)) {
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != 0) {
                aimpPid_.store(pid);
                return pid;
            }
        }

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"AIMP.exe") == 0) {
                        pid = pe.th32ProcessID;
                        aimpPid_.store(pid);
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }
    return pid;
}

AimpPlayerState AimpController::getState()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return state_;
}

uint32_t AimpController::getPositionMs()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return positionMs_;
}

uint32_t AimpController::getDurationMs()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return durationMs_;
}

int AimpController::getVolume()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return volume_;
}

bool AimpController::isMuted()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return isMuted_;
}

bool AimpController::isRepeat()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return isRepeat_;
}

bool AimpController::isShuffle()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return isShuffle_;
}

AimpTrackInfo AimpController::getTrackInfo()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return trackInfo_;
}

QPixmap AimpController::getAlbumArt()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return albumArt_;
}

void AimpController::addStateCallback(void *owner, StateCallback cb)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto &item : callbacks_) {
        if (item.owner == owner) {
            item.stateCb = cb;
            return;
        }
    }
    callbacks_.push_back({owner, cb, nullptr, nullptr, nullptr});
}

void AimpController::addTrackCallback(void *owner, TrackCallback cb)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto &item : callbacks_) {
        if (item.owner == owner) {
            item.trackCb = cb;
            return;
        }
    }
    callbacks_.push_back({owner, nullptr, cb, nullptr, nullptr});
}

void AimpController::addPositionCallback(void *owner, PositionCallback cb)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto &item : callbacks_) {
        if (item.owner == owner) {
            item.posCb = cb;
            return;
        }
    }
    callbacks_.push_back({owner, nullptr, nullptr, cb, nullptr});
}

void AimpController::addAlbumArtCallback(void *owner, AlbumArtCallback cb)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto &item : callbacks_) {
        if (item.owner == owner) {
            item.artCb = cb;
            return;
        }
    }
    callbacks_.push_back({owner, nullptr, nullptr, nullptr, cb});
}

void AimpController::removeCallbacks(void *owner)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(), [owner](const OwnerCallbacks &c) {
        return c.owner == owner;
    }), callbacks_.end());
}

void AimpController::setAudioCapture(bool audioCapture, bool notifySources)
{
    bool changed = (audioCapture_.exchange(audioCapture) != audioCapture);

    if (notifySources) {
        obs_enum_sources([](void *param, obs_source_t *source) -> bool {
            const char *id = obs_source_get_id(source);
            if (id && strcmp(id, "obs_aimp_source") == 0) {
                obs_data_t *settings = obs_source_get_settings(source);
                bool cap = *static_cast<bool *>(param);
                obs_data_set_bool(settings, "audio_capture", cap);
                obs_source_update(source, settings);
                obs_data_release(settings);
                obs_source_update_properties(source);
            }
            return true;
        }, &audioCapture);
    }

    if (changed) {
        emit audioCaptureChanged(audioCapture);
    }
}

void AimpController::notifyAudioCapture(bool audioCapture)
{
    setAudioCapture(audioCapture, false);
}

void AimpController::setMonitoringState(bool audioMon, bool mixerMon, float monVolDb, bool notifySources)
{
    bool prevAudioMon = audioMonitoring_.exchange(audioMon);
    bool prevMixerMon = mixerMonitoring_.exchange(mixerMon);
    float prevMonVolDb = monitorVolumeDb_.exchange(monVolDb);

    bool boolsChanged = (prevAudioMon != audioMon || prevMixerMon != mixerMon);
    bool isInfPrev = (prevMonVolDb <= -96.0f || std::isinf(prevMonVolDb));
    bool isInfCur = (monVolDb <= -96.0f || std::isinf(monVolDb));
    bool volChanged = (isInfPrev != isInfCur) || (!isInfCur && std::abs(prevMonVolDb - monVolDb) >= 0.05f);
    bool changed = (boolsChanged || volChanged);

    if (notifySources) {
        struct MonEnumData {
            bool audioMon;
            bool mixerMon;
            float monVolDb;
            bool propsNeedUpdate;
        } data = {audioMon, mixerMon, isInfCur ? -96.0f : monVolDb, boolsChanged || volChanged};

        obs_enum_sources([](void *param, obs_source_t *source) -> bool {
            const char *id = obs_source_get_id(source);
            if (id && strcmp(id, "obs_aimp_source") == 0) {
                obs_data_t *settings = obs_source_get_settings(source);
                auto *p = static_cast<MonEnumData *>(param);
                obs_data_set_bool(settings, "audio_monitoring", p->audioMon);
                obs_data_set_bool(settings, "mixer_monitoring", p->mixerMon);
                obs_data_set_double(settings, "monitor_volume_db", p->monVolDb);
                obs_source_update(source, settings);
                obs_data_release(settings);
                if (p->propsNeedUpdate) {
                    obs_source_update_properties(source);
                }
            }
            return true;
        }, &data);
    }

    if (changed) {
        emit monitoringChanged(audioMon, mixerMon, monVolDb);
    }
}

void AimpController::notifyMonitoringSettings(bool audioMon, bool mixerMon, float monVolDb)
{
    setMonitoringState(audioMon, mixerMon, monVolDb, false);
}

void AimpController::setMonitorMute(bool muted, bool notifySources)
{
    bool prevMuted = monitorMuted_.exchange(muted);
    if (prevMuted != muted) {
        if (notifySources) {
            obs_enum_sources([](void *param, obs_source_t *source) -> bool {
                const char *id = obs_source_get_id(source);
                if (id && strcmp(id, "obs_aimp_source") == 0) {
                    bool m = *static_cast<bool *>(param);
                    aimp_source_set_monitor_muted(source, m);
                }
                return true;
            }, &muted);
        }
        emit monitorMuteChanged(muted);
    }
}

void AimpController::notifyMonitorMute(bool muted)
{
    bool prevMuted = monitorMuted_.exchange(muted);
    if (prevMuted != muted) {
        emit monitorMuteChanged(muted);
    }
}
