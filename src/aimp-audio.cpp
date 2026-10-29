#include "aimp-audio.hpp"
#include "aimp-controller.hpp"

#include <util/platform.h>
#include <ks.h>
#include <ksmedia.h>

class WASAPICallbackHandler
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE completedEvent;
    HRESULT asyncResult;
    Microsoft::WRL::ComPtr<IUnknown> activatedInterface;

    WASAPICallbackHandler()
        : completedEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
          asyncResult(E_FAIL)
    {
    }

    ~WASAPICallbackHandler() override
    {
        if (completedEvent) {
            CloseHandle(completedEvent);
        }
    }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *activateOperation) override
    {
        if (activateOperation) {
            activateOperation->GetActivateResult(&asyncResult, &activatedInterface);
        }
        SetEvent(completedEvent);
        return S_OK;
    }
};

AimpAudioCapture::AimpAudioCapture(obs_source_t *source)
    : source_(source)
{
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    sampleReadyEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    mmdevapiModule_ = LoadLibraryW(L"Mmdevapi.dll");
    if (mmdevapiModule_) {
        activateAudioInterfaceAsync_ = reinterpret_cast<PFN_ActivateAudioInterfaceAsync>(
            GetProcAddress(mmdevapiModule_, "ActivateAudioInterfaceAsync"));
    }
}

AimpAudioCapture::~AimpAudioCapture()
{
    stop();

    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (sampleReadyEvent_) {
        CloseHandle(sampleReadyEvent_);
        sampleReadyEvent_ = nullptr;
    }
    if (mmdevapiModule_) {
        FreeLibrary(mmdevapiModule_);
        mmdevapiModule_ = nullptr;
    }
}

void AimpAudioCapture::start()
{
    if (running_.load()) return;

    ResetEvent(stopEvent_);
    running_.store(true);
    thread_ = std::thread(&AimpAudioCapture::workerThread, this);
}

void AimpAudioCapture::stop()
{
    if (!running_.load()) return;

    running_.store(false);
    SetEvent(stopEvent_);

    if (thread_.joinable()) {
        thread_.join();
    }

    cleanupWasapi();
}

bool AimpAudioCapture::initWasapi(DWORD pid)
{
    if (!activateAudioInterfaceAsync_ || pid == 0) return false;

    cleanupWasapi();

    struct obs_audio_info oai = {};
    obs_get_audio_info(&oai);

    sampleRate_ = oai.samples_per_sec;
    speakers_ = oai.speakers;
    channels_ = static_cast<uint32_t>(get_audio_channels(speakers_));

    WAVEFORMATEXTENSIBLE wfext = {};
    WAVEFORMATEX &wf = wfext.Format;
    wf.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.nChannels = static_cast<WORD>(channels_);
    wf.nSamplesPerSec = sampleRate_;
    constexpr WORD wBitsPerSample = 32;
    wf.wBitsPerSample = wBitsPerSample;
    wf.nBlockAlign = wf.nChannels * wBitsPerSample / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize = sizeof(wfext) - sizeof(wf);
    wfext.Samples.wValidBitsPerSample = wBitsPerSample;

    switch (speakers_) {
    case SPEAKERS_MONO:
        wfext.dwChannelMask = SPEAKER_FRONT_CENTER;
        break;
    case SPEAKERS_STEREO:
    default:
        wfext.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        break;
    case SPEAKERS_2POINT1:
        wfext.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY;
        break;
    case SPEAKERS_4POINT0:
        wfext.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
        break;
    case SPEAKERS_4POINT1:
        wfext.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
        break;
    case SPEAKERS_5POINT1:
        wfext.dwChannelMask = KSAUDIO_SPEAKER_5POINT1_SURROUND;
        break;
    case SPEAKERS_7POINT1:
        wfext.dwChannelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
        break;
    }

    wfext.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    AUDIOCLIENT_ACTIVATION_PARAMS activationParams = {};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.TargetProcessId = pid;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateProp = {};
    activateProp.vt = VT_BLOB;
    activateProp.blob.cbSize = sizeof(activationParams);
    activateProp.blob.pBlobData = reinterpret_cast<BYTE *>(&activationParams);

    Microsoft::WRL::ComPtr<WASAPICallbackHandler> handler = Microsoft::WRL::Make<WASAPICallbackHandler>();
    Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;

    HRESULT hr = activateAudioInterfaceAsync_(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &activateProp,
        handler.Get(),
        &asyncOp);

    if (FAILED(hr)) {
        return false;
    }

    WaitForSingleObject(handler->completedEvent, 1000);
    if (FAILED(handler->asyncResult) || !handler->activatedInterface) {
        return false;
    }

    hr = handler->activatedInterface.As(&audioClient_);
    if (FAILED(hr) || !audioClient_) {
        return false;
    }

    constexpr REFERENCE_TIME bufferDuration = 500000; // 50 ms
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK,
        bufferDuration,
        0,
        &wf,
        nullptr);

    if (FAILED(hr)) {
        audioClient_.Reset();
        return false;
    }

    hr = audioClient_->SetEventHandle(sampleReadyEvent_);
    if (FAILED(hr)) {
        audioClient_.Reset();
        return false;
    }

    hr = audioClient_->GetService(IID_PPV_ARGS(&captureClient_));
    if (FAILED(hr) || !captureClient_) {
        audioClient_.Reset();
        return false;
    }

    hr = audioClient_->Start();
    if (FAILED(hr)) {
        cleanupWasapi();
        return false;
    }

    currentPid_ = pid;
    lastTimestamp_ = 0;
    capturing_.store(true);
    return true;
}

