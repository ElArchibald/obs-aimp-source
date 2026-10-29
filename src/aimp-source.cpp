#include "aimp-source.hpp"
#include "aimp-audio.hpp"
#include "aimp-overlay-theme.hpp"
#include "aimp-overlay.hpp"

#include <callback/calldata.h>
#include <callback/signal.h>
#include <obs-audio-controls.h>

#include <cmath>
#include <memory>
#include <string>

struct AimpSourceContext {
    obs_source_t *source = nullptr;
    obs_source_t *monitorSource = nullptr;
    std::unique_ptr<AimpOverlay> overlay;
    std::unique_ptr<AimpAudioCapture> audio;
    bool audioEnabled = true;
    bool audioMonitoring = false;
    bool mixerMonitoring = false;
    float monitorVolumeDb = 0.0f;
    bool monitorMuted = false;
    bool monitorSourceIsPublic = false;
    float volumeDb = 0.0f;
    bool settingVolume = false;
    bool inMuteSync = false;
};

// -------------------------------------------------------------
// Companion Monitor Source Callbacks
// -------------------------------------------------------------
static const char *aimp_monitor_source_get_name(void *)
{
    return obs_module_text("AimpMonitorSource");
}

struct AimpMonitorSourceContext {
    obs_source_t *source = nullptr;
    obs_source_t *parentSource = nullptr;
};

static void *aimp_monitor_source_create(obs_data_t *, obs_source_t *source)
{
    auto *ctx = new AimpMonitorSourceContext();
    ctx->source = source;
    obs_source_set_monitoring_enabled(source, true);
    obs_source_set_audio_mixers(source, 0);
    return ctx;
}

static void aimp_monitor_source_destroy(void *data)
{
    auto *ctx = static_cast<AimpMonitorSourceContext *>(data);
    delete ctx;
}

static void aimp_monitor_volume_changed(void *data, calldata_t *params)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (!ctx || ctx->settingVolume || !ctx->source) return;

    float vol = static_cast<float>(calldata_float(params, "volume"));
    float db = (vol <= 0.00001f) ? -INFINITY : obs_mul_to_db(vol);
    if (db > 0.0f) db = 0.0f;
    if (db < -96.0f) db = -INFINITY;
    if (!std::isinf(db)) db = std::round(db * 10.0f) / 10.0f;

    bool isInfPrev = (ctx->monitorVolumeDb <= -96.0f || std::isinf(ctx->monitorVolumeDb));
    bool isInfCur = (db <= -96.0f || std::isinf(db));
    bool changed = (isInfPrev != isInfCur) || (!isInfCur && std::abs(ctx->monitorVolumeDb - db) >= 0.05f);

    if (changed) {
        ctx->monitorVolumeDb = db;
        obs_data_t *settings = obs_source_get_settings(ctx->source);
        if (settings) {
            obs_data_set_double(settings, "monitor_volume_db", isInfCur ? -96.0 : db);
            obs_data_release(settings);
        }
        AimpController::instance().notifyMonitoringSettings(ctx->audioMonitoring, ctx->mixerMonitoring, ctx->monitorVolumeDb);
        obs_source_update_properties(ctx->source);
    }
}

static void aimp_monitor_mute_changed(void *data, calldata_t *params)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (!ctx || ctx->inMuteSync || !ctx->source) return;

    bool muted = calldata_bool(params, "muted");
    if (ctx->monitorMuted != muted) {
        ctx->inMuteSync = true;
        ctx->monitorMuted = muted;
        if (ctx->monitorSource) {
            obs_source_set_monitoring_enabled(ctx->monitorSource, !muted);
        }
        obs_data_t *settings = obs_source_get_settings(ctx->source);
        if (settings) {
            obs_data_set_bool(settings, "monitor_muted", muted);
            obs_data_release(settings);
        }
        AimpController::instance().notifyMonitorMute(muted);
        ctx->inMuteSync = false;
    }
}

static void aimp_monitor_enabled_changed(void *data, calldata_t *params)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (!ctx || ctx->inMuteSync || !ctx->source) return;

    bool enabled = calldata_bool(params, "monitor");
    bool muted = !enabled;
    if (ctx->monitorMuted != muted) {
        ctx->inMuteSync = true;
        ctx->monitorMuted = muted;
        if (ctx->monitorSource) {
            obs_source_set_muted(ctx->monitorSource, muted);
        }
        obs_data_t *settings = obs_source_get_settings(ctx->source);
        if (settings) {
            obs_data_set_bool(settings, "monitor_muted", muted);
            obs_data_release(settings);
        }
        AimpController::instance().notifyMonitorMute(muted);
        ctx->inMuteSync = false;
    }
}

static void aimp_monitor_source_destroyed(void *data, calldata_t *)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (ctx) {
        ctx->monitorSource = nullptr;
        if (ctx->audio) {
            ctx->audio->setMonitorSource(nullptr);
        }
    }
}

