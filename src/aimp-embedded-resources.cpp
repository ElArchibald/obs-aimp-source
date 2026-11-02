#include "aimp-embedded-resources.hpp"

#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

static void doInitResources()
{
    Q_INIT_RESOURCE(resources);
}

static void doCleanupResources()
{
    Q_CLEANUP_RESOURCE(resources);
}

namespace AimpEmbeddedResources {

void initResources()
{
    static struct Init {
        Init() {
            doInitResources();
        }
    } s_init;
}

void cleanupResources()
{
    doCleanupResources();
}

static const char *getMediaSvgPath(AimpControlDock::MediaIconType type)
{
    switch (type) {
    case AimpControlDock::MediaIconType::Prev:
        return ":/aimp/prev.svg";
    case AimpControlDock::MediaIconType::Play:
        return ":/aimp/play.svg";
    case AimpControlDock::MediaIconType::Pause:
        return ":/aimp/pause.svg";
    case AimpControlDock::MediaIconType::Stop:
        return ":/aimp/stop.svg";
    case AimpControlDock::MediaIconType::Next:
        return ":/aimp/next.svg";
    case AimpControlDock::MediaIconType::Shuffle:
        return ":/aimp/shuffle.svg";
    case AimpControlDock::MediaIconType::Repeat:
        return ":/aimp/repeat.svg";
    case AimpControlDock::MediaIconType::VolumeHigh:
        return ":/aimp/volume.svg";
    case AimpControlDock::MediaIconType::Mute:
        return ":/aimp/mute.svg";
    case AimpControlDock::MediaIconType::Headphones:
        return ":/aimp/headphones.svg";
    case AimpControlDock::MediaIconType::HeadphonesMute:
        return ":/aimp/headphones_mute.svg";
    default:
        return nullptr;
    }
}

QByteArray getEmbeddedMediaSvg(AimpControlDock::MediaIconType type)
{
    initResources();
    const char *path = getMediaSvgPath(type);
    if (!path) {
        return QByteArray();
    }
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        return file.readAll();
    }
    return QByteArray();
}

QByteArray getEmbeddedAimpMonoSvg()
{
    initResources();
    QFile file(":/aimp/aimp_icon_mono.svg");
    if (file.open(QIODevice::ReadOnly)) {
        return file.readAll();
    }
    return QByteArray();
}

QByteArray getEmbeddedAimpIconSvg()
{
    initResources();
    QFile file(":/aimp/aimp_icon.svg");
    if (file.open(QIODevice::ReadOnly)) {
        return file.readAll();
    }
    return QByteArray();
}

static QIcon renderSvgIcon(const QByteArray &svgData, const QColor &color, int baseSize, bool replaceColor = true)
{
    if (svgData.isEmpty()) {
        return QIcon();
    }

    QByteArray svgBytes;
    if (replaceColor) {
        QString svgStr = QString::fromUtf8(svgData);
        svgStr.replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb));
        svgBytes = svgStr.toUtf8();
    } else {
        svgBytes = svgData;
    }

    QSvgRenderer renderer(svgBytes);
    if (!renderer.isValid()) {
        return QIcon();
    }

    QIcon icon;
    const int sizes[] = {16, 20, 24, 32, baseSize, baseSize * 2};
    for (int s : sizes) {
        QPixmap pix(s, s);
        pix.fill(Qt::transparent);
        {
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (replaceColor && color.alphaF() < 1.0f) {
                p.setOpacity(color.alphaF());
            }
            renderer.render(&p, QRectF(0, 0, s, s));
        }
        icon.addPixmap(pix);
    }

    return icon;
}

QIcon createMediaIcon(AimpControlDock::MediaIconType type, const QColor &color, int baseSize)
{
    return renderSvgIcon(getEmbeddedMediaSvg(type), color, baseSize, true);
}

QIcon createAimpMonoIcon(const QColor &color, int baseSize)
{
    return renderSvgIcon(getEmbeddedAimpMonoSvg(), color, baseSize, true);
}

QIcon createAimpIcon(int baseSize)
{
    return renderSvgIcon(getEmbeddedAimpIconSvg(), QColor(), baseSize, false);
}

} // namespace AimpEmbeddedResources
