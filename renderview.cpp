#include "renderview.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static const char *DARK_BG    = "#0a0e14";
static const char *BORDER_CLR = "#00d4ff";

RenderView::RenderView(QWidget *parent)
    : QWidget(parent)
    , m_loadingText("请加载CSV数据并点击播放")
{
    setAttribute(Qt::WA_NativeWindow, true);
    setMinimumSize(400, 300);
    setStyleSheet(QString("QWidget { background: %1; border: 1px solid %2; border-radius: 8px; }")
                  .arg(DARK_BG, BORDER_CLR));
}

RenderView::~RenderView()
{
    releaseEmbeddedWindow();
}

/* ------------------------------------------------------------------ */
/*  悬浮覆盖方案（Overlay）                                           */
/*  PyBullet 窗口不做子窗口，而是保持为独立顶级窗口。                     */
/*  通过 owner 关系绑定到 Qt 主窗口上方，定位到 RenderView 区域。         */
/*  视觉上像嵌入，但鼠标/键盘事件完全由 PyBullet 自己处理，零干扰。       */
/* ------------------------------------------------------------------ */

void RenderView::embedWindow(quintptr hwnd)
{
    if (hwnd == 0) return;
    releaseEmbeddedWindow();
    m_embeddedHwnd = hwnd;

#ifdef Q_OS_WIN
    HWND overlayHwnd = reinterpret_cast<HWND>(hwnd);

    // ① 无边框弹出窗口（不是 WS_CHILD！）
    LONG style = GetWindowLong(overlayHwnd, GWL_STYLE);
    style = (style & ~(WS_OVERLAPPEDWINDOW | WS_CHILD | WS_THICKFRAME))
            | WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLong(overlayHwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLong(overlayHwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_APPWINDOW;       // 不在任务栏单独显示
    exStyle |= WS_EX_TOOLWINDOW;
    SetWindowLong(overlayHwnd, GWL_EXSTYLE, exStyle);

    // ② 设为 Qt 主窗口的 owned window
    //    owner ≠ parent：不会变成子窗口，不影响消息路由
    //    效果：始终在主窗口上方，跟随最小化/恢复
    QWidget *mainWin = window();
    if (mainWin) {
        HWND ownerHwnd = reinterpret_cast<HWND>(mainWin->winId());
        SetWindowLongPtr(overlayHwnd, GWLP_HWNDPARENT,
                         reinterpret_cast<LONG_PTR>(ownerHwnd));
    }

    // ③ 定位到 RenderView 区域，并置于 Qt 子窗口之上（否则 SWP_NOZORDER 时常被盖住呈黑屏）
    syncOverlayPosition();
    // 布局/嵌入后一帧再同步一次，避免首次 geometry 未稳定
    QTimer::singleShot(0, this, [this]() { syncOverlayPosition(); });

    // ④ 监听主窗口移动/缩放
    if (mainWin) {
        mainWin->installEventFilter(this);
    }

    // ⑤ 定时器兜底（处理 splitter 拖拽等事件遗漏场景）
    m_syncTimer = new QTimer(this);
    connect(m_syncTimer, &QTimer::timeout, this, &RenderView::syncOverlayPosition);
    m_syncTimer->start(30);
#endif

    m_loadingText.clear();
    setStyleSheet(QString("QWidget { background: %1; }").arg(DARK_BG));
    update();
}

void RenderView::releaseEmbeddedWindow()
{
    m_panActive = false;
    if (m_syncTimer) {
        m_syncTimer->stop();
        delete m_syncTimer;
        m_syncTimer = nullptr;
    }

    // 移除主窗口事件过滤器
    if (window()) {
        window()->removeEventFilter(this);
    }

#ifdef Q_OS_WIN
    if (m_embeddedHwnd != 0) {
        HWND hwnd = reinterpret_cast<HWND>(m_embeddedHwnd);
        if (IsWindow(hwnd)) {
            SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
        }
    }
#endif
    m_embeddedHwnd = 0;
}

/* ---- 将 PyBullet 窗口精确覆盖到 RenderView 的屏幕区域 ---- */
void RenderView::syncOverlayPosition()
{
#ifdef Q_OS_WIN
    if (m_embeddedHwnd == 0) return;
    HWND hwnd = reinterpret_cast<HWND>(m_embeddedHwnd);
    if (!IsWindow(hwnd)) return;

    // setOverlayVisible(false) 期间完全跳过，保持隐藏
    if (m_overlayHidden) return;

    // 不可见 / 最小化 → 隐藏覆盖窗口
    if (!isVisible() || (window() && window()->isMinimized())) {
        if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
        return;
    }

    // 用 Win32 API 直接获取物理像素坐标
    // 避免 Qt 逻辑坐标在高 DPI（125%/150%）笔记本屏幕上错位
    HWND thisHwnd = reinterpret_cast<HWND>(winId());
    RECT rc;
    GetClientRect(thisHwnd, &rc);
    POINT topLeft = { rc.left, rc.top };
    ClientToScreen(thisHwnd, &topLeft);

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    pollPanGesture(topLeft.x, topLeft.y, w, h);
    if (m_raiseOverlayToTop) {
        // 置于 Qt 子窗口之上，避免初始黑屏；数据分析窗口打开时关闭此行为（见 setRaiseOverlayToTop）
        SetWindowPos(hwnd, HWND_TOP, topLeft.x, topLeft.y, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        // 只更新位置尺寸，不改变 Z 序，浮动分析窗口可保持在最前
        SetWindowPos(hwnd, nullptr, topLeft.x, topLeft.y, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOZORDER);
    }
#endif
}

void RenderView::pollPanGesture(int left, int top, int width, int height)
{
#ifdef Q_OS_WIN
    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        m_panActive = false;
        return;
    }

    const bool rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool inside = cursor.x >= left && cursor.x < left + width
                     && cursor.y >= top && cursor.y < top + height;
    if (!rightDown) {
        m_panActive = false;
        return;
    }

    if (!m_panActive) {
        if (!inside)
            return;
        m_panActive = true;
        m_lastPanCursor = QPoint(cursor.x, cursor.y);
        return;
    }

    const QPoint current(cursor.x, cursor.y);
    const QPoint delta = current - m_lastPanCursor;
    m_lastPanCursor = current;
    if (!delta.isNull())
        emit cameraPanRequested(delta.x(), delta.y());
#else
    Q_UNUSED(left);
    Q_UNUSED(top);
    Q_UNUSED(width);
    Q_UNUSED(height);
#endif
}

void RenderView::setRaiseOverlayToTop(bool raise)
{
    m_raiseOverlayToTop = raise;
#ifdef Q_OS_WIN
    syncOverlayPosition();
#endif
}

void RenderView::setOverlayVisible(bool visible)
{
#ifdef Q_OS_WIN
    if (m_embeddedHwnd == 0) return;
    HWND hwnd = reinterpret_cast<HWND>(m_embeddedHwnd);
    if (!IsWindow(hwnd)) return;
    if (visible) {
        m_overlayHidden = false;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        syncOverlayPosition();
    } else {
        m_overlayHidden = true;
        ShowWindow(hwnd, SW_HIDE);
    }
#else
    Q_UNUSED(visible);
#endif
}

/* ---- 标准接口 ---- */
QPixmap RenderView::grabScreenshot() const
{
#ifdef Q_OS_WIN
    if (m_embeddedHwnd != 0) {
        HWND hwnd = reinterpret_cast<HWND>(m_embeddedHwnd);
        if (IsWindow(hwnd)) {
            QScreen *scr = QGuiApplication::primaryScreen();
            if (scr)
                return scr->grabWindow(reinterpret_cast<WId>(hwnd));
        }
    }
#endif
    return QPixmap();
}

void RenderView::setLoadingText(const QString &text)
{
    m_loadingText = text;
    update();
}

void RenderView::clear()
{
    releaseEmbeddedWindow();
    m_loadingText = "请加载CSV数据并点击播放";
    setStyleSheet(QString("QWidget { background: %1; border: 1px solid %2; border-radius: 8px; }")
                  .arg(DARK_BG, BORDER_CLR));
    update();
}

/* ---- 位置/可见性同步 ---- */
void RenderView::resizeEvent(QResizeEvent *event)
{
    syncOverlayPosition();
    QWidget::resizeEvent(event);
}

void RenderView::moveEvent(QMoveEvent *event)
{
    syncOverlayPosition();
    QWidget::moveEvent(event);
}

void RenderView::showEvent(QShowEvent *event)
{
    syncOverlayPosition();
    QWidget::showEvent(event);
}

void RenderView::hideEvent(QHideEvent *event)
{
#ifdef Q_OS_WIN
    if (m_embeddedHwnd != 0) {
        HWND hwnd = reinterpret_cast<HWND>(m_embeddedHwnd);
        if (IsWindow(hwnd)) ShowWindow(hwnd, SW_HIDE);
    }
#endif
    QWidget::hideEvent(event);
}

bool RenderView::eventFilter(QObject *obj, QEvent *event)
{
    // 主窗口移动/缩放/状态变化时同步覆盖窗口位置
    if (obj == window() && m_embeddedHwnd != 0) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
            syncOverlayPosition();
            break;
        case QEvent::WindowActivate:
            // 数据分析等浮动窗打开时不要抢最前
#ifdef Q_OS_WIN
            if (m_raiseOverlayToTop && !m_overlayHidden) {
                HWND hwnd = reinterpret_cast<HWND>(m_embeddedHwnd);
                if (IsWindow(hwnd))
                    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
#endif
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(obj, event);
}

/* ---- 无 PyBullet 时显示占位文字 ---- */
void RenderView::paintEvent(QPaintEvent *)
{
    if (m_embeddedHwnd != 0) return;

    if (!m_loadingText.isEmpty()) {
        QPainter painter(this);
        painter.setPen(QColor(BORDER_CLR));
        QFont f = font();
        f.setPointSize(16);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, m_loadingText);
    }
}
