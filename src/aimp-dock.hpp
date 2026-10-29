#pragma once

#include <QCheckBox>
#include <QDockWidget>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <obs-audio-controls.h>
#include <obs.h>

#include "aimp-controller.hpp"

class ClickableSlider : public QSlider {
    Q_OBJECT
public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    int pixelPosToRangeValue(const QPoint &pos) const;
    bool dragging_ = false;
};

class AimpControlDock : public QDockWidget {
    Q_OBJECT

public:
    explicit AimpControlDock(QWidget *parent = nullptr);
    ~AimpControlDock() override;

    // Non-copyable, non-movable
    AimpControlDock(const AimpControlDock &) = delete;
    AimpControlDock &operator=(const AimpControlDock &) = delete;

    void refreshSourceAttachment();
    void onSourceFaderChanged(float db);
    void onSourceMuted(bool muted);
    void onMonitorMuted(bool muted);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onStateChanged(AimpPlayerState state);
    void onTrackChanged(const AimpTrackInfo &info);
    void onPositionChanged(uint32_t posMs, uint32_t durMs);
    void onVolumeChanged(int volume, bool muted);
    void onPlaybackModesChanged(bool repeat, bool shuffle);
    void onAlbumArtChanged(const QPixmap &art);
    void onConnectionChanged(bool connected);
    void onAudioCaptureChanged(bool audioCapture);
    void onMonitoringChanged(bool audioMonitoring, bool mixerMonitoring, float monitorVolumeDb);

    void onBtnPlayPauseClicked();
    void onBtnStopClicked();
    void onBtnPrevClicked();
    void onBtnNextClicked();
    void onBtnShuffleClicked();
    void onBtnRepeatClicked();
    void onBtnMuteClicked();
    void onBtnLaunchAimpClicked();

    void onSeekSliderMoved(int value);
    void onSeekSliderReleased();
    void onVolumeSliderChanged(int value);

    void onChkAudioCaptureToggled(bool checked);
    void onChkAudioMonitoringToggled(bool checked);
    void onChkMixerMonitoringToggled(bool checked);
    void onSourceVolumeSliderChanged(int value);
    void onBtnSourceMuteClicked();
    void onBtnMonitorMuteClicked();
    void onMonitorVolumeSliderChanged(int value);

public:
    enum class MediaIconType {
        Prev,
        Play,
        Pause,
        Stop,
        Next,
        Shuffle,
        Repeat,
        VolumeHigh,
        Mute,
        Headphones,
        HeadphonesMute
    };

    enum class StatusIconType {
        DisconnectedWarning,
        Playing,
        Paused,
        Stopped
    };

private:
    void setupUi();
    void updateIcons();
    void updatePlaybackModeIcons();
    static void formatTime(uint32_t ms, char *buf, size_t bufLen);
    void updateStatusDisplay(AimpPlayerState state, bool connected);

    static QPixmap createStatusIcon(StatusIconType type, const QColor &color);
    static QIcon createMediaIcon(MediaIconType type, const QColor &color);
    static QIcon createToggleIcon(MediaIconType type, const QColor &offColor, const QColor &onColor);
    static QIcon createAimpMonoIcon(const QColor &color);

    static obs_source_t *findAimpSource();
    obs_source_t *getAttachedSource() const;
    void attachToAimpSource(obs_source_t *source);
    void detachSource();
    static QString formatDb(float db);

    // Widgets
    QLabel *lblStatusDot_ = nullptr;
    QLabel *lblStatus_ = nullptr;
    QPushButton *btnLaunchAimp_ = nullptr;

    QLabel *lblCover_ = nullptr;
    QLabel *lblTitle_ = nullptr;
    QLabel *lblArtist_ = nullptr;

    QLabel *lblElapsed_ = nullptr;
    ClickableSlider *sliderSeek_ = nullptr;
    QLabel *lblDuration_ = nullptr;
    bool isSeeking_ = false;

    QPushButton *btnPrev_ = nullptr;
    QPushButton *btnPlayPause_ = nullptr;
    QPushButton *btnStop_ = nullptr;
    QPushButton *btnNext_ = nullptr;
    QPushButton *btnShuffle_ = nullptr;
    QPushButton *btnRepeat_ = nullptr;

    QPushButton *btnMute_ = nullptr;
    ClickableSlider *sliderVolume_ = nullptr;
    QLabel *lblVolume_ = nullptr;

    QCheckBox *chkAudioCapture_ = nullptr;
    QCheckBox *chkAudioMonitoring_ = nullptr;
    QCheckBox *chkMixerMonitoring_ = nullptr;

    QPushButton *btnSourceMute_ = nullptr;
    ClickableSlider *sliderSourceVolume_ = nullptr;
    QLabel *lblSourceVolume_ = nullptr;
    obs_fader_t *sourceFader_ = nullptr;
    obs_weak_source_t *weakSource_ = nullptr;

    QPushButton *btnMonitorMute_ = nullptr;
    ClickableSlider *sliderMonitorVolume_ = nullptr;
    QLabel *lblMonitorVolume_ = nullptr;
    obs_fader_t *monitorFader_ = nullptr;

    uint32_t currentDurationMs_ = 0;
};
