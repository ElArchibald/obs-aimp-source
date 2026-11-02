#include "aimp-overlay-theme.hpp"

#include <obs.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>

QString AimpTheme::getDisplayName(const char *locale) const
{
    if (localizedNames.empty()) {
        return name;
    }

    std::string loc;
    if (locale && *locale) {
        loc = locale;
    } else {
        const char *cur = obs_get_locale();
        if (cur && *cur) loc = cur;
    }

    if (!loc.empty()) {
        auto it = localizedNames.find(loc);
        if (it != localizedNames.end() && !it->second.isEmpty()) {
            return it->second;
        }

        for (const auto &pair : localizedNames) {
            if (_stricmp(pair.first.c_str(), loc.c_str()) == 0 && !pair.second.isEmpty()) {
                return pair.second;
            }
        }

        std::string prefix = loc.substr(0, 2);
        for (const auto &pair : localizedNames) {
            if (pair.first.length() >= 2 && _strnicmp(pair.first.c_str(), prefix.c_str(), 2) == 0 && !pair.second.isEmpty()) {
                return pair.second;
            }
        }
    }

    auto itEn = localizedNames.find("en-US");
    if (itEn != localizedNames.end() && !itEn->second.isEmpty()) {
        return itEn->second;
    }
    for (const auto &pair : localizedNames) {
        if (_strnicmp(pair.first.c_str(), "en", 2) == 0 && !pair.second.isEmpty()) {
            return pair.second;
        }
    }

    return name;
}

QString AimpTheme::getDisplayDescription(const char *locale) const
{
    if (localizedDescriptions.empty()) {
        return description;
    }

    std::string loc;
    if (locale && *locale) {
        loc = locale;
    } else {
        const char *cur = obs_get_locale();
        if (cur && *cur) loc = cur;
    }

    if (!loc.empty()) {
        auto it = localizedDescriptions.find(loc);
        if (it != localizedDescriptions.end() && !it->second.isEmpty()) {
            return it->second;
        }

        for (const auto &pair : localizedDescriptions) {
            if (_stricmp(pair.first.c_str(), loc.c_str()) == 0 && !pair.second.isEmpty()) {
                return pair.second;
            }
        }

        std::string prefix = loc.substr(0, 2);
        for (const auto &pair : localizedDescriptions) {
            if (pair.first.length() >= 2 && _strnicmp(pair.first.c_str(), prefix.c_str(), 2) == 0 && !pair.second.isEmpty()) {
                return pair.second;
            }
        }
    }

    auto itEn = localizedDescriptions.find("en-US");
    if (itEn != localizedDescriptions.end() && !itEn->second.isEmpty()) {
        return itEn->second;
    }

    return description;
}

AimpThemeManager &AimpThemeManager::instance()
{
    static AimpThemeManager inst;
    return inst;
}

void AimpThemeManager::scanThemes(const QString &themesDirectory)
{
    themes_.clear();

    QDir dir(themesDirectory);
    if (!dir.exists()) {
        AimpTheme defaultTheme;
        themes_[defaultTheme.id] = defaultTheme;
        return;
    }

    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subdir : subdirs) {
        QString manifestPath = dir.filePath(subdir + "/manifest.json");
        if (QFile::exists(manifestPath)) {
            AimpTheme theme;
            if (loadThemeFromManifest(manifestPath, theme)) {
                themes_[theme.id] = theme;
            }
        }
    }

    if (themes_.empty()) {
        AimpTheme defaultTheme;
        themes_[defaultTheme.id] = defaultTheme;
    }
}

static QColor parseColor(const QJsonValue &val, const QColor &fallback)
{
    if (val.isString()) {
        QString s = val.toString().trimmed();
        if (s.startsWith('#')) {
            bool ok = false;
            if (s.length() == 7) {
                // #RRGGBB
                QColor c(s);
                if (c.isValid()) return c;
            } else if (s.length() == 9) {
                // #RRGGBBAA
                int r = s.mid(1, 2).toInt(&ok, 16);
                int g = s.mid(3, 2).toInt(&ok, 16);
                int b = s.mid(5, 2).toInt(&ok, 16);
                int a = s.mid(7, 2).toInt(&ok, 16);
                if (ok) {
                    return QColor(r, g, b, a);
                }
            } else if (s.length() == 4) {
                // #RGB
                QColor c(s);
                if (c.isValid()) return c;
            }
        }
    }
    return fallback;
}

