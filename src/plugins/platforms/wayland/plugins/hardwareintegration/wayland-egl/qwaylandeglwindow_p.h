// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#pragma once

#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include "qwaylandeglinclude_p.h"
#include "qwaylandeglclientbufferintegration_p.h"

#include <atomic>

QT_BEGIN_NAMESPACE

class QOpenGLFramebufferObject;

namespace QtWaylandClient {

class QWaylandGLContext;

class Q_WAYLANDCLIENT_EXPORT QWaylandEglWindow : public QWaylandWindow
{
    Q_OBJECT
public:
    QWaylandEglWindow(QWindow *window, QWaylandDisplay *display);
    ~QWaylandEglWindow();
    WindowType windowType() const override;
    void ensureSize() override;

    void updateSurface(bool create);
    QRect contentsRect() const;

    EGLSurface eglSurface() const;
    GLuint contentFBO() const;
    GLuint contentTexture(int eye = 0) const;
    int contentEyes() const;
    bool needToUpdateContentFBO() const { return decoration() && (m_resize || !m_contentFBO); }

    void bindContentFBO();

    void invalidateSurface() override;

    QMutex* eglSurfaceLock();

    // A frame was presented: the next context made current on the window begins a new one.
    void frameSwapped();
    // With the EGL surface lock held, before a context is made current on the window: at the start
    // of a frame, a surface that should now have a different number of views is destroyed, so it
    // is created again with them.
    void beginFrameViews();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Whether an engine asks for the window's surface to have two views while it draws the
    // window's elements once per eye (the window's _q_stereo_views), and they can be had: the
    // pair is then the desktop's own depth rather than stereo content.
    bool pairWanted(const QSurfaceFormat &format) const;
    void destroySurface();

    QWaylandEglClientBufferIntegration *m_clientBufferIntegration = nullptr;
    struct wl_egl_window *m_waylandEglWindow = nullptr;

    // Locks any manipulation of the eglSurface size
    QMutex m_eglSurfaceLock;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    mutable bool m_resize = false;
    mutable QOpenGLFramebufferObject *m_contentFBO = nullptr;

    // Size used in the last call to wl_egl_window_resize
    QSize m_requestedSize;

    // Size of the buffer used by QWaylandWindow
    // This is always written to from the main thread, potentially read from the rendering thread
    QReadWriteLock m_bufferSizeLock;
    QSize m_bufferSize;

    // Written from the main thread, read from the rendering thread
    std::atomic<bool> m_pairRequested = false;
    // The surface's views, and whether its two views are a pair marked as the desktop's depth
    bool m_twoViews = false;
    bool m_pairSurface = false;
    bool m_frameSwapped = false;
};

}

QT_END_NAMESPACE