static void aimp_source_volume_changed(void *data, calldata_t *params)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (!ctx || ctx->settingVolume || !ctx->source) return;

    float vol = static_cast<float>(calldata_float(params, "volume"));
    float db = (vol <= 0.00001f) ? -INFINITY : obs_mul_to_db(vol);
    if (db > 0.0f) db = 0.0f;
    if (db < -96.0f) db = -INFINITY;
    if (!std::isinf(db)) db = std::round(db * 10.0f) / 10.0f;

    bool isInfPrev = (ctx->volumeDb <= -96.0f || std::isinf(ctx->volumeDb));
    bool isInfCur = (db <= -96.0f || std::isinf(db));
    bool changed = (isInfPrev != isInfCur) || (!isInfCur && std::abs(ctx->volumeDb - db) >= 0.05f);

    if (changed) {
        ctx->volumeDb = db;
        obs_data_t *settings = obs_source_get_settings(ctx->source);
        if (settings) {
            obs_data_set_double(settings, "volume_db", isInfCur ? -96.0 : db);
            obs_data_release(settings);
        }
        obs_source_update_properties(ctx->source);
    }
}

// -------------------------------------------------------------
// Helper to manage companion monitor source
// -------------------------------------------------------------
static std::string get_monitor_source_name(obs_source_t *source)
{
    const char *name = obs_source_get_name(source);
    const char *suffix = obs_module_text("Prop.MixerMonitoring.Suffix");
    if (!suffix || !*suffix) suffix = "Monitoring";
    return std::string(name ? name : "AIMP") + " (" + suffix + ")";
}

static bool s_sceneLoading = true;

void aimp_source_set_scene_loading(bool loading)
{
    s_sceneLoading = loading;
}

static void disconnect_monitor_source_signals(AimpSourceContext *ctx)
{
    if (!ctx || !ctx->monitorSource) return;

    signal_handler_t *sh = obs_source_get_signal_handler(ctx->monitorSource);
    signal_handler_disconnect(sh, "volume", aimp_monitor_volume_changed, ctx);
    signal_handler_disconnect(sh, "mute", aimp_monitor_mute_changed, ctx);
    signal_handler_disconnect(sh, "monitor", aimp_monitor_enabled_changed, ctx);
    signal_handler_disconnect(sh, "destroy", aimp_monitor_source_destroyed, ctx);
}

static void connect_monitor_source_signals(AimpSourceContext *ctx)
{
    if (!ctx || !ctx->monitorSource) return;

    signal_handler_t *sh = obs_source_get_signal_handler(ctx->monitorSource);
    signal_handler_disconnect(sh, "volume", aimp_monitor_volume_changed, ctx);
    signal_handler_disconnect(sh, "mute", aimp_monitor_mute_changed, ctx);
    signal_handler_disconnect(sh, "monitor", aimp_monitor_enabled_changed, ctx);
    signal_handler_disconnect(sh, "destroy", aimp_monitor_source_destroyed, ctx);

    signal_handler_connect(sh, "volume", aimp_monitor_volume_changed, ctx);
    signal_handler_connect(sh, "mute", aimp_monitor_mute_changed, ctx);
    signal_handler_connect(sh, "monitor", aimp_monitor_enabled_changed, ctx);
    signal_handler_connect(sh, "destroy", aimp_monitor_source_destroyed, ctx);
}

