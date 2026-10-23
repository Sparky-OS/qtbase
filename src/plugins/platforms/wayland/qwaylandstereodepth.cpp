// Copyright (C) 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qwaylandstereodepth_p.h"

#include "qwaylanddisplay_p.h"
#include "qwaylandwindow_p.h"

#include <QtWaylandClient/private/qwayland-kde-stereo-content-v1.h>

#include <QtCore/qmutex.h>

#include <memory>
#include <unordered_map>

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

namespace {

class ManagerProxy : public QtWayland::kde_stereo_content_manager_v1
{
public:
    using kde_stereo_content_manager_v1::kde_stereo_content_manager_v1;
    ~ManagerProxy() override { destroy(); }
};

struct Manager
{
    uint32_t id = 0;
    std::unique_ptr<QtWayland::kde_stereo_content_manager_v1> manager;
};

class Content : public QtWayland::kde_stereo_content_v1
{
public:
    using kde_stereo_content_v1::kde_stereo_content_v1;
    ~Content() override { destroy(); }
};

class Depth : public QtWayland::kde_stereo_depth_v1
{
public:
    using kde_stereo_depth_v1::kde_stereo_depth_v1;
    ~Depth() override { destroy(); }
};

struct WindowState
{
    ::wl_surface *surface = nullptr;
    std::unique_ptr<Content> content;
    std::unique_ptr<Depth> depth;
    bool pair = false;
    bool desktopDepth = false;
};

// A window's render thread marks its surface while the gui thread may declare others.
QBasicMutex s_mutex;

std::unordered_map<QWaylandDisplay *, Manager> &managers()
{
    static std::unordered_map<QWaylandDisplay *, Manager> s_managers;
    return s_managers;
}

std::unordered_map<QWaylandWindow *, WindowState> &windows()
{
    static std::unordered_map<QWaylandWindow *, WindowState> s_windows;
    return s_windows;
}

void forgetWindowsOf(QWaylandDisplay *display)
{
    for (auto window = windows().begin(); window != windows().end();) {
        if (window->first->display() == display)
            window = windows().erase(window);
        else
            ++window;
    }
}

} // namespace

void QWaylandStereoDepth::addGlobal(QWaylandDisplay *display, ::wl_registry *registry, uint32_t id, uint32_t version)
{
    QMutexLocker locker(&s_mutex);
    // Version 3 marks a pair as the desktop's own depth; without it a window is never drawn per eye.
    if (version < 3)
        return;
    Manager &entry = managers()[display];
    entry.id = id;
    entry.manager = std::make_unique<ManagerProxy>(registry, id, 3);
}

void QWaylandStereoDepth::removeGlobal(QWaylandDisplay *display, uint32_t id)
{
    QMutexLocker locker(&s_mutex);
    auto it = managers().find(display);
    if (it == managers().end() || it->second.id != id)
        return;
    forgetWindowsOf(display);
    managers().erase(it);
}

void QWaylandStereoDepth::removeDisplay(QWaylandDisplay *display)
{
    QMutexLocker locker(&s_mutex);
    forgetWindowsOf(display);
    managers().erase(display);
}

static bool isSupportedLocked(QWaylandDisplay *display)
{
    auto it = managers().find(display);
    return it != managers().end() && it->second.manager;
}

bool QWaylandStereoDepth::isSupported(QWaylandDisplay *display)
{
    QMutexLocker locker(&s_mutex);
    return isSupportedLocked(display);
}

void QWaylandStereoDepth::setPair(QWaylandWindow *window, bool pair)
{
    QMutexLocker locker(&s_mutex);
    ::wl_surface *surface = window->wlSurface();
    auto it = windows().find(window);
    if (it != windows().end() && it->second.surface != surface) {
        // A new surface: the old one's objects went with it.
        windows().erase(it);
        it = windows().end();
    }
    if (!pair && it == windows().end())
        return;
    if (!surface || !isSupportedLocked(window->display()))
        return;

    WindowState &state = it != windows().end() ? it->second : windows()[window];
    if (state.pair == pair && state.content)
        return;
    auto *manager = managers()[window->display()].manager.get();
    state.surface = surface;
    if (!state.content)
        state.content = std::make_unique<Content>(manager->create(surface));
    if (!state.depth)
        state.depth = std::make_unique<Depth>(manager->get_depth(surface));
    state.content->set_content(pair ? QtWayland::kde_stereo_content_v1::content_side_by_side_full
                                    : QtWayland::kde_stereo_content_v1::content_none);
    state.depth->set_desktop_depth(pair ? 1 : 0);
    state.pair = pair;
    state.desktopDepth = pair;
}

void QWaylandStereoDepth::setDesktopDepth(QWaylandWindow *window, bool desktopDepth)
{
    QMutexLocker locker(&s_mutex);
    ::wl_surface *surface = window->wlSurface();
    auto it = windows().find(window);
    if (it != windows().end() && it->second.surface != surface) {
        windows().erase(it);
        it = windows().end();
    }
    if (!desktopDepth && it == windows().end())
        return;
    if (!surface || !isSupportedLocked(window->display()))
        return;

    WindowState &state = it != windows().end() ? it->second : windows()[window];
    if (state.depth && state.desktopDepth == desktopDepth)
        return;
    if (!state.depth) {
        state.surface = surface;
        state.depth = std::make_unique<Depth>(managers()[window->display()].manager->get_depth(surface));
    }
    state.depth->set_desktop_depth(desktopDepth ? 1 : 0);
    state.desktopDepth = desktopDepth;
}

void QWaylandStereoDepth::forgetWindow(QWaylandWindow *window)
{
    QMutexLocker locker(&s_mutex);
    windows().erase(window);
}

} // namespace QtWaylandClient

QT_END_NAMESPACE
