#include "aimp-dock.hpp"
#include "aimp-embedded-resources.hpp"
#include "aimp-source.hpp"

#include <obs-module.h>

#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>
#include <cstdio>

ClickableSlider::ClickableSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent)
{
    setStyleSheet(
        "QSlider::add-page:horizontal:disabled,"
        "QSlider::add-page:vertical:disabled {"
        "    background: transparent;"
        "}"
    );
}

void ClickableSlider::mousePressEvent(QMouseEvent *event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        setSliderDown(true);
        int val = pixelPosToRangeValue(event->pos());
        setValue(val);
        emit sliderMoved(val);
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void ClickableSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }
    if (dragging_) {
        int val = pixelPosToRangeValue(event->pos());
        setValue(val);
        emit sliderMoved(val);
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void ClickableSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (!isEnabled()) {
        dragging_ = false;
        setSliderDown(false);
        event->ignore();
        return;
    }
    if (dragging_ && (event->button() == Qt::LeftButton || (event->buttons() & Qt::LeftButton) == 0)) {
        dragging_ = false;
        setSliderDown(false);
        int val = pixelPosToRangeValue(event->pos());
        setValue(val);
        emit sliderReleased();
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void ClickableSlider::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange) {
        dragging_ = false;
        setSliderDown(false);
        update();
    }
    QSlider::changeEvent(event);
}

int ClickableSlider::pixelPosToRangeValue(const QPoint &pos) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    int pixelPos;
    int sliderMin;
    int sliderMax;
    int handleLength;

    if (orientation() == Qt::Horizontal) {
        pixelPos = pos.x();
        handleLength = handle.width();
        sliderMin = groove.left() + (handleLength / 2);
        sliderMax = groove.right() - (handleLength / 2) + 1;
    } else {
        pixelPos = pos.y();
        handleLength = handle.height();
        sliderMin = groove.top() + (handleLength / 2);
        sliderMax = groove.bottom() - (handleLength / 2) + 1;
    }

    if (sliderMax <= sliderMin) {
        return minimum();
    }

    int sliderValue = QStyle::sliderValueFromPosition(minimum(), maximum(), pixelPos - sliderMin,
                                                      sliderMax - sliderMin, opt.upsideDown);
    return std::clamp(sliderValue, minimum(), maximum());
}

static void onSourceFaderCallback(void *param, float db)
{
    auto *dock = static_cast<AimpControlDock *>(param);
    QMetaObject::invokeMethod(dock, [dock, db]() {
        dock->onSourceFaderChanged(db);
    }, Qt::QueuedConnection);
}

static void onSourceMuteCallback(void *param, calldata_t *cd)
{
    auto *dock = static_cast<AimpControlDock *>(param);
    bool muted = calldata_bool(cd, "muted");
    QMetaObject::invokeMethod(dock, [dock, muted]() {
        dock->onSourceMuted(muted);
    }, Qt::QueuedConnection);
}

static void onGlobalSourceCreate(void *param, calldata_t *cd)
{
    auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
    if (source) {
        const char *id = obs_source_get_id(source);
        if (id && strcmp(id, "obs_aimp_source") == 0) {
            auto *dock = static_cast<AimpControlDock *>(param);
            QMetaObject::invokeMethod(dock, &AimpControlDock::refreshSourceAttachment, Qt::QueuedConnection);
        }
    }
}

static void onGlobalSourceDestroy(void *param, calldata_t *cd)
{
    auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
    if (source) {
        const char *id = obs_source_get_id(source);
        if (id && strcmp(id, "obs_aimp_source") == 0) {
            auto *dock = static_cast<AimpControlDock *>(param);
            QMetaObject::invokeMethod(dock, &AimpControlDock::refreshSourceAttachment, Qt::QueuedConnection);
        }
    }
}

AimpControlDock::AimpControlDock(QWidget *parent)
    : QDockWidget(parent)
{
    setWindowTitle(QString::fromUtf8(obs_module_text("DockTitle")));
    setObjectName("AimpControlDockWidget");
    setWindowIcon(QIcon());

    monitorFader_ = obs_fader_create(OBS_FADER_LOG);
    sourceFader_ = obs_fader_create(OBS_FADER_LOG);
    obs_fader_add_callback(sourceFader_, onSourceFaderCallback, this);

    signal_handler_connect(obs_get_signal_handler(), "source_create", onGlobalSourceCreate, this);
    signal_handler_connect(obs_get_signal_handler(), "source_destroy", onGlobalSourceDestroy, this);

    setupUi();
    updateIcons();

    AimpController &ctrl = AimpController::instance();

    connect(&ctrl, &AimpController::stateChanged, this, &AimpControlDock::onStateChanged);
    connect(&ctrl, &AimpController::trackChanged, this, &AimpControlDock::onTrackChanged);
    connect(&ctrl, &AimpController::positionChanged, this, &AimpControlDock::onPositionChanged);
    connect(&ctrl, &AimpController::volumeChanged, this, &AimpControlDock::onVolumeChanged);
    connect(&ctrl, &AimpController::playbackModesChanged, this, &AimpControlDock::onPlaybackModesChanged);
    connect(&ctrl, &AimpController::albumArtChanged, this, &AimpControlDock::onAlbumArtChanged);
    connect(&ctrl, &AimpController::connectionChanged, this, &AimpControlDock::onConnectionChanged);
    connect(&ctrl, &AimpController::audioCaptureChanged, this, &AimpControlDock::onAudioCaptureChanged);
    connect(&ctrl, &AimpController::monitoringChanged, this, &AimpControlDock::onMonitoringChanged);
    connect(&ctrl, &AimpController::monitorMuteChanged, this, &AimpControlDock::onMonitorMuted);

    // Initial state
    bool isConn = ctrl.isRunning();
    AimpPlayerState state = ctrl.getState();
    updateStatusDisplay(state, isConn);
    onTrackChanged(ctrl.getTrackInfo());
    onPositionChanged(ctrl.getPositionMs(), ctrl.getDurationMs());
    onVolumeChanged(ctrl.getVolume(), ctrl.isMuted());
    onPlaybackModesChanged(ctrl.isRepeat(), ctrl.isShuffle());
    onAlbumArtChanged(ctrl.getAlbumArt());
    onAudioCaptureChanged(ctrl.isAudioCapture());
    onMonitoringChanged(ctrl.isAudioMonitoring(), ctrl.isMixerMonitoring(), ctrl.getMonitorVolumeDb());
    onMonitorMuted(ctrl.isMonitorMuted());

    refreshSourceAttachment();
}