static void update_monitor_source(AimpSourceContext *ctx)
{
    if (!ctx || !ctx->source) return;

    // Never enable monitoring on the main source in the mixer
    obs_source_set_monitoring_enabled(ctx->source, false);

    if (ctx->audioEnabled && ctx->audioMonitoring) {
        bool needPublic = ctx->mixerMonitoring;

        // If companion source exists but public/private visibility does not match desired setting, recreate it
        if (ctx->monitorSource && ctx->monitorSourceIsPublic != needPublic) {
            disconnect_monitor_source_signals(ctx);
            if (ctx->audio) {
                ctx->audio->setMonitorSource(nullptr);
            }
            obs_source_remove_active_child(ctx->source, ctx->monitorSource);
            obs_source_remove(ctx->monitorSource);
            obs_source_release(ctx->monitorSource);
            ctx->monitorSource = nullptr;
        }

        if (!ctx->monitorSource) {
            std::string monName = get_monitor_source_name(ctx->source);
            if (needPublic) {
                // Public source: registered with OBS and added to the Audio Mixer
                obs_source_t *existing = obs_get_source_by_name(monName.c_str());
                if (!existing) {
                    existing = obs_get_source_by_name((monName + " 2").c_str());
                }
                if (existing) {
                    ctx->monitorSource = existing;
                } else if (!s_sceneLoading) {
                    ctx->monitorSource = obs_source_create("obs_aimp_monitor_source", monName.c_str(), nullptr, nullptr);
                }
                ctx->monitorSourceIsPublic = true;
            } else {
                // Private source: outputs audio monitoring, but NEVER added to Audio Mixer
                ctx->monitorSource = obs_source_create_private("obs_aimp_monitor_source", monName.c_str(), nullptr);
                ctx->monitorSourceIsPublic = false;
            }
            if (ctx->monitorSource) {
                auto *monCtx = static_cast<AimpMonitorSourceContext *>(obs_obj_get_data(ctx->monitorSource));
                if (monCtx) {
                    monCtx->parentSource = ctx->source;
                }
                connect_monitor_source_signals(ctx);
            }
        }

        if (ctx->monitorSource) {
            ctx->inMuteSync = true;
            obs_source_set_monitoring_enabled(ctx->monitorSource, !ctx->monitorMuted);
            obs_source_set_audio_mixers(ctx->monitorSource, 0);
            float mul = (ctx->monitorVolumeDb <= -96.0f || std::isinf(ctx->monitorVolumeDb)) ? 0.0f : obs_db_to_mul(ctx->monitorVolumeDb);
            ctx->settingVolume = true;
            obs_source_set_volume(ctx->monitorSource, mul);
            ctx->settingVolume = false;
            obs_source_set_muted(ctx->monitorSource, ctx->monitorMuted);
            obs_source_add_active_child(ctx->source, ctx->monitorSource);
            obs_source_set_audio_active(ctx->monitorSource, true);
            ctx->inMuteSync = false;

            if (ctx->audio) {
                ctx->audio->setMonitorSource(ctx->monitorSource);
            }
        }
    } else {
        if (ctx->monitorSource) {
            disconnect_monitor_source_signals(ctx);
            if (ctx->audio) {
                ctx->audio->setMonitorSource(nullptr);
            }
            obs_source_remove_active_child(ctx->source, ctx->monitorSource);
            obs_source_remove(ctx->monitorSource);
            obs_source_release(ctx->monitorSource);
            ctx->monitorSource = nullptr;
            ctx->monitorSourceIsPublic = false;
        }
    }
}

static void aimp_source_renamed(void *data, calldata_t *params)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (!ctx || !ctx->monitorSource) return;

    const char *newName = calldata_string(params, "new_name");
    if (!newName) return;

    const char *suffix = obs_module_text("Prop.MixerMonitoring.Suffix");
    if (!suffix || !*suffix) suffix = "Monitoring";
    std::string newMonName = std::string(newName) + " (" + suffix + ")";
    obs_source_set_name(ctx->monitorSource, newMonName.c_str());
}

// -------------------------------------------------------------
// Unified Source Callbacks
// -------------------------------------------------------------
static const char *aimp_source_get_name(void *)
{
    return obs_module_text("AimpSource");
}

static void *aimp_source_create(obs_data_t *settings, obs_source_t *source)
{
    auto *ctx = new AimpSourceContext();
    ctx->source = source;
    ctx->overlay = std::make_unique<AimpOverlay>(source);
    ctx->audio = std::make_unique<AimpAudioCapture>(source);

    ctx->audioEnabled = obs_data_get_bool(settings, "audio_capture");
    ctx->audioMonitoring = obs_data_get_bool(settings, "audio_monitoring");
    ctx->mixerMonitoring = obs_data_get_bool(settings, "mixer_monitoring");
    ctx->monitorMuted = obs_data_get_bool(settings, "monitor_muted");
    if (obs_data_has_user_value(settings, "monitor_volume_db")) {
        float monDb = static_cast<float>(obs_data_get_double(settings, "monitor_volume_db"));
        if (monDb <= -96.0f) {
            ctx->monitorVolumeDb = -INFINITY;
        } else {
            ctx->monitorVolumeDb = std::round(monDb * 10.0f) / 10.0f;
        }
    } else if (obs_data_has_user_value(settings, "monitor_volume")) {
        int pct = static_cast<int>(obs_data_get_int(settings, "monitor_volume"));
        if (pct <= 0) ctx->monitorVolumeDb = -INFINITY;
        else if (pct >= 100) ctx->monitorVolumeDb = 0.0f;
        else ctx->monitorVolumeDb = std::round(obs_mul_to_db(static_cast<float>(pct) / 100.0f) * 10.0f) / 10.0f;
    } else {
        ctx->monitorVolumeDb = 0.0f;
    }

    if (obs_data_has_user_value(settings, "volume_db")) {
        float db = static_cast<float>(obs_data_get_double(settings, "volume_db"));
        if (db <= -96.0f) {
            ctx->volumeDb = -INFINITY;
        } else {
            ctx->volumeDb = std::round(db * 10.0f) / 10.0f;
        }
        float mul = (ctx->volumeDb <= -96.0f || std::isinf(ctx->volumeDb)) ? 0.0f : obs_db_to_mul(ctx->volumeDb);
        ctx->settingVolume = true;
        obs_source_set_volume(source, mul);
        ctx->settingVolume = false;
    } else {
        float mul = obs_source_get_volume(source);
        float db = (mul <= 0.00001f) ? -INFINITY : obs_mul_to_db(mul);
        if (db > 0.0f) db = 0.0f;
        if (db < -96.0f) db = -INFINITY;
        ctx->volumeDb = std::isinf(db) ? -INFINITY : (std::round(db * 10.0f) / 10.0f);
    }

    ctx->overlay->updateSettings(settings);

    obs_source_set_audio_active(source, ctx->audioEnabled);
    update_monitor_source(ctx);

    AimpController::instance().notifyAudioCapture(ctx->audioEnabled);
    AimpController::instance().notifyMonitoringSettings(ctx->audioMonitoring, ctx->mixerMonitoring, ctx->monitorVolumeDb);
    AimpController::instance().notifyMonitorMute(ctx->monitorMuted);

    if (ctx->audioEnabled) {
        ctx->audio->start();
    }

    signal_handler_connect(obs_source_get_signal_handler(source), "rename", aimp_source_renamed, ctx);
    signal_handler_connect(obs_source_get_signal_handler(source), "volume", aimp_source_volume_changed, ctx);

    return ctx;
}