void AimpAudioCapture::cleanupWasapi()
{
    capturing_.store(false);
    if (audioClient_) {
        audioClient_->Stop();
    }
    captureClient_.Reset();
    audioClient_.Reset();
    currentPid_ = 0;
    lastTimestamp_ = 0;
}

void AimpAudioCapture::processAudioPacket()
{
    if (!captureClient_ || !source_) return;

    UINT32 packetSize = 0;
    while (SUCCEEDED(captureClient_->GetNextPacketSize(&packetSize)) && packetSize > 0) {
        BYTE *pData = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;
        UINT64 devPos = 0, qpcPos = 0;

        HRESULT hr = captureClient_->GetBuffer(&pData, &numFrames, &flags, &devPos, &qpcPos);
        if (SUCCEEDED(hr)) {
            if (numFrames > 0) {
                uint64_t now = os_gettime_ns();
                if (lastTimestamp_ == 0 || (now > lastTimestamp_ && (now - lastTimestamp_) > 500000000ULL)) {
                    lastTimestamp_ = now;
                }

                struct obs_source_audio audio = {};
                audio.frames = numFrames;
                audio.samples_per_sec = sampleRate_;
                audio.speakers = speakers_;
                audio.format = AUDIO_FORMAT_FLOAT;
                audio.timestamp = lastTimestamp_;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    audio.data[0] = nullptr;
                } else {
                    audio.data[0] = pData;
                }

                obs_source_output_audio(source_, &audio);
                obs_source_t *mon = monitorSource_.load();
                if (mon) {
                    obs_source_output_audio(mon, &audio);
                }

                lastTimestamp_ += static_cast<uint64_t>(numFrames) * 1000000000ULL / sampleRate_;
            }
            captureClient_->ReleaseBuffer(numFrames);
        } else {
            break;
        }
    }
}

void AimpAudioCapture::workerThread()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HANDLE waitHandles[2] = { stopEvent_, sampleReadyEvent_ };

    while (running_.load()) {
        DWORD targetPid = AimpController::instance().getAimpProcessId();

        if (targetPid == 0) {
            if (capturing_.load()) {
                cleanupWasapi();
            }
            WaitForSingleObject(stopEvent_, 300);
            continue;
        }

        if (!capturing_.load() || targetPid != currentPid_) {
            if (!initWasapi(targetPid)) {
                WaitForSingleObject(stopEvent_, 500);
                continue;
            }
        }

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 200);
        if (waitResult == WAIT_OBJECT_0) {
            // Stop signaled
            break;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            processAudioPacket();
        } else if (waitResult == WAIT_TIMEOUT) {
            // Check if process still alive
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, currentPid_);
            if (hProc) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(hProc, &exitCode) && exitCode != STILL_ACTIVE) {
                    cleanupWasapi();
                }
                CloseHandle(hProc);
            } else {
                cleanupWasapi();
            }
        }
    }

    cleanupWasapi();
    CoUninitialize();
}