bool AimpThemeManager::loadThemeFromManifest(const QString &manifestPath, AimpTheme &outTheme)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    QJsonObject root = doc.object();
    QFileInfo fi(manifestPath);
    QString themeFolder = fi.absolutePath();

    outTheme.folderPath = themeFolder;
    outTheme.id = root.value("id").toString(fi.dir().dirName());
    outTheme.name = root.value("name").toString(outTheme.id);
    outTheme.author = root.value("author").toString();
    outTheme.version = root.value("version").toString("1.0.0");
    outTheme.description = root.value("description").toString();

    // Layout
    if (root.contains("layout") && root.value("layout").isObject()) {
        QJsonObject lay = root.value("layout").toObject();
        outTheme.layout.width = lay.value("width").toInt(outTheme.layout.width);
        outTheme.layout.height = lay.value("height").toInt(outTheme.layout.height);
        outTheme.layout.cornerRadius = lay.value("cornerRadius").toInt(outTheme.layout.cornerRadius);
        outTheme.layout.padding = lay.value("padding").toInt(outTheme.layout.padding);
        outTheme.layout.paddingRight = lay.value("paddingRight").toInt(outTheme.layout.paddingRight);
        outTheme.layout.coverSize = lay.value("coverSize").toInt(outTheme.layout.coverSize);
        outTheme.layout.coverRadius = lay.value("coverRadius").toInt(outTheme.layout.coverRadius);
        outTheme.layout.coverSpacing = lay.value("coverSpacing").toInt(outTheme.layout.coverSpacing);
        outTheme.layout.progressBarHeight = lay.value("progressBarHeight").toInt(outTheme.layout.progressBarHeight);
        outTheme.layout.progressBarRadius = lay.value("progressBarRadius").toInt(outTheme.layout.progressBarRadius);
        outTheme.layout.progressBarBottomOffset = lay.value("progressBarBottomOffset").toInt(outTheme.layout.progressBarBottomOffset);
    }

    // Fonts
    if (root.contains("fonts") && root.value("fonts").isObject()) {
        QJsonObject f = root.value("fonts").toObject();
        outTheme.fonts.titleFont = f.value("titleFont").toString(outTheme.fonts.titleFont);
        outTheme.fonts.titleSize = f.value("titleSize").toInt(outTheme.fonts.titleSize);
        outTheme.fonts.titleWeight = f.value("titleWeight").toInt(outTheme.fonts.titleWeight);

        outTheme.fonts.artistFont = f.value("artistFont").toString(outTheme.fonts.artistFont);
        outTheme.fonts.artistSize = f.value("artistSize").toInt(outTheme.fonts.artistSize);
        outTheme.fonts.artistWeight = f.value("artistWeight").toInt(outTheme.fonts.artistWeight);

        outTheme.fonts.timeFont = f.value("timeFont").toString(outTheme.fonts.timeFont);
        outTheme.fonts.timeSize = f.value("timeSize").toInt(outTheme.fonts.timeSize);
        outTheme.fonts.timeWeight = f.value("timeWeight").toInt(outTheme.fonts.timeWeight);
    }

    // Colors
    if (root.contains("colors") && root.value("colors").isObject()) {
        QJsonObject c = root.value("colors").toObject();
        outTheme.colors.background = parseColor(c.value("background"), outTheme.colors.background);
        outTheme.colors.border = parseColor(c.value("border"), outTheme.colors.border);
        outTheme.colors.borderWidth = c.value("borderWidth").toDouble(outTheme.colors.borderWidth);

        outTheme.colors.titleText = parseColor(c.value("titleText"), outTheme.colors.titleText);
        outTheme.colors.artistText = parseColor(c.value("artistText"), outTheme.colors.artistText);
        outTheme.colors.timeText = parseColor(c.value("timeText"), outTheme.colors.timeText);

        outTheme.colors.progressBackground = parseColor(c.value("progressBackground"), outTheme.colors.progressBackground);
        outTheme.colors.progressFill = parseColor(c.value("progressFill"), outTheme.colors.progressFill);
        outTheme.colors.progressFillEnd = parseColor(c.value("progressFillEnd"), outTheme.colors.progressFillEnd);
        outTheme.colors.glowColor = parseColor(c.value("glowColor"), outTheme.colors.glowColor);
    }

    // Assets
    if (root.contains("assets") && root.value("assets").isObject()) {
        QJsonObject a = root.value("assets").toObject();
        QString coverName = a.value("placeholderCover").toString("placeholder.png");
        outTheme.placeholderCoverPath = themeFolder + "/" + coverName;
        if (QFile::exists(outTheme.placeholderCoverPath)) {
            outTheme.placeholderCover.load(outTheme.placeholderCoverPath);
        }
    }

    // Load separate localization file (locale.json, locale.ini, or locale/ folder)
    loadThemeLocalization(themeFolder, outTheme);

    return true;
}