static void aimp_source_destroy(void *data)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (ctx) {
        signal_handler_disconnect(obs_source_get_signal_handler(ctx->source), "rename", aimp_source_renamed, ctx);
        signal_handler_disconnect(obs_source_get_signal_handler(ctx->source), "volume", aimp_source_volume_changed, ctx);
        if (ctx->monitorSource) {
            disconnect_monitor_source_signals(ctx);
            if (ctx->audio) {
                ctx->audio->setMonitorSource(nullptr);
            }
            obs_source_remove_active_child(ctx->source, ctx->monitorSource);
            obs_source_remove(ctx->monitorSource);
            obs_source_release(ctx->monitorSource);
            ctx->monitorSource = nullptr;
        }
        if (ctx->audio) {
            ctx->audio->stop();
        }
        delete ctx;
    }
}

static void aimp_source_update(void *data, obs_data_t *settings)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (!ctx) return;

    ctx->overlay->updateSettings(settings);

    bool shouldAudio = obs_data_get_bool(settings, "audio_capture");
    if (shouldAudio != ctx->audioEnabled) {
        ctx->audioEnabled = shouldAudio;
        obs_source_set_audio_active(ctx->source, ctx->audioEnabled);
        if (ctx->audioEnabled) {
            ctx->audio->start();
        } else {
            ctx->audio->stop();
        }
    }

    ctx->audioMonitoring = obs_data_get_bool(settings, "audio_monitoring");
    ctx->mixerMonitoring = obs_data_get_bool(settings, "mixer_monitoring");
    if (obs_data_has_user_value(settings, "monitor_volume_db")) {
        float monDb = static_cast<float>(obs_data_get_double(settings, "monitor_volume_db"));
        if (monDb <= -96.0f) {
            ctx->monitorVolumeDb = -INFINITY;
        } else {
            ctx->monitorVolumeDb = std::round(monDb * 10.0f) / 10.0f;
        }
    } else if (obs_data_has_user_value(settings, "monitor_volume")) {
        int pct = static_cast<int>(obs_data_get_int(settings, "monitor_volume"));
        if (pct <= 0) ctx->monitorVolumeDb = -INFINITY;
        else if (pct >= 100) ctx->monitorVolumeDb = 0.0f;
        else ctx->monitorVolumeDb = std::round(obs_mul_to_db(static_cast<float>(pct) / 100.0f) * 10.0f) / 10.0f;
    }

    if (obs_data_has_user_value(settings, "volume_db")) {
        float db = static_cast<float>(obs_data_get_double(settings, "volume_db"));
        if (db <= -96.0f) {
            db = -INFINITY;
        } else {
            db = std::round(db * 10.0f) / 10.0f;
        }
        bool isInfPrev = (ctx->volumeDb <= -96.0f || std::isinf(ctx->volumeDb));
        bool isInfCur = (db <= -96.0f || std::isinf(db));
        bool changed = (isInfPrev != isInfCur) || (!isInfCur && std::abs(ctx->volumeDb - db) >= 0.05f);
        if (changed) {
            ctx->volumeDb = db;
            float mul = isInfCur ? 0.0f : obs_db_to_mul(db);
            ctx->settingVolume = true;
            obs_source_set_volume(ctx->source, mul);
            ctx->settingVolume = false;
        }
    }

    if (obs_data_has_user_value(settings, "monitor_muted")) {
        bool m = obs_data_get_bool(settings, "monitor_muted");
        if (ctx->monitorMuted != m) {
            ctx->inMuteSync = true;
            ctx->monitorMuted = m;
            if (ctx->monitorSource) {
                obs_source_set_muted(ctx->monitorSource, m);
                obs_source_set_monitoring_enabled(ctx->monitorSource, !m);
            }
            ctx->inMuteSync = false;
        }
    }

    update_monitor_source(ctx);

    AimpController::instance().notifyAudioCapture(ctx->audioEnabled);
    AimpController::instance().notifyMonitoringSettings(ctx->audioMonitoring, ctx->mixerMonitoring, ctx->monitorVolumeDb);
    AimpController::instance().notifyMonitorMute(ctx->monitorMuted);
}