AimpControlDock::~AimpControlDock()
{
    signal_handler_disconnect(obs_get_signal_handler(), "source_create", onGlobalSourceCreate, this);
    signal_handler_disconnect(obs_get_signal_handler(), "source_destroy", onGlobalSourceDestroy, this);

    detachSource();

    if (sourceFader_) {
        obs_fader_remove_callback(sourceFader_, onSourceFaderCallback, this);
        obs_fader_destroy(sourceFader_);
        sourceFader_ = nullptr;
    }

    if (monitorFader_) {
        obs_fader_destroy(monitorFader_);
        monitorFader_ = nullptr;
    }
}

void AimpControlDock::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ThemeChange) {
        updateIcons();
        updateStatusDisplay(AimpController::instance().getState(), AimpController::instance().isRunning());
        if (AimpController::instance().getAlbumArt().isNull()) {
            onAlbumArtChanged(QPixmap());
        }
    }
    QDockWidget::changeEvent(event);
}

void AimpControlDock::formatTime(uint32_t ms, char *buf, size_t bufLen)
{
    uint32_t totalSec = ms / 1000;
    uint32_t hours = totalSec / 3600;
    uint32_t mins = (totalSec % 3600) / 60;
    uint32_t secs = totalSec % 60;

    if (hours > 0) {
        snprintf(buf, bufLen, "%u:%02u:%02u", hours, mins, secs);
    } else {
        snprintf(buf, bufLen, "%02u:%02u", mins, secs);
    }
}

QPixmap AimpControlDock::createStatusIcon(StatusIconType type, const QColor &color)
{
    const int s = 16;
    QPixmap pix(s, s);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    switch (type) {
    case StatusIconType::DisconnectedWarning: {
        QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        QPolygonF tri;
        tri << QPointF(8.0, 2.0)
            << QPointF(14.5, 13.5)
            << QPointF(1.5, 13.5);
        p.drawPolygon(tri);

        // Exclamation mark: vertical stroke and dot
        p.drawLine(QPointF(8.0, 5.5), QPointF(8.0, 9.2));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(7.0, 11.0, 2.0, 2.0));
        break;
    }
    case StatusIconType::Playing: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPolygonF tri;
        tri << QPointF(4.0, 3.0)
            << QPointF(13.0, 8.0)
            << QPointF(4.0, 13.0);
        p.drawPolygon(tri);
        break;
    }
    case StatusIconType::Paused: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(3.5, 3.0, 3.0, 10.0), 1.0, 1.0);
        p.drawRoundedRect(QRectF(9.5, 3.0, 3.0, 10.0), 1.0, 1.0);
        break;
    }
    case StatusIconType::Stopped: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(3.5, 3.5, 9.0, 9.0), 1.5, 1.5);
        break;
    }
    }

    p.end();
    return pix;
}

QIcon AimpControlDock::createAimpMonoIcon(const QColor &color)
{
    return AimpEmbeddedResources::createAimpMonoIcon(color);
}

QIcon AimpControlDock::createMediaIcon(MediaIconType type, const QColor &color)
{
    return AimpEmbeddedResources::createMediaIcon(type, color);
}

QIcon AimpControlDock::createToggleIcon(MediaIconType type, const QColor &offColor, const QColor &onColor)
{
    QIcon icon;
    QIcon offIcon = createMediaIcon(type, offColor);
    QIcon onIcon = createMediaIcon(type, onColor);

    const int sizes[] = {16, 20, 24, 32, 48, 96};
    for (int s : sizes) {
        QPixmap offPix = offIcon.pixmap(s, s);
        QPixmap onPix = onIcon.pixmap(s, s);

        icon.addPixmap(offPix, QIcon::Normal, QIcon::Off);
        icon.addPixmap(offPix, QIcon::Active, QIcon::Off);
        icon.addPixmap(offPix, QIcon::Selected, QIcon::Off);

        icon.addPixmap(onPix, QIcon::Normal, QIcon::On);
        icon.addPixmap(onPix, QIcon::Active, QIcon::On);
        icon.addPixmap(onPix, QIcon::Selected, QIcon::On);
    }
    return icon;
}

