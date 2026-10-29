#include "aimp-overlay.hpp"

#include <obs-module.h>

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

AimpOverlay::AimpOverlay(obs_source_t *source)
    : source_(source)
{
    AimpController &ctrl = AimpController::instance();

    playerState_.store(ctrl.getState());
    trackInfo_ = ctrl.getTrackInfo();
    positionMs_ = ctrl.getPositionMs();
    durationMs_ = ctrl.getDurationMs();
    albumArt_ = ctrl.getAlbumArt();

    ctrl.addStateCallback(this, [this](AimpPlayerState s) {
        playerState_.store(s);
        std::lock_guard<std::mutex> lock(renderMutex_);
        if (s == AimpPlayerState::Stopped) {
            positionMs_ = 0;
            marqueeTitleOffset_ = 0.0f;
            marqueeArtistOffset_ = 0.0f;
        }
        needRedraw_ = true;
    });

    ctrl.addTrackCallback(this, [this](const AimpTrackInfo &info) {
        std::lock_guard<std::mutex> lock(renderMutex_);
        trackInfo_ = info;
        durationMs_ = info.durationMs;
        marqueeTitleOffset_ = 0.0f;
        marqueeArtistOffset_ = 0.0f;
        needRedraw_ = true;
    });

    ctrl.addPositionCallback(this, [this](uint32_t pos, uint32_t dur) {
        std::lock_guard<std::mutex> lock(renderMutex_);
        positionMs_ = pos;
        if (dur > 0) durationMs_ = dur;
        needRedraw_ = true;
    });

    ctrl.addAlbumArtCallback(this, [this](const QPixmap &art) {
        std::lock_guard<std::mutex> lock(renderMutex_);
        albumArt_ = art;
        needRedraw_ = true;
    });
}

AimpOverlay::~AimpOverlay()
{
    AimpController::instance().removeCallbacks(this);

    obs_enter_graphics();
    if (texture_) {
        gs_texture_destroy(texture_);
        texture_ = nullptr;
    }
    obs_leave_graphics();
}

void AimpOverlay::updateSettings(obs_data_t *settings)
{
    std::lock_guard<std::mutex> lock(renderMutex_);

    const char *themeId = obs_data_get_string(settings, "theme");
    settings_.themeId = (themeId && strlen(themeId) > 0) ? themeId : "default";

    char *themesDir = obs_module_file("themes");
    if (themesDir) {
        AimpThemeManager::instance().scanThemes(QString::fromUtf8(themesDir));
        bfree(themesDir);
    }
    currentTheme_ = AimpThemeManager::instance().getTheme(settings_.themeId);

    settings_.autoHide = obs_data_get_bool(settings, "auto_hide");
    settings_.marquee = obs_data_get_bool(settings, "marquee");
    settings_.scrollSpeed = static_cast<float>(obs_data_get_int(settings, "scroll_speed"));
    if (settings_.scrollSpeed < 5.0f) settings_.scrollSpeed = 40.0f;

    settings_.overrideColors = obs_data_get_bool(settings, "override_colors");
    if (settings_.overrideColors) {
        settings_.bgColor = QColor(static_cast<QRgb>(obs_data_get_int(settings, "color_background")));
        settings_.titleColor = QColor(static_cast<QRgb>(obs_data_get_int(settings, "color_title")));
        settings_.artistColor = QColor(static_cast<QRgb>(obs_data_get_int(settings, "color_artist")));
        settings_.timeColor = QColor(static_cast<QRgb>(obs_data_get_int(settings, "color_time")));
        settings_.progressFillColor = QColor(static_cast<QRgb>(obs_data_get_int(settings, "color_progress_fill")));
        settings_.progressBgColor = QColor(static_cast<QRgb>(obs_data_get_int(settings, "color_progress_bg")));
    }

    settings_.overrideFonts = obs_data_get_bool(settings, "override_fonts");
    if (settings_.overrideFonts) {
        obs_data_t *titleFontObj = obs_data_get_obj(settings, "font_title");
        if (titleFontObj) {
            settings_.titleFont = obs_data_get_string(titleFontObj, "face");
            settings_.titleSize = static_cast<int>(obs_data_get_int(titleFontObj, "size"));
            obs_data_release(titleFontObj);
        }
        obs_data_t *artistFontObj = obs_data_get_obj(settings, "font_artist");
        if (artistFontObj) {
            settings_.artistFont = obs_data_get_string(artistFontObj, "face");
            settings_.artistSize = static_cast<int>(obs_data_get_int(artistFontObj, "size"));
            obs_data_release(artistFontObj);
        }
    }

    width_ = static_cast<uint32_t>(currentTheme_.layout.width);
    height_ = static_cast<uint32_t>(currentTheme_.layout.height);

    if (overlayImage_.width() != static_cast<int>(width_) || overlayImage_.height() != static_cast<int>(height_)) {
        overlayImage_ = QImage(width_, height_, QImage::Format_ARGB32_Premultiplied);
    }

    needRedraw_ = true;
    textureNeedsUpdate_.store(true);
}

