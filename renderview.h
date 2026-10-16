#ifndef RENDERVIEW_H
#define RENDERVIEW_H

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QPoint>

class RenderView : public QWidget
{
    Q_OBJECT

public:
    explicit RenderView(QWidget *parent = nullptr);
    ~RenderView();

    void embedWindow(quintptr hwnd);
    void releaseEmbeddedWindow();
    void setLoadingText(const QString &text);
    void clear();
    /// 为 false 时同步位置但不再 HWND_TOP，避免盖住「数据分析」等浮动窗口（定时器也不会反复置顶）
    void setRaiseOverlayToTop(bool raise);
    /// 临时隐藏/恢复 PyBullet 覆盖窗口（用于文件对话框等模态场景）
    void setOverlayVisible(bool visible);
    /// 截取当前 PyBullet 渲染画面
    QPixmap grabScreenshot() const;

signals:
    void cameraPanRequested(double deltaX, double deltaY);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void syncOverlayPosition();
    void pollPanGesture(int left, int top, int width, int height);

    QString m_loadingText;
    quintptr m_embeddedHwnd = 0;
    QTimer *m_syncTimer = nullptr;
    bool m_raiseOverlayToTop = true;
    bool m_overlayHidden = false;   ///< setOverlayVisible(false) 期间为 true，阻止定时器/事件恢复显示
    bool m_panActive = false;
    QPoint m_lastPanCursor;
};

#endif // RENDERVIEW_H