void AimpControlDock::setupUi()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // 1. Status Bar & Quick Launch Button
    auto *statusContainer = new QWidget(central);
    statusContainer->setFixedHeight(28);

    auto *statusLayout = new QHBoxLayout(statusContainer);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(6);

    lblStatusDot_ = new QLabel(statusContainer);
    lblStatusDot_->setFixedSize(16, 16);
    lblStatusDot_->setAlignment(Qt::AlignCenter);

    lblStatus_ = new QLabel(QString::fromUtf8(obs_module_text("Dock.Status.Disconnected")), statusContainer);

    btnLaunchAimp_ = new QPushButton(QString::fromUtf8(obs_module_text("Dock.LaunchAimp")), statusContainer);
    btnLaunchAimp_->setToolTip(QString::fromUtf8(obs_module_text("Dock.LaunchAimpTooltip")));
    btnLaunchAimp_->setFixedHeight(26);
    btnLaunchAimp_->setIcon(createAimpMonoIcon(palette().color(QPalette::ButtonText)));
    btnLaunchAimp_->setIconSize(QSize(16, 16));
    connect(btnLaunchAimp_, &QPushButton::clicked, this, &AimpControlDock::onBtnLaunchAimpClicked);

    statusLayout->addWidget(lblStatusDot_);
    statusLayout->addWidget(lblStatus_);
    statusLayout->addStretch();
    statusLayout->addWidget(btnLaunchAimp_);
    mainLayout->addWidget(statusContainer);

    // 2. Track Info with Album Art
    auto *trackLayout = new QHBoxLayout();
    lblCover_ = new QLabel(central);
    lblCover_->setFixedSize(68, 68);
    lblCover_->setAlignment(Qt::AlignCenter);

    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);
    lblTitle_ = new QLabel(QString::fromUtf8(obs_module_text("Dock.NoTrack")), central);
    lblTitle_->setWordWrap(true);
    QFont titleFont = lblTitle_->font();
    titleFont.setBold(true);
    lblTitle_->setFont(titleFont);

    lblArtist_ = new QLabel(QString::fromUtf8(obs_module_text("Dock.UnknownArtist")), central);
    lblArtist_->setWordWrap(true);

    infoLayout->addWidget(lblTitle_);
    infoLayout->addWidget(lblArtist_);
    infoLayout->addStretch();

    trackLayout->addWidget(lblCover_);
    trackLayout->addLayout(infoLayout, 1);
    mainLayout->addLayout(trackLayout);

    // 3. Seek Bar
    auto *seekLayout = new QHBoxLayout();
    lblElapsed_ = new QLabel("00:00", central);
    lblElapsed_->setFixedWidth(40);
    lblElapsed_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(qMax(8, monoFont.pointSize() - 1));
    lblElapsed_->setFont(monoFont);

    sliderSeek_ = new ClickableSlider(Qt::Horizontal, central);
    sliderSeek_->setRange(0, 100);
    sliderSeek_->setValue(0);
    sliderSeek_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Seek")));
    connect(sliderSeek_, &QSlider::sliderMoved, this, &AimpControlDock::onSeekSliderMoved);
    connect(sliderSeek_, &QSlider::sliderReleased, this, &AimpControlDock::onSeekSliderReleased);

    lblDuration_ = new QLabel("00:00", central);
    lblDuration_->setFixedWidth(40);
    lblDuration_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lblDuration_->setFont(monoFont);

    seekLayout->addWidget(lblElapsed_);
    seekLayout->addWidget(sliderSeek_);
    seekLayout->addWidget(lblDuration_);
    mainLayout->addLayout(seekLayout);

    // 4. Playback Controls Toolbar
    auto *ctrlLayout = new QHBoxLayout();
    ctrlLayout->setSpacing(4);

    const QSize btnSize(30, 28);
    const QSize iconSize(18, 18);

    btnPrev_ = new QPushButton(central);
    btnPrev_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Prev")));
    btnPrev_->setFixedSize(btnSize);
    btnPrev_->setIconSize(iconSize);
    connect(btnPrev_, &QPushButton::clicked, this, &AimpControlDock::onBtnPrevClicked);

    btnPlayPause_ = new QPushButton(central);
    btnPlayPause_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Play")));
    btnPlayPause_->setFixedSize(QSize(34, 28));
    btnPlayPause_->setIconSize(QSize(20, 20));
    connect(btnPlayPause_, &QPushButton::clicked, this, &AimpControlDock::onBtnPlayPauseClicked);

    btnStop_ = new QPushButton(central);
    btnStop_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Stop")));
    btnStop_->setFixedSize(btnSize);
    btnStop_->setIconSize(iconSize);
    connect(btnStop_, &QPushButton::clicked, this, &AimpControlDock::onBtnStopClicked);

    btnNext_ = new QPushButton(central);
    btnNext_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Next")));
    btnNext_->setFixedSize(btnSize);
    btnNext_->setIconSize(iconSize);
    connect(btnNext_, &QPushButton::clicked, this, &AimpControlDock::onBtnNextClicked);

    btnShuffle_ = new QPushButton(central);
    btnShuffle_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Shuffle")));
    btnShuffle_->setCheckable(true);
    btnShuffle_->setFixedSize(btnSize);
    btnShuffle_->setIconSize(iconSize);
    connect(btnShuffle_, &QPushButton::clicked, this, &AimpControlDock::onBtnShuffleClicked);

    btnRepeat_ = new QPushButton(central);
    btnRepeat_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Repeat")));
    btnRepeat_->setCheckable(true);
    btnRepeat_->setFixedSize(btnSize);
    btnRepeat_->setIconSize(iconSize);
    connect(btnRepeat_, &QPushButton::clicked, this, &AimpControlDock::onBtnRepeatClicked);

    ctrlLayout->addStretch();
    ctrlLayout->addWidget(btnPrev_);
    ctrlLayout->addWidget(btnPlayPause_);
    ctrlLayout->addWidget(btnStop_);
    ctrlLayout->addWidget(btnNext_);
    ctrlLayout->addWidget(btnShuffle_);
    ctrlLayout->addWidget(btnRepeat_);
    ctrlLayout->addStretch();
    mainLayout->addLayout(ctrlLayout);

    // 5. Volume Control
    auto *volLayout = new QHBoxLayout();
    btnMute_ = new QPushButton(central);
    btnMute_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Mute")));
    btnMute_->setFixedSize(QSize(28, 28));
    btnMute_->setIconSize(iconSize);
    connect(btnMute_, &QPushButton::clicked, this, &AimpControlDock::onBtnMuteClicked);

    sliderVolume_ = new ClickableSlider(Qt::Horizontal, central);
    sliderVolume_->setRange(0, 100);
    sliderVolume_->setValue(100);
    sliderVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Volume")));
    connect(sliderVolume_, &QSlider::valueChanged, this, &AimpControlDock::onVolumeSliderChanged);

    lblVolume_ = new QLabel("100%", central);
    lblVolume_->setFixedWidth(36);
    lblVolume_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblVolume_->setFont(monoFont);
    lblVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Volume")));

    volLayout->addWidget(btnMute_);
    volLayout->addWidget(sliderVolume_);
    volLayout->addWidget(lblVolume_);
    mainLayout->addLayout(volLayout);

    // 6. OBS Audio Capture & Monitoring Controls
    auto *sep = new QFrame(central);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep);

    auto *capCheckLayout = new QHBoxLayout();
    chkAudioCapture_ = new QCheckBox(QString::fromUtf8(obs_module_text("Dock.AudioCapture")), central);
    chkAudioCapture_->setToolTip(QString::fromUtf8(obs_module_text("Prop.AudioCapture.Desc")));
    connect(chkAudioCapture_, &QCheckBox::toggled, this, &AimpControlDock::onChkAudioCaptureToggled);
    capCheckLayout->addWidget(chkAudioCapture_);
    capCheckLayout->addStretch();
    mainLayout->addLayout(capCheckLayout);

    auto *monCheckLayout = new QHBoxLayout();
    monCheckLayout->setSpacing(12);

    chkAudioMonitoring_ = new QCheckBox(QString::fromUtf8(obs_module_text("Dock.AudioMonitoring")), central);
    chkAudioMonitoring_->setToolTip(QString::fromUtf8(obs_module_text("Prop.AudioMonitoring.Desc")));
    chkMixerMonitoring_ = new QCheckBox(QString::fromUtf8(obs_module_text("Dock.MixerMonitoring")), central);
    chkMixerMonitoring_->setToolTip(QString::fromUtf8(obs_module_text("Prop.MixerMonitoring.Desc")));

    connect(chkAudioMonitoring_, &QCheckBox::toggled, this, &AimpControlDock::onChkAudioMonitoringToggled);
    connect(chkMixerMonitoring_, &QCheckBox::toggled, this, &AimpControlDock::onChkMixerMonitoringToggled);

    monCheckLayout->addWidget(chkAudioMonitoring_);
    monCheckLayout->addWidget(chkMixerMonitoring_);
    monCheckLayout->addStretch();
    mainLayout->addLayout(monCheckLayout);

    // 7. OBS Mixer Source Volume
    auto *srcVolLayout = new QHBoxLayout();
    btnSourceMute_ = new QPushButton(central);
    btnSourceMute_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceMute.Mute")));
    btnSourceMute_->setFixedSize(QSize(28, 28));
    btnSourceMute_->setIconSize(iconSize);
    connect(btnSourceMute_, &QPushButton::clicked, this, &AimpControlDock::onBtnSourceMuteClicked);

    sliderSourceVolume_ = new ClickableSlider(Qt::Horizontal, central);
    sliderSourceVolume_->setRange(0, 1000);
    sliderSourceVolume_->setValue(1000);
    sliderSourceVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceVolume")));
    connect(sliderSourceVolume_, &QSlider::valueChanged, this, &AimpControlDock::onSourceVolumeSliderChanged);

    lblSourceVolume_ = new QLabel("0.0 dB", central);
    lblSourceVolume_->setFixedWidth(54);
    lblSourceVolume_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblSourceVolume_->setFont(monoFont);
    lblSourceVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceVolume")));

    srcVolLayout->addWidget(btnSourceMute_);
    srcVolLayout->addWidget(sliderSourceVolume_);
    srcVolLayout->addWidget(lblSourceVolume_);
    mainLayout->addLayout(srcVolLayout);

    // 8. Audio Monitoring Volume
    auto *monVolLayout = new QHBoxLayout();

    btnMonitorMute_ = new QPushButton(central);
    btnMonitorMute_->setToolTip(QString::fromUtf8(obs_module_text("Dock.MonitorMute.Mute")));
    btnMonitorMute_->setFixedSize(QSize(28, 28));
    btnMonitorMute_->setIconSize(iconSize);
    connect(btnMonitorMute_, &QPushButton::clicked, this, &AimpControlDock::onBtnMonitorMuteClicked);

    sliderMonitorVolume_ = new ClickableSlider(Qt::Horizontal, central);
    sliderMonitorVolume_->setRange(0, 1000);
    sliderMonitorVolume_->setValue(1000);
    sliderMonitorVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.MonitorVolume")));
    connect(sliderMonitorVolume_, &QSlider::valueChanged, this, &AimpControlDock::onMonitorVolumeSliderChanged);

    lblMonitorVolume_ = new QLabel("0.0 dB", central);
    lblMonitorVolume_->setFixedWidth(54);
    lblMonitorVolume_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblMonitorVolume_->setFont(monoFont);
    lblMonitorVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.MonitorVolume")));

    monVolLayout->addWidget(btnMonitorMute_);
    monVolLayout->addWidget(sliderMonitorVolume_);
    monVolLayout->addWidget(lblMonitorVolume_);
    mainLayout->addLayout(monVolLayout);

    mainLayout->addStretch();
    setWidget(central);
}