void AimpOverlay::formatTime(uint32_t ms, char *buf, size_t bufLen)
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

void AimpOverlay::videoTick(float seconds)
{
    float targetOpacity = 1.0f;
    AimpPlayerState state = playerState_.load();

    if (settings_.autoHide) {
        if (state != AimpPlayerState::Playing) {
            targetOpacity = 0.0f;
        }
    }

    constexpr float fadeSpeed = 4.0f; // ~250ms fade
    if (currentOpacity_ < targetOpacity) {
        currentOpacity_ = std::min(targetOpacity, currentOpacity_ + fadeSpeed * seconds);
        needRedraw_ = true;
    } else if (currentOpacity_ > targetOpacity) {
        currentOpacity_ = std::max(targetOpacity, currentOpacity_ - fadeSpeed * seconds);
        needRedraw_ = true;
    }

    if (settings_.marquee && currentOpacity_ > 0.01f && state == AimpPlayerState::Playing) {
        marqueeTitleOffset_ += settings_.scrollSpeed * seconds;
        marqueeArtistOffset_ += (settings_.scrollSpeed * 0.7f) * seconds;
        needRedraw_ = true;
    }

    if (needRedraw_ && currentOpacity_ > 0.001f) {
        std::lock_guard<std::mutex> lock(renderMutex_);
        renderImage();
        needRedraw_ = false;
        textureNeedsUpdate_.store(true);
    }
}

