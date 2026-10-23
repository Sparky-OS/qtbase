// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "qwaylandeglwindow_p.h"

#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandsurface_p.h>
#include <QtWaylandClient/private/qwaylandstereodepth_p.h>
#include "qwaylandglcontext_p.h"

#include <QtGui/private/qeglconvenience_p.h>

#include <QDebug>
#include <QtCore/QEvent>
#include <QtGui/QWindow>
#include <qpa/qwindowsysteminterface.h>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

QWaylandEglWindow::QWaylandEglWindow(QWindow *window, QWaylandDisplay *display)
    : QWaylandWindow(window, display)
    , m_clientBufferIntegration(static_cast<QWaylandEglClientBufferIntegration *>(mDisplay->clientBufferIntegration()))
{
    connect(display, &QWaylandDisplay::connected, this, [this] {
        m_clientBufferIntegration = static_cast<QWaylandEglClientBufferIntegration *>(
                mDisplay->clientBufferIntegration());
    });
    m_pairRequested = window->property("_q_stereo_views").toInt() == 2;
    window->installEventFilter(this);
    ensureSize();
}

QWaylandEglWindow::~QWaylandEglWindow()
{
    window()->removeEventFilter(this);
    if (m_eglSurface) {
        eglDestroySurface(m_clientBufferIntegration->eglDisplay(), m_eglSurface);
        m_eglSurface = 0;
    }

    if (m_waylandEglWindow)
        wl_egl_window_destroy(m_waylandEglWindow);

    delete m_contentFBO;
}

QWaylandWindow::WindowType QWaylandEglWindow::windowType() const
{
    return QWaylandWindow::Egl;
}

void QWaylandEglWindow::ensureSize()
{
    // this is always called on the main thread
    QRect rect = geometry();
    QMargins margins = clientSideMargins();
    QSize sizeWithMargins = (rect.size() + QSize(margins.left() + margins.right(), margins.top() + margins.bottom())) * scale();
    {
        QWriteLocker lock(&m_bufferSizeLock);
        m_bufferSize = sizeWithMargins;
    }

    QMutexLocker lock (&m_eglSurfaceLock);
    updateSurface(false);
}

void QWaylandEglWindow::updateSurface(bool create)
{
    // eglSurfaceLock should be locked before calling this method

    QSize sizeWithMargins;
    {
        QReadLocker lock(&m_bufferSizeLock);
        sizeWithMargins = m_bufferSize;
    }

    // wl_egl_windows must have both width and height > 0
    // mesa's egl returns NULL if we try to create a, invalid wl_egl_window, however not all EGL
    // implementations may do that, so check the size ourself. Besides, we must deal with resizing
    // a valid window to 0x0, which would make it invalid. Hence, destroy it.
    if (sizeWithMargins.isEmpty()) {
        destroySurface();
        mOffset = QPoint();
    } else {
        QReadLocker locker(&mSurfaceLock);
        if (m_waylandEglWindow) {
            int current_width = 0;
            int current_height = 0;
            static bool disableResizeCheck = qgetenv("QT_WAYLAND_DISABLE_RESIZECHECK").toInt();

            if (!disableResizeCheck) {
                wl_egl_window_get_attached_size(m_waylandEglWindow, &current_width, &current_height);
            }
            if (disableResizeCheck || (current_width != sizeWithMargins.width() || current_height != sizeWithMargins.height()) || m_requestedSize != sizeWithMargins) {
                wl_egl_window_resize(m_waylandEglWindow, sizeWithMargins.width(), sizeWithMargins.height(), mOffset.x(), mOffset.y());
                m_requestedSize = sizeWithMargins;
                mOffset = QPoint();

                m_resize = true;
            }
        } else if (create && mSurface) {
            // The EGL implementation sizes a stereo surface itself, from the window it is created with and
            // not from the size Qt gives the surface later: wait until the window is shown and has its size
            const bool pair = pairWanted(window()->requestedFormat());
            if (!window()->isVisible() && (m_clientBufferIntegration->supportsStereo(window()->requestedFormat()) || pair))
                return;

            wl_egl_window *eglWindow = wl_egl_window_create(mSurface->object(), sizeWithMargins.width(), sizeWithMargins.height());
            if (Q_UNLIKELY(!eglWindow)) {
                qCWarning(lcQpaWayland, "Could not create wl_egl_window with size %dx%d\n", sizeWithMargins.width(), sizeWithMargins.height());
                return;
            }

            QSurfaceFormat fmt = window()->requestedFormat();
            // a format asking for more than 8 bits per channel keeps the alpha it asks for
            if (mDisplay->supportsWindowDecoration() && fmt.redBufferSize() <= 8)
                fmt.setAlphaBufferSize(8);
            EGLConfig eglConfig = q_configFromGLFormat(m_clientBufferIntegration->eglDisplay(), fmt);
            QSurfaceFormat eglFormat = q_glFormatFromConfig(m_clientBufferIntegration->eglDisplay(), eglConfig, fmt);

            // Two views make the surface a stereo one: the left and the right back buffer
            const bool stereo = m_clientBufferIntegration->supportsStereo(fmt) || pair;
            const EGLint surfaceAttribs[] = { EGL_MULTIVIEW_VIEW_COUNT_EXT, 2, EGL_NONE };
            if (stereo)
                eglFormat.setStereo(true);
            setFormat(eglFormat);

            // The EGL implementation declares the two views as a full side-by-side pair; an engine's
            // pair is marked, with the same first commit, as the desktop's own depth.
            QWaylandStereoDepth::setDesktopDepth(this, pair);

            EGLSurface eglSurface = eglCreateWindowSurface(m_clientBufferIntegration->eglDisplay(), eglConfig, (EGLNativeWindowType) eglWindow, stereo ? surfaceAttribs : nullptr);
            if (Q_UNLIKELY(eglSurface == EGL_NO_SURFACE)) {
                qCWarning(lcQpaWayland, "Could not create EGL surface (EGL error 0x%x)\n", eglGetError());
                wl_egl_window_destroy(eglWindow);
                return;
            }

            m_waylandEglWindow = eglWindow;
            m_eglSurface = eglSurface;
            m_requestedSize = sizeWithMargins;
            m_twoViews = stereo;
            m_pairSurface = pair;
        }
    }
}