void AimpControlDock::updatePlaybackModeIcons()
{
    QColor normalColor = palette().color(QPalette::ButtonText);
    const QColor activeColor(255, 255, 255);

    if (btnRepeat_) {
        bool isRepeat = btnRepeat_->isChecked();
        btnRepeat_->setIcon(createMediaIcon(MediaIconType::Repeat, isRepeat ? activeColor : normalColor));
        btnRepeat_->setToolTip(QString::fromUtf8(obs_module_text(isRepeat ? "Dock.Btn.Repeat.Disable" : "Dock.Btn.Repeat.Enable")));
    }

    if (btnShuffle_) {
        bool isShuffle = btnShuffle_->isChecked();
        btnShuffle_->setIcon(createMediaIcon(MediaIconType::Shuffle, isShuffle ? activeColor : normalColor));
        btnShuffle_->setToolTip(QString::fromUtf8(obs_module_text(isShuffle ? "Dock.Btn.Shuffle.Disable" : "Dock.Btn.Shuffle.Enable")));
    }
}

void AimpControlDock::updateIcons()
{
    QColor btnTextColor = palette().color(QPalette::ButtonText);
    const QColor activeColor(255, 255, 255);

    btnPrev_->setIcon(createMediaIcon(MediaIconType::Prev, btnTextColor));
    btnStop_->setIcon(createMediaIcon(MediaIconType::Stop, btnTextColor));
    btnNext_->setIcon(createMediaIcon(MediaIconType::Next, btnTextColor));
    btnShuffle_->setIcon(createToggleIcon(MediaIconType::Shuffle, btnTextColor, activeColor));
    btnRepeat_->setIcon(createToggleIcon(MediaIconType::Repeat, btnTextColor, activeColor));

    AimpPlayerState state = AimpController::instance().getState();
    bool isPlaying = (state == AimpPlayerState::Playing);
    btnPlayPause_->setIcon(createMediaIcon(isPlaying ? MediaIconType::Pause : MediaIconType::Play, btnTextColor));
    btnPlayPause_->setToolTip(QString::fromUtf8(obs_module_text(isPlaying ? "Dock.Btn.Pause" : "Dock.Btn.Play")));

    bool isMuted = AimpController::instance().isMuted();
    btnMute_->setIcon(createMediaIcon(isMuted ? MediaIconType::Mute : MediaIconType::VolumeHigh,
                                      isMuted ? QColor(231, 76, 60) : btnTextColor));
    btnMute_->setToolTip(QString::fromUtf8(obs_module_text(isMuted ? "Dock.Btn.Unmute" : "Dock.Btn.Mute")));

    if (btnSourceMute_) {
        obs_source_t *src = getAttachedSource();
        bool srcMuted = src ? obs_source_muted(src) : false;
        if (src) obs_source_release(src);

        btnSourceMute_->setIcon(createMediaIcon(srcMuted ? MediaIconType::Mute : MediaIconType::VolumeHigh,
                                                srcMuted ? QColor(231, 76, 60) : btnTextColor));
        btnSourceMute_->setToolTip(QString::fromUtf8(obs_module_text(srcMuted ? "Dock.SourceMute.Unmute" : "Dock.SourceMute.Mute")));
    }

    if (btnLaunchAimp_) {
        btnLaunchAimp_->setIcon(createAimpMonoIcon(btnTextColor));
        btnLaunchAimp_->setIconSize(QSize(16, 16));
    }

    if (btnMonitorMute_) {
        obs_source_t *src = getAttachedSource();
        bool monMuted = src ? aimp_source_get_monitor_muted(src) : AimpController::instance().isMonitorMuted();
        if (src) obs_source_release(src);

        btnMonitorMute_->setIcon(createMediaIcon(monMuted ? MediaIconType::HeadphonesMute : MediaIconType::Headphones,
                                                 monMuted ? QColor(231, 76, 60) : btnTextColor));
        btnMonitorMute_->setToolTip(QString::fromUtf8(obs_module_text(monMuted ? "Dock.MonitorMute.Unmute" : "Dock.MonitorMute.Mute")));
    }

    updatePlaybackModeIcons();
}