static uint32_t aimp_source_get_width(void *data)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    return ctx ? ctx->overlay->getWidth() : 520;
}

static uint32_t aimp_source_get_height(void *data)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    return ctx ? ctx->overlay->getHeight() : 130;
}

static void aimp_source_video_tick(void *data, float seconds)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (ctx) {
        ctx->overlay->videoTick(seconds);
    }
}

static void aimp_source_video_render(void *data, gs_effect_t *)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    if (ctx) {
        ctx->overlay->videoRender();
    }
}

static bool override_colors_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    bool override = obs_data_get_bool(settings, "override_colors");
    obs_property_set_visible(obs_properties_get(props, "color_background"), override);
    obs_property_set_visible(obs_properties_get(props, "color_title"), override);
    obs_property_set_visible(obs_properties_get(props, "color_artist"), override);
    obs_property_set_visible(obs_properties_get(props, "color_time"), override);
    obs_property_set_visible(obs_properties_get(props, "color_progress_fill"), override);
    obs_property_set_visible(obs_properties_get(props, "color_progress_bg"), override);
    return true;
}

static bool override_fonts_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    bool override = obs_data_get_bool(settings, "override_fonts");
    obs_property_set_visible(obs_properties_get(props, "font_title"), override);
    obs_property_set_visible(obs_properties_get(props, "font_artist"), override);
    return true;
}

static obs_property_t *find_prop(obs_properties_t *props, const char *name)
{
    if (!props) return nullptr;
    obs_property_t *p = obs_properties_get(props, name);
    if (p) return p;
    obs_property_t *grp = obs_properties_get(props, "group_audio");
    if (grp) {
        obs_properties_t *content = obs_property_group_content(grp);
        if (content) {
            p = obs_properties_get(content, name);
            if (p) return p;
        }
    }
    return nullptr;
}

static bool audio_capture_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    bool audio = obs_data_get_bool(settings, "audio_capture");
    bool audioMon = obs_data_get_bool(settings, "audio_monitoring");

    obs_property_t *propVol = find_prop(props, "volume_db");
    obs_property_t *propMon = find_prop(props, "audio_monitoring");
    obs_property_t *propMixer = find_prop(props, "mixer_monitoring");
    obs_property_t *propMonVol = find_prop(props, "monitor_volume_db");

    if (propVol) obs_property_set_enabled(propVol, audio);
    if (propMon) obs_property_set_enabled(propMon, audio);
    if (propMixer) obs_property_set_enabled(propMixer, audio && audioMon);
    if (propMonVol) obs_property_set_enabled(propMonVol, audio && audioMon);
    return true;
}

static bool audio_monitoring_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    bool audio = obs_data_get_bool(settings, "audio_capture");
    bool audioMon = obs_data_get_bool(settings, "audio_monitoring");

    obs_property_t *propMixer = find_prop(props, "mixer_monitoring");
    obs_property_t *propMonVol = find_prop(props, "monitor_volume_db");

    if (propMixer) obs_property_set_enabled(propMixer, audio && audioMon);
    if (propMonVol) obs_property_set_enabled(propMonVol, audio && audioMon);
    return true;
}

static bool marquee_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    bool marquee = obs_data_get_bool(settings, "marquee");
    obs_property_set_enabled(obs_properties_get(props, "scroll_speed"), marquee);
    return true;
}