void AimpThemeManager::loadThemeLocalization(const QString &themeFolder, AimpTheme &outTheme)
{
    // 1. Separate locale.json in theme folder
    QString localeJsonPath = themeFolder + "/locale.json";
    if (QFile::exists(localeJsonPath)) {
        QFile file(localeJsonPath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
            file.close();
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject root = doc.object();
                for (auto it = root.begin(); it != root.end(); ++it) {
                    std::string locKey = it.key().trimmed().toStdString();
                    if (it.value().isObject()) {
                        QJsonObject locObj = it.value().toObject();
                        if (locObj.contains("name")) {
                            outTheme.localizedNames[locKey] = locObj.value("name").toString();
                        }
                        if (locObj.contains("description")) {
                            outTheme.localizedDescriptions[locKey] = locObj.value("description").toString();
                        }
                    } else if (it.value().isString()) {
                        outTheme.localizedNames[locKey] = it.value().toString();
                    }
                }
            }
        }
    }

    // 2. Separate locale.ini in theme folder
    QString localeIniPath = themeFolder + "/locale.ini";
    if (QFile::exists(localeIniPath)) {
        QSettings ini(localeIniPath, QSettings::IniFormat);
        const QStringList groups = ini.childGroups();
        if (!groups.isEmpty()) {
            for (const QString &grp : groups) {
                ini.beginGroup(grp);
                std::string locKey = grp.trimmed().toStdString();
                if (ini.contains("name")) {
                    outTheme.localizedNames[locKey] = ini.value("name").toString();
                }
                if (ini.contains("description")) {
                    outTheme.localizedDescriptions[locKey] = ini.value("description").toString();
                }
                ini.endGroup();
            }
        } else {
            const QStringList keys = ini.childKeys();
            for (const QString &k : keys) {
                std::string locKey = k.trimmed().toStdString();
                outTheme.localizedNames[locKey] = ini.value(k).toString();
            }
        }
    }

    // 3. Subfolder themeFolder/locale/ containing <lang>.json or <lang>.ini
    QDir locDir(themeFolder + "/locale");
    if (locDir.exists()) {
        const QStringList locFiles = locDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &f : locFiles) {
            QFileInfo fi(locDir.filePath(f));
            std::string locKey = fi.baseName().trimmed().toStdString();
            if (fi.suffix().compare("json", Qt::CaseInsensitive) == 0) {
                QFile file(fi.absoluteFilePath());
                if (file.open(QIODevice::ReadOnly)) {
                    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                    file.close();
                    if (doc.isObject()) {
                        QJsonObject o = doc.object();
                        if (o.contains("name")) {
                            outTheme.localizedNames[locKey] = o.value("name").toString();
                        }
                        if (o.contains("description")) {
                            outTheme.localizedDescriptions[locKey] = o.value("description").toString();
                        }
                    }
                }
            } else if (fi.suffix().compare("ini", Qt::CaseInsensitive) == 0) {
                QSettings ini(fi.absoluteFilePath(), QSettings::IniFormat);
                if (ini.contains("name")) {
                    outTheme.localizedNames[locKey] = ini.value("name").toString();
                }
                if (ini.contains("description")) {
                    outTheme.localizedDescriptions[locKey] = ini.value("description").toString();
                }
            }
        }
    }
}

std::vector<AimpTheme> AimpThemeManager::getAvailableThemes() const
{
    std::vector<AimpTheme> result;
    result.reserve(themes_.size());
    for (const auto &pair : themes_) {
        result.push_back(pair.second);
    }
    return result;
}

AimpTheme AimpThemeManager::getTheme(const QString &themeId) const
{
    auto it = themes_.find(themeId);
    if (it != themes_.end()) {
        return it->second;
    }
    if (!themes_.empty()) {
        return themes_.begin()->second;
    }
    return AimpTheme();
}