void AimpControlDock::updateStatusDisplay(AimpPlayerState state, bool connected)
{
    QColor textColor = palette().color(QPalette::WindowText);
    if (!textColor.isValid()) {
        textColor = QColor(220, 222, 230);
    }

    if (!connected || state == AimpPlayerState::Disconnected) {
        lblStatusDot_->setPixmap(createStatusIcon(StatusIconType::DisconnectedWarning, textColor));
        lblStatus_->setText(QString::fromUtf8(obs_module_text("Dock.Status.Disconnected")));
        btnLaunchAimp_->setVisible(true);
    } else {
        btnLaunchAimp_->setVisible(false);
        switch (state) {
        case AimpPlayerState::Playing:
            lblStatusDot_->setPixmap(createStatusIcon(StatusIconType::Playing, textColor));
            lblStatus_->setText(QString::fromUtf8(obs_module_text("Dock.Status.Playing")));
            break;
        case AimpPlayerState::Paused:
            lblStatusDot_->setPixmap(createStatusIcon(StatusIconType::Paused, textColor));
            lblStatus_->setText(QString::fromUtf8(obs_module_text("Dock.Status.Paused")));
            break;
        case AimpPlayerState::Stopped:
        default:
            lblStatusDot_->setPixmap(createStatusIcon(StatusIconType::Stopped, textColor));
            lblStatus_->setText(QString::fromUtf8(obs_module_text("Dock.Status.Stopped")));
            break;
        }
    }
}

void AimpControlDock::onStateChanged(AimpPlayerState state)
{
    updateStatusDisplay(state, state != AimpPlayerState::Disconnected);

    QColor btnTextColor = palette().color(QPalette::ButtonText);
    if (state == AimpPlayerState::Playing) {
        btnPlayPause_->setIcon(createMediaIcon(MediaIconType::Pause, btnTextColor));
        btnPlayPause_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Pause")));
    } else {
        btnPlayPause_->setIcon(createMediaIcon(MediaIconType::Play, btnTextColor));
        btnPlayPause_->setToolTip(QString::fromUtf8(obs_module_text("Dock.Btn.Play")));
    }

    if (state == AimpPlayerState::Stopped) {
        onPositionChanged(0, currentDurationMs_);
    }
}

void AimpControlDock::onTrackChanged(const AimpTrackInfo &info)
{
    QString title = QString::fromStdWString(info.title);
    if (title.isEmpty()) {
        title = QString::fromUtf8(obs_module_text(info.active ? "Dock.UnknownTitle" : "Dock.NoTrack"));
    }
    lblTitle_->setText(title);

    QString artist = QString::fromStdWString(info.artist);
    if (artist.isEmpty()) {
        artist = QString::fromUtf8(obs_module_text("Dock.UnknownArtist"));
    }
    lblArtist_->setText(artist);
}

void AimpControlDock::onPositionChanged(uint32_t posMs, uint32_t durMs)
{
    currentDurationMs_ = durMs;

    char posBuf[32], durBuf[32];
    formatTime(posMs, posBuf, sizeof(posBuf));
    formatTime(durMs, durBuf, sizeof(durBuf));

    lblElapsed_->setText(posBuf);
    lblDuration_->setText(durBuf);

    if (!isSeeking_) {
        sliderSeek_->blockSignals(true);
        sliderSeek_->setRange(0, static_cast<int>(durMs / 1000));
        sliderSeek_->setValue(static_cast<int>(posMs / 1000));
        sliderSeek_->blockSignals(false);
    }
}

void AimpControlDock::onVolumeChanged(int volume, bool muted)
{
    sliderVolume_->blockSignals(true);
    sliderVolume_->setValue(volume);
    sliderVolume_->blockSignals(false);

    lblVolume_->setText(QString("%1%").arg(volume));

    QColor btnTextColor = palette().color(QPalette::ButtonText);
    btnMute_->setIcon(createMediaIcon(muted ? MediaIconType::Mute : MediaIconType::VolumeHigh,
                                      muted ? QColor(231, 76, 60) : btnTextColor));
    btnMute_->setToolTip(QString::fromUtf8(obs_module_text(muted ? "Dock.Btn.Unmute" : "Dock.Btn.Mute")));
}

void AimpControlDock::onPlaybackModesChanged(bool repeat, bool shuffle)
{
    btnRepeat_->blockSignals(true);
    btnRepeat_->setChecked(repeat);
    btnRepeat_->blockSignals(false);

    btnShuffle_->blockSignals(true);
    btnShuffle_->setChecked(shuffle);
    btnShuffle_->blockSignals(false);

    updatePlaybackModeIcons();
}

