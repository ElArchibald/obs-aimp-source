#pragma once

#include <map>
#include <string>
#include <vector>

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QString>

struct AimpThemeLayout {
    int width = 520;
    int height = 130;
    int cornerRadius = 16;
    int padding = 14;
    int paddingRight = -1;
    int coverSize = 102;
    int coverRadius = 10;
    int coverSpacing = 16;
    int progressBarHeight = 6;
    int progressBarRadius = 3;
    int progressBarBottomOffset = 14;
};

struct AimpThemeFonts {
    QString titleFont = "Segoe UI";
    int titleSize = 15;
    int titleWeight = 700;

    QString artistFont = "Segoe UI";
    int artistSize = 12;
    int artistWeight = 500;

    QString timeFont = "Consolas";
    int timeSize = 11;
    int timeWeight = 400;
};

struct AimpThemeColors {
    QColor background = QColor(26, 27, 36, 230);
    QColor border = QColor(58, 61, 84, 128);
    qreal borderWidth = 1.5;

    QColor titleText = QColor(255, 255, 255);
    QColor artistText = QColor(162, 167, 189);
    QColor timeText = QColor(142, 147, 171);

    QColor progressBackground = QColor(40, 43, 60, 128);
    QColor progressFill = QColor(255, 122, 0);
    QColor progressFillEnd = QColor(255, 164, 77);
    QColor glowColor = QColor(255, 122, 0, 64);
};

struct AimpTheme {
    QString id = "default";
    QString name = "Default Dark Glass";
    QString author = "Antigravity";
    QString version = "1.0.0";
    QString description;
    QString folderPath;

    AimpThemeLayout layout;
    AimpThemeFonts fonts;
    AimpThemeColors colors;

    QString placeholderCoverPath;
    QPixmap placeholderCover;

    std::map<std::string, QString> localizedNames;
    std::map<std::string, QString> localizedDescriptions;

    QString getDisplayName(const char *locale = nullptr) const;
    QString getDisplayDescription(const char *locale = nullptr) const;
};

class AimpThemeManager {
public:
    static AimpThemeManager &instance();

    void scanThemes(const QString &themesDirectory);
    std::vector<AimpTheme> getAvailableThemes() const;
    AimpTheme getTheme(const QString &themeId) const;
    static bool loadThemeFromManifest(const QString &manifestPath, AimpTheme &outTheme);
    static void loadThemeLocalization(const QString &themeFolder, AimpTheme &outTheme);

private:
    AimpThemeManager() = default;
    std::map<QString, AimpTheme> themes_;
};