static obs_properties_t *aimp_source_get_properties(void *data)
{
    auto *ctx = static_cast<AimpSourceContext *>(data);
    obs_properties_t *props = obs_properties_create();

    if (ctx) {
        if (ctx->source) {
            float mul = obs_source_get_volume(ctx->source);
            float db = (mul <= 0.00001f) ? -INFINITY : obs_mul_to_db(mul);
            if (db > 0.0f) db = 0.0f;
            if (db < -96.0f) db = -INFINITY;
            ctx->volumeDb = std::isinf(db) ? -INFINITY : (std::round(db * 10.0f) / 10.0f);
        }
        if (ctx->monitorSource) {
            float mul = obs_source_get_volume(ctx->monitorSource);
            float db = (mul <= 0.00001f) ? -INFINITY : obs_mul_to_db(mul);
            if (db > 0.0f) db = 0.0f;
            if (db < -96.0f) db = -INFINITY;
            ctx->monitorVolumeDb = std::isinf(db) ? -INFINITY : (std::round(db * 10.0f) / 10.0f);
        }
        if (ctx->source) {
            obs_data_t *settings = obs_source_get_settings(ctx->source);
            if (settings) {
                obs_data_set_double(settings, "volume_db", (ctx->volumeDb <= -96.0f || std::isinf(ctx->volumeDb)) ? -96.0 : ctx->volumeDb);
                obs_data_set_double(settings, "monitor_volume_db", (ctx->monitorVolumeDb <= -96.0f || std::isinf(ctx->monitorVolumeDb)) ? -96.0 : ctx->monitorVolumeDb);
                obs_data_release(settings);
            }
        }
    }

    // Scan available themes
    char *themesDir = obs_module_file("themes");
    if (themesDir) {
        AimpThemeManager::instance().scanThemes(QString::fromUtf8(themesDir));
        bfree(themesDir);
    }

    // ==========================================
    // 1. Отображение (Display / Overlay)
    // ==========================================
    obs_properties_t *groupDisplay = obs_properties_create();

    obs_property_t *themeProp = obs_properties_add_list(
        groupDisplay, "theme", obs_module_text("Prop.Theme"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    std::vector<AimpTheme> themes = AimpThemeManager::instance().getAvailableThemes();
    for (const auto &theme : themes) {
        QString displayName = theme.getDisplayName();
        obs_property_list_add_string(themeProp, displayName.toUtf8().constData(), theme.id.toUtf8().constData());
    }

    obs_property_t *propAutoHide = obs_properties_add_bool(groupDisplay, "auto_hide", obs_module_text("Prop.AutoHide"));
    obs_property_set_long_description(propAutoHide, obs_module_text("Prop.AutoHide.Desc"));

    obs_property_t *propMarquee = obs_properties_add_bool(groupDisplay, "marquee", obs_module_text("Prop.Marquee"));
    obs_property_set_long_description(propMarquee, obs_module_text("Prop.Marquee.Desc"));
    obs_property_set_modified_callback(propMarquee, marquee_changed);

    obs_properties_add_int_slider(groupDisplay, "scroll_speed", obs_module_text("Prop.ScrollSpeed"), 10, 150, 5);

    obs_properties_add_group(props, "group_display", obs_module_text("Group.Display"), OBS_GROUP_NORMAL, groupDisplay);

    // ==========================================
    // 2. Звук (Audio capture & monitoring)
    // ==========================================
    obs_properties_t *groupAudio = obs_properties_create();

    obs_property_t *propAudioCapture = obs_properties_add_bool(groupAudio, "audio_capture", obs_module_text("Prop.AudioCapture"));
    obs_property_set_long_description(propAudioCapture, obs_module_text("Prop.AudioCapture.Desc"));
    obs_property_set_modified_callback(propAudioCapture, audio_capture_changed);

    obs_property_t *propVolume = obs_properties_add_float_slider(
        groupAudio, "volume_db", obs_module_text("Prop.Volume"), -96.0, 0.0, 0.1);
    obs_property_float_set_suffix(propVolume, " dB");
    obs_property_set_long_description(propVolume, obs_module_text("Prop.Volume.Desc"));

    obs_property_t *propAudioMonitoring = obs_properties_add_bool(groupAudio, "audio_monitoring", obs_module_text("Prop.AudioMonitoring"));
    obs_property_set_long_description(propAudioMonitoring, obs_module_text("Prop.AudioMonitoring.Desc"));
    obs_property_set_modified_callback(propAudioMonitoring, audio_monitoring_changed);

    obs_property_t *propMixerMonitoring = obs_properties_add_bool(groupAudio, "mixer_monitoring", obs_module_text("Prop.MixerMonitoring"));
    obs_property_set_long_description(propMixerMonitoring, obs_module_text("Prop.MixerMonitoring.Desc"));

    obs_property_t *propMonitorVolume = obs_properties_add_float_slider(
        groupAudio, "monitor_volume_db", obs_module_text("Prop.MonitorVolume"), -96.0, 0.0, 0.1);
    obs_property_float_set_suffix(propMonitorVolume, " dB");
    obs_property_set_long_description(propMonitorVolume, obs_module_text("Prop.MonitorVolume.Desc"));

    bool audio = ctx ? ctx->audioEnabled : true;
    bool mon = ctx ? ctx->audioMonitoring : false;
    if (ctx && ctx->source) {
        obs_data_t *settings = obs_source_get_settings(ctx->source);
        if (settings) {
            audio = obs_data_get_bool(settings, "audio_capture");
            mon = obs_data_get_bool(settings, "audio_monitoring");
            obs_data_release(settings);
        }
    }

    obs_property_set_enabled(propVolume, audio);
    obs_property_set_enabled(propAudioMonitoring, audio);
    obs_property_set_enabled(propMixerMonitoring, audio && mon);
    obs_property_set_enabled(propMonitorVolume, audio && mon);

    obs_properties_add_group(props, "group_audio", obs_module_text("Group.Audio"), OBS_GROUP_NORMAL, groupAudio);

    // ==========================================
    // 3. Цвета (Color overrides)
    // ==========================================
    obs_properties_t *groupColors = obs_properties_create();

    obs_property_t *propColorToggle = obs_properties_add_bool(groupColors, "override_colors", obs_module_text("Prop.CustomColors"));
    obs_property_set_modified_callback(propColorToggle, override_colors_changed);

    obs_properties_add_color(groupColors, "color_background", obs_module_text("Prop.Color.Background"));
    obs_properties_add_color(groupColors, "color_title", obs_module_text("Prop.Color.Title"));
    obs_properties_add_color(groupColors, "color_artist", obs_module_text("Prop.Color.Artist"));
    obs_properties_add_color(groupColors, "color_time", obs_module_text("Prop.Color.Time"));
    obs_properties_add_color(groupColors, "color_progress_fill", obs_module_text("Prop.Color.ProgressFill"));
    obs_properties_add_color(groupColors, "color_progress_bg", obs_module_text("Prop.Color.ProgressBg"));

    obs_properties_add_group(props, "group_colors", obs_module_text("Group.Colors"), OBS_GROUP_NORMAL, groupColors);

    // ==========================================
    // 4. Шрифты (Font overrides)
    // ==========================================
    obs_properties_t *groupFonts = obs_properties_create();

    obs_property_t *propFontToggle = obs_properties_add_bool(groupFonts, "override_fonts", obs_module_text("Prop.CustomFonts"));
    obs_property_set_modified_callback(propFontToggle, override_fonts_changed);

    obs_properties_add_font(groupFonts, "font_title", obs_module_text("Prop.Font.Title"));
    obs_properties_add_font(groupFonts, "font_artist", obs_module_text("Prop.Font.Artist"));

    obs_properties_add_group(props, "group_fonts", obs_module_text("Group.Fonts"), OBS_GROUP_NORMAL, groupFonts);

    return props;
}

static void aimp_source_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "theme", "default");
    obs_data_set_default_bool(settings, "audio_capture", true);
    obs_data_set_default_double(settings, "volume_db", 0.0);
    obs_data_set_default_bool(settings, "audio_monitoring", false);
    obs_data_set_default_bool(settings, "mixer_monitoring", false);
    obs_data_set_default_double(settings, "monitor_volume_db", 0.0);
    obs_data_set_default_bool(settings, "monitor_muted", false);
    obs_data_set_default_bool(settings, "auto_hide", true);
    obs_data_set_default_bool(settings, "marquee", true);
    obs_data_set_default_int(settings, "scroll_speed", 40);

    obs_data_set_default_bool(settings, "override_colors", false);
    obs_data_set_default_int(settings, "color_background", 0xE61A1B24);
    obs_data_set_default_int(settings, "color_title", 0xFFFFFFFF);
    obs_data_set_default_int(settings, "color_artist", 0xFFA2A7BD);
    obs_data_set_default_int(settings, "color_time", 0xFF8E93AB);
    obs_data_set_default_int(settings, "color_progress_fill", 0xFFFF7A00);
    obs_data_set_default_int(settings, "color_progress_bg", 0x80282B3C);

    obs_data_set_default_bool(settings, "override_fonts", false);
}

