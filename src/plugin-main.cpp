#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSlider>
#include <QTimer>
#include <functional>

#include "aimp-dock.hpp"
#include "aimp-source.hpp"
#include "plugin-support.h"

Q_DECLARE_METATYPE(OBSSource);

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

class MixerMenuFilter : public QObject {
public:
    explicit MixerMenuFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    static void adjustDoubleSpinBoxDecimals(QDoubleSpinBox *spin)
    {
        if (!spin) return;
        if (spin->decimals() != 1 && spin->suffix().trimmed().compare("dB", Qt::CaseInsensitive) == 0) {
            if (std::abs(spin->singleStep() - 0.1) < 0.01) {
                spin->setDecimals(1);
            }
        }
    }

    static void syncSlidersInWidget(QWidget *container)
    {
        if (!container) return;

        const auto sliders = container->findChildren<QSlider *>();
        for (QSlider *slider : sliders) {
            if (!slider) continue;
            QWidget *parent = slider->parentWidget();
            if (!parent || !parent->layout()) continue;

            std::function<void(QLayout *)> inspectLayout = [&](QLayout *layout) {
                if (!layout) return;
                for (int i = 0; i < layout->count(); ++i) {
                    QLayoutItem *item = layout->itemAt(i);
                    if (!item) continue;
                    if (QLayout *subLayout = item->layout()) {
                        bool containsSlider = false;
                        QDoubleSpinBox *pairedSpin = nullptr;
                        for (int j = 0; j < subLayout->count(); ++j) {
                            QLayoutItem *subItem = subLayout->itemAt(j);
                            if (!subItem) continue;
                            if (subItem->widget() == slider) {
                                containsSlider = true;
                            } else if (auto *sp = qobject_cast<QDoubleSpinBox *>(subItem->widget())) {
                                pairedSpin = sp;
                            }
                        }
                        if (containsSlider && pairedSpin) {
                            if (slider->isEnabled() != pairedSpin->isEnabled()) {
                                slider->setEnabled(pairedSpin->isEnabled());
                                slider->update();
                            }
                            return;
                        }
                        inspectLayout(subLayout);
                    }
                }
            };

            inspectLayout(parent->layout());
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!watched || !event) return false;

        // Keep float sliders in OBS properties view in sync with their paired spinboxes
        if (event->type() == QEvent::EnabledChange) {
            if (auto *spin = qobject_cast<QDoubleSpinBox *>(watched)) {
                QWidget *w = spin->parentWidget();
                while (w) {
                    if (w->objectName() == "PropertiesContainer") {
                        syncSlidersInWidget(w);
                        break;
                    }
                    w = w->parentWidget();
                }
            }
        }

        if (watched->objectName() == "PropertiesContainer") {
            if (event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize) {
                auto *container = static_cast<QWidget *>(watched);
                syncSlidersInWidget(container);
                QPointer<QWidget> pContainer(container);
                QTimer::singleShot(0, [pContainer]() {
                    if (pContainer) {
                        syncSlidersInWidget(pContainer.data());
                    }
                });
            }
        }

        if (event->type() == QEvent::Show) {
            if (auto *slider = qobject_cast<QSlider *>(watched)) {
                QWidget *w = slider->parentWidget();
                while (w) {
                    if (w->objectName() == "PropertiesContainer") {
                        syncSlidersInWidget(w);
                        break;
                    }
                    w = w->parentWidget();
                }
            }
        }

        if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::KeyRelease) {
            if (auto *cb = qobject_cast<QCheckBox *>(watched)) {
                QWidget *w = cb->parentWidget();
                while (w) {
                    if (w->objectName() == "PropertiesContainer") {
                        QPointer<QWidget> pContainer(w);
                        QTimer::singleShot(50, [pContainer]() {
                            if (pContainer) {
                                syncSlidersInWidget(pContainer.data());
                            }
                        });
                        break;
                    }
                    w = w->parentWidget();
                }
            }
        }

        // Decimal adjustment for 0.1 dB volume spinboxes in properties view
        if (event->type() == QEvent::Show ||
            event->type() == QEvent::Polish ||
            event->type() == QEvent::ShowToParent ||
            event->type() == QEvent::Paint) {
            if (auto *spin = qobject_cast<QDoubleSpinBox *>(watched)) {
                adjustDoubleSpinBoxDecimals(spin);
            } else if (event->type() == QEvent::Show) {
                if (auto *widget = qobject_cast<QWidget *>(watched)) {
                    const auto spinBoxes = widget->findChildren<QDoubleSpinBox *>();
                    for (auto *childSpin : spinBoxes) {
                        adjustDoubleSpinBoxDecimals(childSpin);
                    }
                }
            }
        }

