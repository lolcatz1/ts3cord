#include "mpvwidget.h"

#include <QMetaObject>
#include <QOpenGLContext>
#include <clocale>

static void *procAddress(void *, const char *name) {
    auto ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name))) : nullptr;
}

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent) {
    // mpv needs the c numeric locale
    std::setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", qEnvironmentVariable("TS3CORD_HWDEC", "no").toUtf8().constData());
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "audio-display", "embedded-first");
    mpv_set_option_string(m_mpv, "user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36");
    mpv_initialize(m_mpv);
    mpv_request_log_messages(m_mpv, "warn");
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "dheight", MPV_FORMAT_INT64);
    mpv_set_wakeup_callback(m_mpv, [](void *self) { QMetaObject::invokeMethod(static_cast<MpvWidget *>(self), "events", Qt::QueuedConnection); }, this);
}

MpvWidget::~MpvWidget() {
    makeCurrent();
    if (m_ctx) mpv_render_context_free(m_ctx);
    doneCurrent();
    mpv_terminate_destroy(m_mpv);
}

void MpvWidget::initializeGL() {
    mpv_opengl_init_params gl{procAddress, nullptr};
    mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                              {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
                              {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&m_ctx, m_mpv, params) < 0) {
        emit failed(tr("could not start the video renderer"));
        return;
    }
    mpv_render_context_set_update_callback(m_ctx, [](void *self) { QMetaObject::invokeMethod(static_cast<MpvWidget *>(self), "frame", Qt::QueuedConnection); }, this);
    if (!m_pending.isEmpty()) {
        load(m_pending, m_pendingLoop);
        m_pending.clear();
    }
}

void MpvWidget::paintGL() {
    if (!m_ctx) return;
    auto dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{int(defaultFramebufferObject()), int(width() * dpr), int(height() * dpr), 0};
    int flip = 1;
    mpv_render_param params[]{{MPV_RENDER_PARAM_OPENGL_FBO, &fbo}, {MPV_RENDER_PARAM_FLIP_Y, &flip}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(m_ctx, params);
}

void MpvWidget::frame() {
    if (m_ctx && (mpv_render_context_update(m_ctx) & MPV_RENDER_UPDATE_FRAME)) update();
}

void MpvWidget::events() {
    while (m_mpv) {
        auto ev = mpv_wait_event(m_mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto p = static_cast<mpv_event_property *>(ev->data);
            QByteArray name(p->name);
            if (p->format == MPV_FORMAT_DOUBLE && name == "time-pos") emit position(*static_cast<double *>(p->data));
            else if (p->format == MPV_FORMAT_DOUBLE && name == "duration") emit duration(*static_cast<double *>(p->data));
            else if (p->format == MPV_FORMAT_FLAG && name == "pause") emit paused(*static_cast<int *>(p->data));
            else if (p->format == MPV_FORMAT_INT64 && (name == "dwidth" || name == "dheight")) {
                int64_t w = 0, h = 0;
                mpv_get_property(m_mpv, "dwidth", MPV_FORMAT_INT64, &w);
                mpv_get_property(m_mpv, "dheight", MPV_FORMAT_INT64, &h);
                if (w > 0 && h > 0) emit videoSize(int(w), int(h));
            }
        } else if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto m = static_cast<mpv_event_log_message *>(ev->data);
            fprintf(stderr, "mpv %s: %s", m->prefix, m->text);
        } else if (ev->event_id == MPV_EVENT_END_FILE) {
            auto e = static_cast<mpv_event_end_file *>(ev->data);
            if (e->reason == MPV_END_FILE_REASON_ERROR) emit failed(QString::fromUtf8(mpv_error_string(e->error)));
        }
    }
}

void MpvWidget::command(const QStringList &args) {
    QList<QByteArray> keep;
    QVector<const char *> argv;
    for (auto &a : args) {
        keep << a.toUtf8();
        argv << keep.last().constData();
    }
    argv << nullptr;
    mpv_command_async(m_mpv, 0, argv.data());
}

void MpvWidget::setOption(const char *name, const QString &value) {
    mpv_set_property_string(m_mpv, name, value.toUtf8().constData());
}

void MpvWidget::load(const QString &url, bool loop) {
    if (!m_ctx && !m_audioOnly) {
        m_pending = url;
        m_pendingLoop = loop;
        return;
    }
    setOption("loop-file", loop ? "inf" : "no");
    command({"loadfile", url});
}

void MpvWidget::togglePause() { command({"cycle", "pause"}); }
void MpvWidget::seek(double s) { command({"seek", QString::number(s), "absolute"}); }
void MpvWidget::setVolume(int v) { setOption("volume", QString::number(v)); }

void MpvWidget::setAudioOnly(bool on) {
    m_audioOnly = on;
    setOption("vid", on ? "no" : "auto");
}
