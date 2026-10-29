#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <obs.h>
#include <obs-source.h>

#include <atomic>
#include <cstdint>
#include <thread>

class AimpAudioCapture {
public:
    explicit AimpAudioCapture(obs_source_t *source);
    ~AimpAudioCapture();

    // Non-copyable, non-movable
    AimpAudioCapture(const AimpAudioCapture &) = delete;
    AimpAudioCapture &operator=(const AimpAudioCapture &) = delete;

    void start();
    void stop();

    bool isCapturing() const { return capturing_.load(); }

    void setMonitorSource(obs_source_t *monitorSource)
    {
        monitorSource_.store(monitorSource);
    }

private:
    void workerThread();
    bool initWasapi(DWORD pid);
    void cleanupWasapi();
    void processAudioPacket();

    obs_source_t *source_ = nullptr;
    std::atomic<obs_source_t *> monitorSource_{nullptr};
    std::atomic<bool> running_{false};
    std::atomic<bool> capturing_{false};
    std::thread thread_;

    HANDLE stopEvent_ = nullptr;
    HANDLE sampleReadyEvent_ = nullptr;

    HMODULE mmdevapiModule_ = nullptr;
    typedef HRESULT(STDAPICALLTYPE *PFN_ActivateAudioInterfaceAsync)(
        LPCWSTR, REFIID, PROPVARIANT *, IActivateAudioInterfaceCompletionHandler *, IActivateAudioInterfaceAsyncOperation **);
    PFN_ActivateAudioInterfaceAsync activateAudioInterfaceAsync_ = nullptr;

    Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient_;

    uint32_t sampleRate_ = 48000;
    uint32_t channels_ = 2;
    enum speaker_layout speakers_ = SPEAKERS_STEREO;
    enum audio_format format_ = AUDIO_FORMAT_FLOAT;
    DWORD currentPid_ = 0;
    uint64_t lastTimestamp_ = 0;
};
