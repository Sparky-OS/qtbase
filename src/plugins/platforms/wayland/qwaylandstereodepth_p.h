// Copyright (C) 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QWAYLANDSTEREODEPTH_P_H
#define QWAYLANDSTEREODEPTH_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtWaylandClient/qtwaylandclientglobal.h>

#include <QtCore/qtypes.h>

struct wl_registry;

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

class QWaylandDisplay;
class QWaylandWindow;

// A window drawn once per eye as the desktop's own depth (kde_stereo_content_v1 version 3):
// the two views side by side in one buffer, declared as a full side-by-side pair and marked as
// the desktop's depth rather than stereo content. Kept apart from the display's and the
// window's own state.
namespace QWaylandStereoDepth {

// The compositor's kde_stereo_content_manager_v1, bound at version 3 at most.
void addGlobal(QWaylandDisplay *display, ::wl_registry *registry, uint32_t id, uint32_t version);
void removeGlobal(QWaylandDisplay *display, uint32_t id);
void removeDisplay(QWaylandDisplay *display);

// Whether the compositor takes a window's pair as the desktop's own depth.
Q_WAYLANDCLIENT_EXPORT bool isSupported(QWaylandDisplay *display);

// Declares, with the window's next commit, that the buffer holds both eyes side by side and is
// the desktop's own depth, or withdraws it.
void setPair(QWaylandWindow *window, bool pair);
// Marks, with the window's next commit, the pair someone else declares (the EGL implementation
// for a surface with two views) as the desktop's own depth, or withdraws the mark. Any thread.
Q_WAYLANDCLIENT_EXPORT void setDesktopDepth(QWaylandWindow *window, bool desktopDepth);
// Before the window's surface goes away.
void forgetWindow(QWaylandWindow *window);

} // namespace QWaylandStereoDepth

} // namespace QtWaylandClient

QT_END_NAMESPACE

#endif
