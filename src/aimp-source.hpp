#pragma once

#include <obs-module.h>

void register_aimp_sources();
obs_source_t *aimp_get_main_source_for_monitor(obs_source_t *monitorSource);
void aimp_source_set_scene_loading(bool loading);
void aimp_source_sync_all_monitors();
bool aimp_source_get_monitor_muted(obs_source_t *mainSource);
void aimp_source_set_monitor_muted(obs_source_t *mainSource, bool muted);