QRect QWaylandEglWindow::contentsRect() const
{
    QRect r = geometry();
    QMargins m = clientSideMargins();
    return QRect(m.left(), m.bottom(), r.width(), r.height());
}

void QWaylandEglWindow::invalidateSurface()
{
    QMutexLocker lock (&m_eglSurfaceLock);

    destroySurface();
    delete m_contentFBO;
    m_contentFBO = nullptr;
}

void QWaylandEglWindow::destroySurface()
{
    // eglSurfaceLock should be locked before calling this method
    if (m_eglSurface) {
        eglDestroySurface(m_clientBufferIntegration->eglDisplay(), m_eglSurface);
        m_eglSurface = 0;
    }
    if (m_waylandEglWindow) {
        wl_egl_window_destroy(m_waylandEglWindow);
        m_waylandEglWindow = nullptr;
    }
    m_twoViews = false;
    m_pairSurface = false;
}

bool QWaylandEglWindow::pairWanted(const QSurfaceFormat &format) const
{
    return m_pairRequested.load(std::memory_order_acquire)
            && m_clientBufferIntegration->supportsTwoViews(format)
            && QWaylandStereoDepth::isSupported(mDisplay);
}

bool QWaylandEglWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window() && event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "_q_stereo_views") {
        m_pairRequested.store(window()->property("_q_stereo_views").toInt() == 2, std::memory_order_release);
    }
    return QWaylandWindow::eventFilter(watched, event);
}

void QWaylandEglWindow::frameSwapped()
{
    QMutexLocker lock(&m_eglSurfaceLock);
    m_frameSwapped = true;
}

void QWaylandEglWindow::beginFrameViews()
{
    // eglSurfaceLock should be locked before calling this method. Within a frame, which an engine
    // drawing a pair renders in two parts, the surface keeps its views.
    if (!m_frameSwapped)
        return;
    m_frameSwapped = false;
    if (!m_eglSurface)
        return;

    const QSurfaceFormat fmt = window()->requestedFormat();
    const bool pair = pairWanted(fmt);
    if (pair == m_pairSurface)
        return;

    if (m_clientBufferIntegration->supportsStereo(fmt)) {
        // A stereo window has two views either way: only the mark changes.
        QWaylandStereoDepth::setDesktopDepth(this, pair);
        m_pairSurface = pair;
        return;
    }

    // The context is made current again, on the new surface, right after this.
    eglMakeCurrent(m_clientBufferIntegration->eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    const bool wasPair = m_pairSurface;
    destroySurface();
    // The EGL implementation withdrew its declaration with the surface; the mark goes with the
    // same next commit, the first of the surface made again.
    if (wasPair)
        QWaylandStereoDepth::setDesktopDepth(this, false);
}

EGLSurface QWaylandEglWindow::eglSurface() const
{
    return m_eglSurface;
}

QMutex* QWaylandEglWindow::eglSurfaceLock()
{
    return &m_eglSurfaceLock;
}

GLuint QWaylandEglWindow::contentFBO() const
{
    if (!decoration())
        return 0;

    if (m_resize || !m_contentFBO) {
        QOpenGLFramebufferObject *old = m_contentFBO;
        QSize fboSize = geometry().size() * scale();
        m_contentFBO = new QOpenGLFramebufferObject(fboSize.width(), fboSize.height(), QOpenGLFramebufferObject::CombinedDepthStencil);
        // A stereo window has a slot per eye: the right eye is rendered into the second attachment
        if (m_twoViews) {
            m_contentFBO->bind();
            m_contentFBO->addColorAttachment(fboSize);
        }

        delete old;
        m_resize = false;
    }

    return m_contentFBO->handle();
}

GLuint QWaylandEglWindow::contentTexture(int eye) const
{
    return m_contentFBO->textures().value(eye);
}

int QWaylandEglWindow::contentEyes() const
{
    return m_contentFBO->textures().size();
}

void QWaylandEglWindow::bindContentFBO()
{
    if (decoration()) {
        contentFBO();
        m_contentFBO->bind();
    }
}

}

QT_END_NAMESPACE

#include "moc_qwaylandeglwindow_p.cpp"
