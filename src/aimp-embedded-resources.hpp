#pragma once

#include "aimp-dock.hpp"

#include <QByteArray>
#include <QColor>
#include <QIcon>

namespace AimpEmbeddedResources {

void initResources();
void cleanupResources();

QByteArray getEmbeddedMediaSvg(AimpControlDock::MediaIconType type);
QByteArray getEmbeddedAimpMonoSvg();
QByteArray getEmbeddedAimpIconSvg();

QIcon createMediaIcon(AimpControlDock::MediaIconType type, const QColor &color, int baseSize = 48);
QIcon createAimpMonoIcon(const QColor &color, int baseSize = 48);
QIcon createAimpIcon(int baseSize = 48);

} // namespace AimpEmbeddedResources