void AimpControlDock::onAlbumArtChanged(const QPixmap &art)
{
    if (!art.isNull()) {
        QPixmap rounded(68, 68);
        rounded.fill(Qt::transparent);

        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QPainterPath path;
        path.addRoundedRect(0, 0, 68, 68, 8, 8);
        p.setClipPath(path);
        p.drawPixmap(0, 0, 68, 68, art.scaled(68, 68, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        p.end();

        lblCover_->setPixmap(rounded);
    } else {
        // Fallback placeholder using theme colors and vector musical note
        QColor bgColor = palette().color(QPalette::Base);
        QColor borderColor = palette().color(QPalette::Mid);
        QColor noteColor = palette().color(QPalette::PlaceholderText);
        if (!noteColor.isValid()) {
            noteColor = palette().color(QPalette::WindowText);
        }

        QPixmap ph(68, 68);
        ph.fill(Qt::transparent);
        QPainter p(&ph);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(bgColor);
        p.setPen(QPen(borderColor, 1));
        p.drawRoundedRect(QRectF(0.5, 0.5, 67, 67), 8, 8);

        // Vector double musical note
        p.setBrush(noteColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(19, 39, 10, 8));
        p.drawEllipse(QRectF(37, 33, 10, 8));
        p.setPen(QPen(noteColor, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(28, 42), QPointF(28, 20));
        p.drawLine(QPointF(46, 36), QPointF(46, 14));
        p.setPen(QPen(noteColor, 4.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(28, 20), QPointF(46, 14));
        p.end();

        lblCover_->setPixmap(ph);
    }
}

void AimpControlDock::onConnectionChanged(bool connected)
{
    updateStatusDisplay(AimpController::instance().getState(), connected);
}

void AimpControlDock::onBtnPlayPauseClicked()
{
    AimpController::instance().playPause();
}

void AimpControlDock::onBtnStopClicked()
{
    AimpController::instance().stop();
    onPositionChanged(0, currentDurationMs_);
}

void AimpControlDock::onBtnPrevClicked()
{
    AimpController::instance().prev();
}

void AimpControlDock::onBtnNextClicked()
{
    AimpController::instance().next();
}

void AimpControlDock::onBtnShuffleClicked()
{
    bool cur = AimpController::instance().isShuffle();
    AimpController::instance().setTrackShuffle(!cur);
    updatePlaybackModeIcons();
}

void AimpControlDock::onBtnRepeatClicked()
{
    bool cur = AimpController::instance().isRepeat();
    AimpController::instance().setTrackRepeat(!cur);
    updatePlaybackModeIcons();
}

void AimpControlDock::onBtnMuteClicked()
{
    bool cur = AimpController::instance().isMuted();
    AimpController::instance().setMute(!cur);
}

void AimpControlDock::onBtnLaunchAimpClicked()
{
    if (!AimpController::instance().launchAimp()) {
        QMessageBox::warning(this,
                             QString::fromUtf8(obs_module_text("DockTitle")),
                             QString::fromUtf8(obs_module_text("Dock.AimpNotFound")));
    }
}

void AimpControlDock::onSeekSliderMoved(int value)
{
    isSeeking_ = true;
    char buf[32];
    formatTime(static_cast<uint32_t>(value * 1000), buf, sizeof(buf));
    lblElapsed_->setText(buf);
}

void AimpControlDock::onSeekSliderReleased()
{
    isSeeking_ = false;
    uint32_t newPosMs = static_cast<uint32_t>(sliderSeek_->value() * 1000);
    AimpController::instance().setPosition(newPosMs);
}

void AimpControlDock::onVolumeSliderChanged(int value)
{
    AimpController::instance().setVolume(value);
}

void AimpControlDock::onChkAudioCaptureToggled(bool checked)
{
    if (sliderSourceVolume_) {
        sliderSourceVolume_->setEnabled(checked);
        sliderSourceVolume_->update();
    }
    if (btnSourceMute_) btnSourceMute_->setEnabled(checked);
    if (lblSourceVolume_) lblSourceVolume_->setEnabled(checked);

    chkAudioMonitoring_->setEnabled(checked);
    bool monActive = checked && chkAudioMonitoring_->isChecked();
    chkMixerMonitoring_->setEnabled(monActive);
    if (sliderMonitorVolume_) {
        sliderMonitorVolume_->setEnabled(monActive);
        sliderMonitorVolume_->update();
    }
    if (btnMonitorMute_) btnMonitorMute_->setEnabled(monActive);
    if (lblMonitorVolume_) lblMonitorVolume_->setEnabled(monActive);

    AimpController::instance().setAudioCapture(checked);
}

void AimpControlDock::onAudioCaptureChanged(bool audioCapture)
{
    if (chkAudioCapture_) {
        if (weakSource_) {
            chkAudioCapture_->setEnabled(true);
        }
        chkAudioCapture_->blockSignals(true);
        chkAudioCapture_->setChecked(audioCapture);
        chkAudioCapture_->blockSignals(false);
    }

    if (sliderSourceVolume_) {
        sliderSourceVolume_->setEnabled(audioCapture);
        sliderSourceVolume_->update();
    }
    if (btnSourceMute_) btnSourceMute_->setEnabled(audioCapture);
    if (lblSourceVolume_) lblSourceVolume_->setEnabled(audioCapture);

    if (chkAudioMonitoring_) {
        chkAudioMonitoring_->setEnabled(audioCapture);
        bool monActive = audioCapture && chkAudioMonitoring_->isChecked();
        if (chkMixerMonitoring_) chkMixerMonitoring_->setEnabled(monActive);
        if (sliderMonitorVolume_) {
            sliderMonitorVolume_->setEnabled(monActive);
            sliderMonitorVolume_->update();
        }
        if (btnMonitorMute_) btnMonitorMute_->setEnabled(monActive);
        if (lblMonitorVolume_) lblMonitorVolume_->setEnabled(monActive);
    }
}

void AimpControlDock::onChkAudioMonitoringToggled(bool checked)
{
    bool cap = chkAudioCapture_ ? chkAudioCapture_->isChecked() : true;
    chkMixerMonitoring_->setEnabled(cap && checked);
    if (sliderMonitorVolume_) {
        sliderMonitorVolume_->setEnabled(cap && checked);
        sliderMonitorVolume_->update();
    }
    if (btnMonitorMute_) btnMonitorMute_->setEnabled(cap && checked);
    if (lblMonitorVolume_) lblMonitorVolume_->setEnabled(cap && checked);

    AimpController::instance().setMonitoringState(checked, chkMixerMonitoring_->isChecked(), AimpController::instance().getMonitorVolumeDb());
}

void AimpControlDock::onChkMixerMonitoringToggled(bool checked)
{
    AimpController::instance().setMonitoringState(chkAudioMonitoring_->isChecked(), checked, AimpController::instance().getMonitorVolumeDb());
}

void AimpControlDock::onSourceVolumeSliderChanged(int value)
{
    if (!sourceFader_) return;
    float def = static_cast<float>(value) / 1000.0f;
    obs_fader_set_deflection(sourceFader_, def);
    float db = obs_fader_get_db(sourceFader_);
    lblSourceVolume_->setText(formatDb(db));
}

void AimpControlDock::onBtnSourceMuteClicked()
{
    obs_source_t *source = getAttachedSource();
    if (source) {
        bool muted = obs_source_muted(source);
        obs_source_set_muted(source, !muted);
        obs_source_release(source);
    }
}

void AimpControlDock::onSourceFaderChanged(float db)
{
    if (!sourceFader_ || !sliderSourceVolume_) return;
    float def = obs_fader_get_deflection(sourceFader_);
    int sliderVal = static_cast<int>(std::round(def * 1000.0f));

    sliderSourceVolume_->blockSignals(true);
    sliderSourceVolume_->setValue(sliderVal);
    sliderSourceVolume_->blockSignals(false);

    lblSourceVolume_->setText(formatDb(db));
}

void AimpControlDock::onSourceMuted(bool muted)
{
    if (btnSourceMute_) {
        QColor btnTextColor = palette().color(QPalette::ButtonText);
        btnSourceMute_->setIcon(createMediaIcon(muted ? MediaIconType::Mute : MediaIconType::VolumeHigh,
                                                muted ? QColor(231, 76, 60) : btnTextColor));
        btnSourceMute_->setToolTip(QString::fromUtf8(obs_module_text(muted ? "Dock.SourceMute.Unmute" : "Dock.SourceMute.Mute")));
    }
}

void AimpControlDock::onBtnMonitorMuteClicked()
{
    obs_source_t *source = getAttachedSource();
    if (source) {
        bool muted = aimp_source_get_monitor_muted(source);
        aimp_source_set_monitor_muted(source, !muted);
        obs_source_release(source);
    } else {
        bool muted = AimpController::instance().isMonitorMuted();
        AimpController::instance().setMonitorMute(!muted);
    }
}

void AimpControlDock::onMonitorMuted(bool muted)
{
    if (btnMonitorMute_) {
        QColor btnTextColor = palette().color(QPalette::ButtonText);
        btnMonitorMute_->setIcon(createMediaIcon(muted ? MediaIconType::HeadphonesMute : MediaIconType::Headphones,
                                                 muted ? QColor(231, 76, 60) : btnTextColor));
        btnMonitorMute_->setToolTip(QString::fromUtf8(obs_module_text(muted ? "Dock.MonitorMute.Unmute" : "Dock.MonitorMute.Mute")));
    }
}

void AimpControlDock::onMonitorVolumeSliderChanged(int value)
{
    float def = static_cast<float>(value) / 1000.0f;
    obs_fader_set_deflection(monitorFader_, def);
    float db = obs_fader_get_db(monitorFader_);
    float monDb = (db <= -96.0f || std::isinf(db)) ? -INFINITY : (std::round(db * 10.0f) / 10.0f);
    if (monDb > 0.0f) monDb = 0.0f;
    lblMonitorVolume_->setText(formatDb(monDb));
    AimpController::instance().setMonitoringState(chkAudioMonitoring_->isChecked(), chkMixerMonitoring_->isChecked(), monDb);
}

void AimpControlDock::onMonitoringChanged(bool audioMon, bool mixerMon, float monVolDb)
{
    bool cap = chkAudioCapture_ ? chkAudioCapture_->isChecked() : true;

    chkAudioMonitoring_->blockSignals(true);
    chkAudioMonitoring_->setChecked(audioMon);
    chkAudioMonitoring_->setEnabled(cap);
    chkAudioMonitoring_->blockSignals(false);

    chkMixerMonitoring_->blockSignals(true);
    chkMixerMonitoring_->setChecked(mixerMon);
    chkMixerMonitoring_->setEnabled(cap && audioMon);
    chkMixerMonitoring_->blockSignals(false);

    obs_fader_set_db(monitorFader_, monVolDb);
    float def = obs_fader_get_deflection(monitorFader_);
    int sliderVal = static_cast<int>(std::round(def * 1000.0f));

    sliderMonitorVolume_->blockSignals(true);
    sliderMonitorVolume_->setValue(sliderVal);
    sliderMonitorVolume_->setEnabled(cap && audioMon);
    sliderMonitorVolume_->update();
    sliderMonitorVolume_->blockSignals(false);

    if (btnMonitorMute_) btnMonitorMute_->setEnabled(cap && audioMon);
    lblMonitorVolume_->setEnabled(cap && audioMon);
    lblMonitorVolume_->setText(formatDb(monVolDb));
}

QString AimpControlDock::formatDb(float db)
{
    if (db <= -96.0f || std::isinf(db)) {
        return "-inf dB";
    }
    float rounded = std::round(db * 10.0f) / 10.0f;
    if (std::abs(rounded) < 0.001f) {
        rounded = 0.0f;
    }
    return QString::number(rounded, 'f', 1) + " dB";
}

obs_source_t *AimpControlDock::findAimpSource()
{
    obs_source_t *found = nullptr;
    obs_enum_sources([](void *param, obs_source_t *source) -> bool {
        const char *id = obs_source_get_id(source);
        if (id && strcmp(id, "obs_aimp_source") == 0) {
            *static_cast<obs_source_t **>(param) = obs_source_get_ref(source);
            return false;
        }
        return true;
    }, &found);
    return found;
}

obs_source_t *AimpControlDock::getAttachedSource() const
{
    if (!weakSource_) return nullptr;
    return obs_weak_source_get_source(weakSource_);
}

void AimpControlDock::refreshSourceAttachment()
{
    obs_source_t *source = findAimpSource();
    attachToAimpSource(source);
    if (source) {
        obs_source_release(source);
    }
}

void AimpControlDock::attachToAimpSource(obs_source_t *source)
{
    obs_source_t *current = getAttachedSource();
    if (current == source && (source != nullptr || weakSource_ == nullptr)) {
        if (current) obs_source_release(current);
        return;
    }
    if (current) {
        obs_source_release(current);
    }

    detachSource();

    QColor btnTextColor = palette().color(QPalette::ButtonText);

    if (source) {
        weakSource_ = obs_source_get_weak_source(source);
        obs_fader_attach_source(sourceFader_, source);
        signal_handler_connect(obs_source_get_signal_handler(source), "mute", onSourceMuteCallback, this);

        float def = obs_fader_get_deflection(sourceFader_);
        float db = obs_fader_get_db(sourceFader_);

        sliderSourceVolume_->blockSignals(true);
        sliderSourceVolume_->setValue(static_cast<int>(std::round(def * 1000.0f)));
        sliderSourceVolume_->blockSignals(false);
        sliderSourceVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceVolume")));

        lblSourceVolume_->setText(formatDb(db));

        bool muted = obs_source_muted(source);
        btnSourceMute_->setIcon(createMediaIcon(muted ? MediaIconType::Mute : MediaIconType::VolumeHigh,
                                                muted ? QColor(231, 76, 60) : btnTextColor));
        btnSourceMute_->setToolTip(QString::fromUtf8(obs_module_text(muted ? "Dock.SourceMute.Unmute" : "Dock.SourceMute.Mute")));

        if (chkAudioCapture_) {
            chkAudioCapture_->setEnabled(true);
        }

        obs_data_t *settings = obs_source_get_settings(source);
        if (settings) {
            bool cap = obs_data_get_bool(settings, "audio_capture");
            bool mon = obs_data_get_bool(settings, "audio_monitoring");
            bool mixerMon = obs_data_get_bool(settings, "mixer_monitoring");
            float monVol = static_cast<float>(obs_data_get_double(settings, "monitor_volume_db"));
            bool monMuted = aimp_source_get_monitor_muted(source);
            obs_data_release(settings);

            AimpController::instance().notifyAudioCapture(cap);
            AimpController::instance().notifyMonitoringSettings(mon, mixerMon, monVol);
            AimpController::instance().notifyMonitorMute(monMuted);

            if (chkAudioCapture_) {
                chkAudioCapture_->setEnabled(true);
                chkAudioCapture_->blockSignals(true);
                chkAudioCapture_->setChecked(cap);
                chkAudioCapture_->blockSignals(false);
            }

            if (chkAudioMonitoring_) {
                chkAudioMonitoring_->setEnabled(cap);
                chkAudioMonitoring_->blockSignals(true);
                chkAudioMonitoring_->setChecked(mon);
                chkAudioMonitoring_->blockSignals(false);
            }

            bool monActive = cap && mon;

            if (chkMixerMonitoring_) {
                chkMixerMonitoring_->setEnabled(monActive);
                chkMixerMonitoring_->blockSignals(true);
                chkMixerMonitoring_->setChecked(mixerMon);
                chkMixerMonitoring_->blockSignals(false);
            }

            sliderSourceVolume_->setEnabled(cap);
            sliderSourceVolume_->update();
            lblSourceVolume_->setEnabled(cap);
            btnSourceMute_->setEnabled(cap);

            sliderMonitorVolume_->setEnabled(monActive);
            sliderMonitorVolume_->update();
            lblMonitorVolume_->setEnabled(monActive);

            if (btnMonitorMute_) {
                btnMonitorMute_->setEnabled(monActive);
                btnMonitorMute_->setIcon(createMediaIcon(monMuted ? MediaIconType::HeadphonesMute : MediaIconType::Headphones,
                                                         monMuted ? QColor(231, 76, 60) : btnTextColor));
                btnMonitorMute_->setToolTip(QString::fromUtf8(obs_module_text(monMuted ? "Dock.MonitorMute.Unmute" : "Dock.MonitorMute.Mute")));
            }
        }
    } else {
        sliderSourceVolume_->setEnabled(false);
        sliderSourceVolume_->update();
        sliderSourceVolume_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceNotFound")));
        lblSourceVolume_->setEnabled(false);
        lblSourceVolume_->setText("-- dB");
        btnSourceMute_->setEnabled(false);
        btnSourceMute_->setIcon(createMediaIcon(MediaIconType::VolumeHigh, btnTextColor));
        btnSourceMute_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceNotFound")));

        if (chkAudioCapture_) chkAudioCapture_->setEnabled(false);
        if (chkAudioMonitoring_) chkAudioMonitoring_->setEnabled(false);
        if (chkMixerMonitoring_) chkMixerMonitoring_->setEnabled(false);
        if (sliderMonitorVolume_) {
            sliderMonitorVolume_->setEnabled(false);
            sliderMonitorVolume_->update();
        }
        if (btnMonitorMute_) {
            btnMonitorMute_->setEnabled(false);
            btnMonitorMute_->setIcon(createMediaIcon(MediaIconType::Headphones, btnTextColor));
            btnMonitorMute_->setToolTip(QString::fromUtf8(obs_module_text("Dock.SourceNotFound")));
        }
        if (lblMonitorVolume_) lblMonitorVolume_->setEnabled(false);
    }
}

void AimpControlDock::detachSource()
{
    if (weakSource_) {
        obs_source_t *src = obs_weak_source_get_source(weakSource_);
        if (src) {
            signal_handler_disconnect(obs_source_get_signal_handler(src), "mute", onSourceMuteCallback, this);
            obs_source_release(src);
        }
        obs_weak_source_release(weakSource_);
        weakSource_ = nullptr;
    }
    if (sourceFader_) {
        obs_fader_detach_source(sourceFader_);
    }
}
