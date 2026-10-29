#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <QObject>
#include <QPixmap>
#include <QImage>

#include "apiRemote.h"

struct AimpTrackInfo {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring date;
    std::wstring genre;
    std::wstring fileName;
    uint32_t durationMs = 0;
    uint32_t bitRate = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    int64_t fileSize = 0;
    bool active = false;

    bool operator==(const AimpTrackInfo &other) const
    {
        return durationMs == other.durationMs &&
               bitRate == other.bitRate &&
               sampleRate == other.sampleRate &&
               channels == other.channels &&
               fileSize == other.fileSize &&
               active == other.active &&
               title == other.title &&
               artist == other.artist &&
               album == other.album &&
               date == other.date &&
               genre == other.genre &&
               fileName == other.fileName;
    }

    bool operator!=(const AimpTrackInfo &other) const
    {
        return !(*this == other);
    }
};

enum class AimpPlayerState {
    Stopped = 0,
    Paused = 1,
    Playing = 2,
    Disconnected = 3
};

class AimpController : public QObject {
    Q_OBJECT

public:
    static AimpController &instance();

    bool isRunning() const;
    HWND getAimpWindow() const;
    DWORD getAimpProcessId();

    // Playback control
    void play();
    void pause();
    void playPause();
    void stop();
    void next();
    void prev();
    void setPosition(uint32_t positionMs);
    void setVolume(int volume);
    void setMute(bool mute);
    void setTrackRepeat(bool repeat);
    void setTrackShuffle(bool shuffle);

    // Launch AIMP
    bool launchAimp();
    static std::wstring findAimpExecutable();

    // Getters
    AimpPlayerState getState();
    uint32_t getPositionMs();
    uint32_t getDurationMs();
    int getVolume();
    bool isMuted();
    bool isRepeat();
    bool isShuffle();
    AimpTrackInfo getTrackInfo();
    QPixmap getAlbumArt();

    // Direct C++ Callbacks for OBS Sources
    using StateCallback = std::function<void(AimpPlayerState)>;
    using TrackCallback = std::function<void(const AimpTrackInfo &)>;
    using PositionCallback = std::function<void(uint32_t posMs, uint32_t durMs)>;
    using AlbumArtCallback = std::function<void(const QPixmap &)>;

    void addStateCallback(void *owner, StateCallback cb);
    void addTrackCallback(void *owner, TrackCallback cb);
    void addPositionCallback(void *owner, PositionCallback cb);
    void addAlbumArtCallback(void *owner, AlbumArtCallback cb);
    void removeCallbacks(void *owner);

    // Audio Capture & Monitoring Settings
    bool isAudioCapture() const { return audioCapture_.load(); }
    bool isAudioMonitoring() const { return audioMonitoring_.load(); }
    bool isMixerMonitoring() const { return mixerMonitoring_.load(); }
    float getMonitorVolumeDb() const { return monitorVolumeDb_.load(); }
    bool isMonitorMuted() const { return monitorMuted_.load(); }

    void setAudioCapture(bool audioCapture, bool notifySources = true);
    void notifyAudioCapture(bool audioCapture);
    void setMonitoringState(bool audioMon, bool mixerMon, float monVolDb, bool notifySources = true);
    void notifyMonitoringSettings(bool audioMon, bool mixerMon, float monVolDb);
    void setMonitorMute(bool muted, bool notifySources = true);
    void notifyMonitorMute(bool muted);

signals:
    void stateChanged(AimpPlayerState state);
    void trackChanged(const AimpTrackInfo &info);
    void positionChanged(uint32_t positionMs, uint32_t durationMs);
    void volumeChanged(int volume, bool muted);
    void playbackModesChanged(bool repeat, bool shuffle);
    void albumArtChanged(const QPixmap &art);
    void connectionChanged(bool connected);
    void audioCaptureChanged(bool audioCapture);
    void monitoringChanged(bool audioMonitoring, bool mixerMonitoring, float monitorVolumeDb);
    void monitorMuteChanged(bool muted);

public slots:
    void pollTimerTick();

private:
    AimpController();
    ~AimpController() override;

    void createMessageWindow();
    void destroyMessageWindow();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void connectToAimp(HWND aimpHwnd);
    void disconnectFromAimp();
    void updateTrackInfo();
    void updatePlayerState();
    void updatePosition();
    void updateVolume();
    void updateModes();
    void requestAlbumArt();

    struct OwnerCallbacks {
        void *owner = nullptr;
        StateCallback stateCb;
        TrackCallback trackCb;
        PositionCallback posCb;
        AlbumArtCallback artCb;
    };
    void dispatchCallbacks(const std::function<void(const OwnerCallbacks &)> &dispatchFn);

    HWND messageHwnd_ = nullptr;
    std::atomic<HWND> aimpHwnd_{nullptr};
    std::atomic<DWORD> aimpPid_{0};

    std::mutex dataMutex_;
    AimpPlayerState state_ = AimpPlayerState::Disconnected;
    AimpTrackInfo trackInfo_;
    uint32_t positionMs_ = 0;
    uint32_t durationMs_ = 0;
    int volume_ = 100;
    bool isMuted_ = false;
    bool isRepeat_ = false;
    bool isShuffle_ = false;
    QPixmap albumArt_;

    std::atomic<bool> audioCapture_{true};
    std::atomic<bool> audioMonitoring_{false};
    std::atomic<bool> mixerMonitoring_{false};
    std::atomic<float> monitorVolumeDb_{0.0f};
    std::atomic<bool> monitorMuted_{false};

    std::vector<OwnerCallbacks> callbacks_;

    class QTimer *pollTimer_ = nullptr;
};
