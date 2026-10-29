#pragma once

#include <obs.h>
#include <obs-source.h>
#include <graphics/graphics.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

#include "aimp-controller.hpp"
#include "aimp-overlay-theme.hpp"

struct AimpOverlaySettings {
    QString themeId = "default";
    bool autoHide = true;
    bool marquee = true;
    float scrollSpeed = 40.0f; // px per second

    bool overrideColors = false;
    QColor bgColor;
    QColor titleColor;
    QColor artistColor;
    QColor timeColor;
    QColor progressFillColor;
    QColor progressBgColor;

    bool overrideFonts = false;
    QString titleFont;
    int titleSize = 15;
    QString artistFont;
    int artistSize = 12;
    QString timeFont;
    int timeSize = 11;
};

class AimpOverlay {
public:
    explicit AimpOverlay(obs_source_t *source);
    ~AimpOverlay();

    // Non-copyable, non-movable
    AimpOverlay(const AimpOverlay &) = delete;
    AimpOverlay &operator=(const AimpOverlay &) = delete;

    void updateSettings(obs_data_t *settings);
    void videoTick(float seconds);
    void videoRender();

    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }

private:
    void renderImage();
    static void formatTime(uint32_t ms, char *buf, size_t bufLen);

    obs_source_t *source_ = nullptr;
    gs_texture_t *texture_ = nullptr;

    AimpOverlaySettings settings_;
    AimpTheme currentTheme_;

    uint32_t width_ = 520;
    uint32_t height_ = 130;

    std::mutex renderMutex_;
    QImage overlayImage_;
    bool needRedraw_ = true;
    std::atomic<bool> textureNeedsUpdate_{true};

    float currentOpacity_ = 0.0f;
    float marqueeTitleOffset_ = 0.0f;
    float marqueeArtistOffset_ = 0.0f;

    std::atomic<AimpPlayerState> playerState_{AimpPlayerState::Disconnected};
    AimpTrackInfo trackInfo_;
    uint32_t positionMs_ = 0;
    uint32_t durationMs_ = 0;
    QPixmap albumArt_;
};