        if (event->type() == QEvent::Show) {
            if (auto *menu = qobject_cast<QMenu *>(watched)) {
                const auto actions = menu->actions();
                obs_source_t *monSource = nullptr;
                for (QAction *action : actions) {
                    if (!action) continue;
                    QVariant var = action->property("source");
                    if (var.isValid() && var.canConvert<OBSSource>()) {
                        OBSSource obsSrc = var.value<OBSSource>();
                        obs_source_t *src = obsSrc.Get();
                        if (src) {
                            const char *id = obs_source_get_id(src);
                            if (id && strcmp(id, "obs_aimp_monitor_source") == 0) {
                                monSource = src;
                                break;
                            }
                        }
                    }
                }

                if (monSource) {
                    obs_source_t *parent = aimp_get_main_source_for_monitor(monSource);
                    if (parent) {
                        for (QAction *action : actions) {
                            if (!action) continue;
                            QVariant var = action->property("source");
                            if (var.isValid() && var.canConvert<OBSSource>()) {
                                action->setProperty("source", QVariant::fromValue<OBSSource>(parent));
                            }
                        }

                        QAction *propAction = nullptr;
                        QAction *filtersAction = nullptr;
                        for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
                            if (!*it || (*it)->isSeparator()) continue;
                            if (!propAction) {
                                propAction = *it;
                            } else if (!filtersAction) {
                                filtersAction = *it;
                                break;
                            }
                        }

                        obs_weak_source_t *weakParent = obs_source_get_weak_source(parent);

                        if (propAction) {
                            propAction->disconnect();
                            QObject::connect(propAction, &QAction::triggered, [weakParent]() {
                                obs_source_t *src = obs_weak_source_get_source(weakParent);
                                if (src) {
                                    obs_frontend_open_source_properties(src);
                                    obs_source_release(src);
                                }
                            });
                        }

                        if (filtersAction) {
                            filtersAction->disconnect();
                            QObject::connect(filtersAction, &QAction::triggered, [weakParent]() {
                                obs_source_t *src = obs_weak_source_get_source(weakParent);
                                if (src) {
                                    obs_frontend_open_source_filters(src);
                                    obs_source_release(src);
                                }
                            });
                        }

                        obs_weak_source_release(weakParent);
                    }
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

static AimpControlDock *s_controlDock = nullptr;
static MixerMenuFilter *s_mixerMenuFilter = nullptr;
static QAction *s_dockToggleAction = nullptr;
static QMetaObject::Connection s_menuDocksConn;

static void ensure_dock_menu_action()
{
    if (!s_controlDock) return;
    auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!mainWindow) return;

    QMenu *menuDocks = mainWindow->findChild<QMenu *>("menuDocks");
    if (!menuDocks) {
        QMenuBar *menuBar = mainWindow->menuBar();
        if (menuBar) {
            for (QAction *action : menuBar->actions()) {
                if (action && action->menu()) {
                    if (action->menu()->objectName() == "menuDocks") {
                        menuDocks = action->menu();
                        break;
                    }
                }
            }
        }
    }

    if (menuDocks) {
        if (!s_dockToggleAction) {
            s_dockToggleAction = s_controlDock->toggleViewAction();
            s_dockToggleAction->setText(QString::fromUtf8(obs_module_text("DockTitle")));
            char *dockIconPath = obs_module_file("aimp_icon.png");
            if (dockIconPath) {
                s_dockToggleAction->setIcon(QIcon(QString::fromUtf8(dockIconPath)));
                bfree(dockIconPath);
            }
            QObject::disconnect(s_menuDocksConn);
            s_menuDocksConn = QObject::connect(menuDocks, &QMenu::aboutToShow, []() {
                ensure_dock_menu_action();
            });
        }
        if (!menuDocks->actions().contains(s_dockToggleAction)) {
            menuDocks->addAction(s_dockToggleAction);
        }
    }
}

static void frontend_event_callback(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
        event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
        aimp_source_sync_all_monitors();
        AimpController::instance().pollTimerTick();
        ensure_dock_menu_action();
        if (s_controlDock) {
            s_controlDock->refreshSourceAttachment();
        }
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP ||
               event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
        aimp_source_set_scene_loading(true);
    }
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-aimp-source] Loading plugin version %s", PLUGIN_VERSION);

    // Register OBS Sources
    register_aimp_sources();

    // Register Qt Control Dock Widget
    auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (mainWindow) {
        s_controlDock = new AimpControlDock(mainWindow);
        obs_frontend_add_custom_qdock("obs_aimp_control_dock", s_controlDock);
        ensure_dock_menu_action();
        blog(LOG_INFO, "[obs-aimp-source] Registered AIMP control dock");
    }

    // Install mixer menu filter for monitor source properties redirection
    if (qApp) {
        s_mixerMenuFilter = new MixerMenuFilter();
        qApp->installEventFilter(s_mixerMenuFilter);
        blog(LOG_INFO, "[obs-aimp-source] Installed mixer menu filter");
    }

    obs_frontend_add_event_callback(frontend_event_callback, nullptr);

    blog(LOG_INFO, "[obs-aimp-source] Plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-aimp-source] Unloading plugin");

    obs_frontend_remove_event_callback(frontend_event_callback, nullptr);

    QObject::disconnect(s_menuDocksConn);

    if (s_dockToggleAction) {
        auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
        if (mainWindow) {
            QMenu *menuDocks = mainWindow->findChild<QMenu *>("menuDocks");
            if (menuDocks) {
                menuDocks->removeAction(s_dockToggleAction);
            }
        }
        s_dockToggleAction = nullptr;
    }

    if (s_mixerMenuFilter) {
        if (qApp) {
            qApp->removeEventFilter(s_mixerMenuFilter);
        }
        delete s_mixerMenuFilter;
        s_mixerMenuFilter = nullptr;
    }

    if (s_controlDock) {
        obs_frontend_remove_dock("obs_aimp_control_dock");
        s_controlDock = nullptr;
    }

    blog(LOG_INFO, "[obs-aimp-source] Plugin unloaded");
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("Plugin.Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return obs_module_text("Plugin.Name");
}