static const char *aimp_source_get_dark_icon(void *)
{
    char *path = obs_module_file("source_dark.svg");
    if (!path) {
        path = obs_module_file("source_dark.png");
    }
    return path;
}

static const char *aimp_source_get_light_icon(void *)
{
    char *path = obs_module_file("source_light.svg");
    if (!path) {
        path = obs_module_file("source_light.png");
    }
    return path;
}

static const char *aimp_monitor_source_get_dark_icon(void *)
{
    char *path = obs_module_file("monitor_dark.svg");
    if (!path) {
        path = obs_module_file("monitor_dark.png");
    }
    return path;
}

static const char *aimp_monitor_source_get_light_icon(void *)
{
    char *path = obs_module_file("monitor_light.svg");
    if (!path) {
        path = obs_module_file("monitor_light.png");
    }
    return path;
}

void register_aimp_sources()
{
    struct obs_source_info aimp_src_info = {};
    aimp_src_info.id = "obs_aimp_source";
    aimp_src_info.type = OBS_SOURCE_TYPE_INPUT;
    aimp_src_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE;
    aimp_src_info.icon_type = OBS_ICON_TYPE_CUSTOM;
    aimp_src_info.get_dark_icon = aimp_source_get_dark_icon;
    aimp_src_info.get_light_icon = aimp_source_get_light_icon;
    aimp_src_info.get_name = aimp_source_get_name;
    aimp_src_info.create = aimp_source_create;
    aimp_src_info.destroy = aimp_source_destroy;
    aimp_src_info.update = aimp_source_update;
    aimp_src_info.get_width = aimp_source_get_width;
    aimp_src_info.get_height = aimp_source_get_height;
    aimp_src_info.video_tick = aimp_source_video_tick;
    aimp_src_info.video_render = aimp_source_video_render;
    aimp_src_info.get_properties = aimp_source_get_properties;
    aimp_src_info.get_defaults = aimp_source_get_defaults;
    aimp_src_info.load = [](void *data, obs_data_t *) {
        auto *ctx = static_cast<AimpSourceContext *>(data);
        if (ctx) {
            update_monitor_source(ctx);
        }
    };
    aimp_src_info.enum_active_sources = [](void *data, obs_source_enum_proc_t enum_callback, void *param) {
        auto *ctx = static_cast<AimpSourceContext *>(data);
        if (ctx && ctx->monitorSource) {
            enum_callback(ctx->source, ctx->monitorSource, param);
        }
    };

    obs_register_source(&aimp_src_info);

    struct obs_source_info mon_info = {};
    mon_info.id = "obs_aimp_monitor_source";
    mon_info.type = OBS_SOURCE_TYPE_INPUT;
    mon_info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_MONITOR_BY_DEFAULT;
    mon_info.icon_type = OBS_ICON_TYPE_CUSTOM;
    mon_info.get_dark_icon = aimp_monitor_source_get_dark_icon;
    mon_info.get_light_icon = aimp_monitor_source_get_light_icon;
    mon_info.get_name = aimp_monitor_source_get_name;
    mon_info.create = aimp_monitor_source_create;
    mon_info.destroy = aimp_monitor_source_destroy;

    obs_register_source(&mon_info);
}