void AimpOverlay::renderImage()
{
    if (overlayImage_.isNull() || width_ == 0 || height_ == 0) return;

    overlayImage_.fill(Qt::transparent);
    if (currentOpacity_ <= 0.001f) return;

    QPainter painter(&overlayImage_);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(currentOpacity_);

    const auto &layout = currentTheme_.layout;
    const auto &colors = currentTheme_.colors;
    const auto &fonts = currentTheme_.fonts;

    QColor bgColor = settings_.overrideColors ? settings_.bgColor : colors.background;
    QColor borderColor = colors.border;
    qreal borderWidth = colors.borderWidth;

    QColor titleColor = settings_.overrideColors ? settings_.titleColor : colors.titleText;
    QColor artistColor = settings_.overrideColors ? settings_.artistColor : colors.artistText;
    QColor timeColor = settings_.overrideColors ? settings_.timeColor : colors.timeText;
    QColor progressFill = settings_.overrideColors ? settings_.progressFillColor : colors.progressFill;
    QColor progressFillEnd = settings_.overrideColors ? settings_.progressFillColor : colors.progressFillEnd;
    QColor progressBg = settings_.overrideColors ? settings_.progressBgColor : colors.progressBackground;

    // 1. Card Background
    QRectF cardRect(borderWidth / 2.0, borderWidth / 2.0, width_ - borderWidth, height_ - borderWidth);
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, layout.cornerRadius, layout.cornerRadius);

    painter.fillPath(cardPath, bgColor);
    if (borderWidth > 0.0) {
        QPen borderPen(borderColor, borderWidth);
        painter.strokePath(cardPath, borderPen);
    }

    // 2. Cover Art
    QRectF coverRect(layout.padding, layout.padding, layout.coverSize, layout.coverSize);
    QPainterPath coverClip;
    coverClip.addRoundedRect(coverRect, layout.coverRadius, layout.coverRadius);

    painter.save();
    painter.setClipPath(coverClip);

    if (!albumArt_.isNull()) {
        QPixmap scaledArt = albumArt_.scaled(layout.coverSize, layout.coverSize,
                                             Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int xOffset = (scaledArt.width() - layout.coverSize) / 2;
        int yOffset = (scaledArt.height() - layout.coverSize) / 2;
        painter.drawPixmap(coverRect.left(), coverRect.top(), scaledArt, xOffset, yOffset, layout.coverSize, layout.coverSize);
    } else if (!currentTheme_.placeholderCover.isNull()) {
        painter.drawPixmap(coverRect.toRect(), currentTheme_.placeholderCover);
    } else {
        painter.fillRect(coverRect, QColor(40, 44, 58));
    }
    painter.restore();

    // Cover border
    QPainterPath coverBorder;
    coverBorder.addRoundedRect(coverRect, layout.coverRadius, layout.coverRadius);
    painter.strokePath(coverBorder, QPen(QColor(255, 255, 255, 30), 1.0));

    // 3. Text Area & Layout
    qreal paddingLeft = layout.padding;
    qreal paddingRight = (layout.paddingRight >= 0)
                             ? static_cast<qreal>(layout.paddingRight)
                             : (layout.cornerRadius > layout.padding * 2)
                                   ? std::max(static_cast<qreal>(layout.padding), static_cast<qreal>(layout.cornerRadius * 0.65f))
                                   : static_cast<qreal>(layout.padding);

    qreal textX = paddingLeft + layout.coverSize + layout.coverSpacing;
    qreal textWidth = width_ - paddingRight - textX;
    if (textWidth < 40.0) {
        textWidth = 40.0;
    }
    qreal textY = layout.padding + 2;

    // Title Font & Text
    QString titleFontFamily = (settings_.overrideFonts && !settings_.titleFont.isEmpty()) ? settings_.titleFont : fonts.titleFont;
    int titleFontSize = settings_.overrideFonts ? settings_.titleSize : fonts.titleSize;
    QFont titleFont(titleFontFamily, titleFontSize, fonts.titleWeight);
    painter.setFont(titleFont);
    painter.setPen(titleColor);

    QString titleText = QString::fromStdWString(trackInfo_.title);
    if (titleText.isEmpty()) {
        titleText = QString::fromUtf8(obs_module_text(trackInfo_.active ? "Dock.UnknownTitle" : "Dock.NoTrack"));
    }

    QFontMetrics fmTitle(titleFont);
    int titleAdvance = fmTitle.horizontalAdvance(titleText);
    QRectF titleClipRect(textX, textY, textWidth, fmTitle.height() + 4);

    if (titleAdvance <= textWidth || !settings_.marquee) {
        painter.drawText(titleClipRect, Qt::AlignLeft | Qt::AlignVCenter, titleText);
    } else {
        // Marquee scrolling
        painter.save();
        painter.setClipRect(titleClipRect);
        QString sep = "     •     ";
        QString loopTitle = titleText + sep;
        int loopAdv = fmTitle.horizontalAdvance(loopTitle);
        float offset = fmod(marqueeTitleOffset_, static_cast<float>(loopAdv));

        painter.drawText(QRectF(textX - offset, textY, loopAdv, fmTitle.height() + 4), Qt::AlignLeft | Qt::AlignVCenter, loopTitle);
        painter.drawText(QRectF(textX - offset + loopAdv, textY, loopAdv, fmTitle.height() + 4), Qt::AlignLeft | Qt::AlignVCenter, loopTitle);
        painter.restore();
    }

    // Prepare Timings & Progress Bar geometry
    char posBuf[32], durBuf[32];
    formatTime(positionMs_, posBuf, sizeof(posBuf));
    formatTime(durationMs_, durBuf, sizeof(durBuf));
    QString timeText = QString("%1 / %2").arg(posBuf, durBuf);

    QFont timeFont(fonts.timeFont, fonts.timeSize, fonts.timeWeight);
    QFontMetrics fmTime(timeFont);
    qreal progressY = height_ - layout.progressBarBottomOffset - layout.progressBarHeight;
    qreal timeY = progressY - fmTime.height() - 4;

    // Artist Font & Text
    textY += fmTitle.height() + 4;
    QString artistFontFamily = (settings_.overrideFonts && !settings_.artistFont.isEmpty()) ? settings_.artistFont : fonts.artistFont;
    int artistFontSize = settings_.overrideFonts ? settings_.artistSize : fonts.artistSize;
    QFont artistFont(artistFontFamily, artistFontSize, fonts.artistWeight);
    painter.setFont(artistFont);
    painter.setPen(artistColor);

    QString artistText = QString::fromStdWString(trackInfo_.artist);
    if (artistText.isEmpty()) {
        artistText = QString::fromUtf8(obs_module_text("Dock.UnknownArtist"));
    }

    QFontMetrics fmArtist(artistFont);
    int artistAdvance = fmArtist.horizontalAdvance(artistText);

    // If artist and time share the same horizontal row (e.g. compact themes), reserve space for timeText
    qreal artistWidth = textWidth;
    if (textY + fmArtist.height() >= timeY - 2) {
        int timeAdvance = fmTime.horizontalAdvance(timeText);
        artistWidth = std::max(40.0, textWidth - timeAdvance - 12.0);
    }

    QRectF artistClipRect(textX, textY, artistWidth, fmArtist.height() + 4);

    if (artistAdvance <= artistWidth || !settings_.marquee) {
        painter.drawText(artistClipRect, Qt::AlignLeft | Qt::AlignVCenter, artistText);
    } else {
        // Marquee scrolling for artist
        painter.save();
        painter.setClipRect(artistClipRect);
        QString sep = "     •     ";
        QString loopArtist = artistText + sep;
        int loopAdv = fmArtist.horizontalAdvance(loopArtist);
        float offset = fmod(marqueeArtistOffset_, static_cast<float>(loopAdv));

        painter.drawText(QRectF(textX - offset, textY, loopAdv, fmArtist.height() + 4), Qt::AlignLeft | Qt::AlignVCenter, loopArtist);
        painter.drawText(QRectF(textX - offset + loopAdv, textY, loopAdv, fmArtist.height() + 4), Qt::AlignLeft | Qt::AlignVCenter, loopArtist);
        painter.restore();
    }

    // 4. Timings & Progress Bar
    painter.setFont(timeFont);
    painter.setPen(timeColor);

    QRectF timeRect(textX, timeY, textWidth, fmTime.height());
    painter.drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, timeText);

    // Progress Bar Track
    QRectF barBgRect(textX, progressY, textWidth, layout.progressBarHeight);
    QPainterPath barBgPath;
    barBgPath.addRoundedRect(barBgRect, layout.progressBarRadius, layout.progressBarRadius);
    painter.fillPath(barBgPath, progressBg);

    // Filled Progress
    float progress = (durationMs_ > 0) ? std::min(1.0f, static_cast<float>(positionMs_) / static_cast<float>(durationMs_)) : 0.0f;
    if (progress > 0.001f) {
        qreal fillW = textWidth * progress;
        QRectF fillRect(textX, progressY, fillW, layout.progressBarHeight);
        QPainterPath fillPath;
        fillPath.addRoundedRect(fillRect, layout.progressBarRadius, layout.progressBarRadius);

        QLinearGradient grad(fillRect.topLeft(), fillRect.topRight());
        grad.setColorAt(0.0, progressFill);
        grad.setColorAt(1.0, progressFillEnd);
        painter.fillPath(fillPath, grad);
    }
}

void AimpOverlay::videoRender()
{
    if (currentOpacity_ <= 0.001f || width_ == 0 || height_ == 0) return;

    if (!texture_ || gs_texture_get_width(texture_) != width_ || gs_texture_get_height(texture_) != height_) {
        if (texture_) {
            gs_texture_destroy(texture_);
        }
        texture_ = gs_texture_create(width_, height_, GS_BGRA, 1, nullptr, GS_DYNAMIC);
        textureNeedsUpdate_.store(true);
    }

    if (!texture_) return;

    if (textureNeedsUpdate_.exchange(false)) {
        std::lock_guard<std::mutex> lock(renderMutex_);
        if (!overlayImage_.isNull()) {
            gs_texture_set_image(texture_, overlayImage_.constBits(), width_ * 4, false);
        }
    }

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    while (gs_effect_loop(effect, "Draw")) {
        obs_source_draw(texture_, 0, 0, width_, height_, false);
    }

    gs_blend_state_pop();
}
