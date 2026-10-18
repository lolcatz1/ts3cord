#pragma once
// libmpv player in a qt widget (render api over opengl)
#include <QOpenGLWidget>
#include <mpv/client.h>
#include <mpv/render_gl.h>

class MpvWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;
    void load(const QString &url, bool loop = false);
    void command(const QStringList &args);
    void setOption(const char *name, const QString &value);
    void togglePause();
    void seek(double seconds);
    void setVolume(int percent);
    // audio files: no video surface needed, plays while hidden
    void setAudioOnly(bool on);
signals:
    void position(double seconds);
    void duration(double seconds);
    void paused(bool on);
    void videoSize(int w, int h);
    void failed(const QString &why);
protected:
    void initializeGL() override;
    void paintGL() override;
private slots:
    void events();
    void frame();
private:
    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr;
    // loadfile waits for the render context, else mpv drops the video
    QString m_pending;
    bool m_pendingLoop = false, m_audioOnly = false;
};