obs_source_t *aimp_get_main_source_for_monitor(obs_source_t *monitorSource)
{
    if (!monitorSource) return nullptr;

    const char *monId = obs_source_get_id(monitorSource);
    if (!monId || strcmp(monId, "obs_aimp_monitor_source") != 0) return nullptr;

    auto *monCtx = static_cast<AimpMonitorSourceContext *>(obs_obj_get_data(monitorSource));
    if (monCtx && monCtx->parentSource) {
        return monCtx->parentSource;
    }

    struct SearchData {
        obs_source_t *targetMonitor;
        obs_source_t *matched = nullptr;
        obs_source_t *firstMain = nullptr;
    } sd = {monitorSource, nullptr, nullptr};

    obs_enum_sources([](void *param, obs_source_t *source) -> bool {
        auto *data = static_cast<SearchData *>(param);
        const char *id = obs_source_get_id(source);
        if (id && strcmp(id, "obs_aimp_source") == 0) {
            if (!data->firstMain) {
                data->firstMain = source;
            }
            auto *ctx = static_cast<AimpSourceContext *>(obs_obj_get_data(source));
            if (ctx && ctx->monitorSource == data->targetMonitor) {
                data->matched = source;
                return false;
            }
        }
        return true;
    }, &sd);

    return sd.matched ? sd.matched : sd.firstMain;
}

void aimp_source_sync_all_monitors()
{
    s_sceneLoading = false;
    obs_enum_sources([](void *, obs_source_t *source) -> bool {
        const char *id = obs_source_get_id(source);
        if (id && strcmp(id, "obs_aimp_source") == 0) {
            auto *ctx = static_cast<AimpSourceContext *>(obs_obj_get_data(source));
            if (ctx) {
                update_monitor_source(ctx);
                AimpController::instance().notifyAudioCapture(ctx->audioEnabled);
                AimpController::instance().notifyMonitoringSettings(ctx->audioMonitoring, ctx->mixerMonitoring, ctx->monitorVolumeDb);
                AimpController::instance().notifyMonitorMute(ctx->monitorMuted);
                if (ctx->audio && ctx->audioEnabled) {
                    ctx->audio->start();
                }
            }
        }
        return true;
    }, nullptr);
}

bool aimp_source_get_monitor_muted(obs_source_t *mainSource)
{
    if (!mainSource) return false;
    auto *ctx = static_cast<AimpSourceContext *>(obs_obj_get_data(mainSource));
    if (!ctx) return false;
    if (ctx->monitorSource) {
        return obs_source_muted(ctx->monitorSource) || !obs_source_get_monitoring_enabled(ctx->monitorSource);
    }
    return ctx->monitorMuted;
}

void aimp_source_set_monitor_muted(obs_source_t *mainSource, bool muted)
{
    if (!mainSource) return;
    auto *ctx = static_cast<AimpSourceContext *>(obs_obj_get_data(mainSource));
    if (!ctx || ctx->inMuteSync) return;
    ctx->inMuteSync = true;
    ctx->monitorMuted = muted;
    if (ctx->monitorSource) {
        obs_source_set_muted(ctx->monitorSource, muted);
        obs_source_set_monitoring_enabled(ctx->monitorSource, !muted);
    }
    obs_data_t *settings = obs_source_get_settings(mainSource);
    if (settings) {
        obs_data_set_bool(settings, "monitor_muted", muted);
        obs_data_release(settings);
    }
    AimpController::instance().notifyMonitorMute(muted);
    ctx->inMuteSync = false;
}
