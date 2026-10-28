#include "covercrop.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <functional>

namespace {

constexpr int kCanvas = 440;    // the area the picture moves in
constexpr int kFrame = 340;     // the square that becomes the cover, fixed in the middle
constexpr qreal kMaxZoom = 4;   // times the smallest zoom that still fills the square

} // namespace

// The square stays put and the picture moves and zooms behind it, as in
// Discord's avatar editor. The picture always covers the square, so a cover
// never has an empty edge.
class CoverCropDialog::Canvas : public QWidget {
public:
    explicit Canvas(const QImage &image) : m_original(image), m_image(image)
    {
        setFixedSize(kCanvas, kCanvas);
        setCursor(Qt::OpenHandCursor);
        reset();
    }

    // As it was opened: unturned, unzoomed, centred.
    void reset()
    {
        m_image = m_original;
        m_fit = qreal(kFrame) / qMin(m_image.width(), m_image.height());
        m_zoom = m_fit;
        m_center = QPointF(m_image.width() / 2.0, m_image.height() / 2.0);
        update();
    }

    // A quarter turn clockwise; the same spot stays in the square.
    void rotate()
    {
        const qreal level = this->level();
        const QPointF c = m_center;
        const int oldHeight = m_image.height();
        m_image = m_image.transformed(QTransform().rotate(90));
        m_fit = qreal(kFrame) / qMin(m_image.width(), m_image.height());
        m_center = QPointF(oldHeight - c.y(), c.x());
        setLevel(level);
    }

    // 0..1 along the slider: the smallest zoom up to kMaxZoom times it.
    void setLevel(qreal level)
    {
        m_zoom = m_fit * (1 + level * (kMaxZoom - 1));
        clamp();
        update();
    }
    qreal level() const { return (m_zoom / m_fit - 1) / (kMaxZoom - 1); }
    std::function<void(qreal)> onLevel;

    QImage cropped() const
    {
        const qreal side = kFrame / m_zoom;
        const QRectF source(m_center.x() - side / 2, m_center.y() - side / 2, side, side);
        return m_image.copy(source.toRect().intersected(m_image.rect()));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor(16, 15, 20));
        // The picture, placed so that m_center sits in the middle of the canvas.
        const QSizeF shown = QSizeF(m_image.size()) * m_zoom;
        const QPointF topLeft = QPointF(width() / 2.0, height() / 2.0) - m_center * m_zoom;
        p.drawImage(QRectF(topLeft, shown), m_image);
        const QRectF frame = frameRect();
        QPainterPath outside;
        outside.addRect(rect());
        QPainterPath inside;
        inside.addRoundedRect(frame, 12, 12);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 160));
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(frame, 12, 12);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        m_grab = e->position();
        m_start = m_center;
        setCursor(Qt::ClosedHandCursor);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton))
            return;
        // The picture follows the mouse.
        m_center = m_start - (e->position() - m_grab) / m_zoom;
        clamp();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override { setCursor(Qt::OpenHandCursor); }

    void wheelEvent(QWheelEvent *e) override
    {
        setLevel(qBound(0.0, level() + (e->angleDelta().y() > 0 ? 0.06 : -0.06), 1.0));
        if (onLevel)
            onLevel(level());
    }

private:
    QRectF frameRect() const
    {
        return {(width() - kFrame) / 2.0, (height() - kFrame) / 2.0, qreal(kFrame), qreal(kFrame)};
    }

    void clamp()
    {
        // Half the square, in picture pixels, must stay inside the picture.
        const qreal half = kFrame / 2.0 / m_zoom;
        m_center.setX(qBound(half, m_center.x(), m_image.width() - half));
        m_center.setY(qBound(half, m_center.y(), m_image.height() - half));
    }

    QImage m_original, m_image;
    qreal m_fit = 1, m_zoom = 1;
    QPointF m_center, m_start, m_grab;
};

CoverCropDialog::CoverCropDialog(const QImage &image, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Choose a cover");
    m_canvas = new Canvas(image);

    auto *zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(0, 1000);
    zoom->setFixedWidth(240);
    connect(zoom, &QSlider::valueChanged, this, [this](int v) { m_canvas->setLevel(v / 1000.0); });
    m_canvas->onLevel = [zoom](qreal level) {
        const QSignalBlocker quiet(zoom);
        zoom->setValue(int(level * 1000));
    };
    // A picture mark, small and large, at the slider's two ends.
    auto *small = new QLabel(QString(QChar(0xEB9F)));
    auto *large = new QLabel(QString(QChar(0xEB9F)));
    small->setStyleSheet("font-family: 'Segoe Fluent Icons'; font-size: 9pt; color: rgb(170,168,182);");
    large->setStyleSheet("font-family: 'Segoe Fluent Icons'; font-size: 15pt; color: rgb(170,168,182);");
    auto *turn = new QPushButton(QString(QChar(0xE7AD)));
    turn->setObjectName("glyph");
    turn->setToolTip("Rotate");
    turn->setCursor(Qt::PointingHandCursor);
    connect(turn, &QPushButton::clicked, this, [this] { m_canvas->rotate(); });
    auto *zoomRow = new QHBoxLayout;
    zoomRow->addStretch();
    zoomRow->addWidget(small);
    zoomRow->addWidget(zoom);
    zoomRow->addWidget(large);
    zoomRow->addSpacing(16);
    zoomRow->addWidget(turn);
    zoomRow->addStretch();

    auto *hint = new QLabel("Drag the picture to move it; zoom with the slider or the mouse wheel.");
    hint->setObjectName("statLabel");
    hint->setAlignment(Qt::AlignCenter);
    auto *ok = new QPushButton("Use this");
    ok->setObjectName("primary");
    ok->setDefault(true);
    auto *cancel = new QPushButton("Cancel");
    // Back to how the picture opened; removing a cover is on the playlist's
    // own menu, this only undoes the moving here.
    auto *reset = new QPushButton("Reset");
    reset->setObjectName("link");
    reset->setCursor(Qt::PointingHandCursor);
    connect(reset, &QPushButton::clicked, this, [this, zoom] {
        m_canvas->reset();
        const QSignalBlocker quiet(zoom);
        zoom->setValue(0);
    });
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(20, 20, 20, 16);
    box->setSpacing(12);
    box->addWidget(m_canvas, 0, Qt::AlignCenter);
    box->addLayout(zoomRow);
    box->addWidget(hint);
    box->addLayout(buttons);
}

QImage CoverCropDialog::cropped() const
{
    return m_canvas->cropped();
}
