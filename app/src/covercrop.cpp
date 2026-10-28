#include "covercrop.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kView = 520;       // the longest side of the picture on screen
constexpr int kHandle = 14;      // the corner that resizes
constexpr qreal kMinSide = 40;   // on screen

} // namespace

class CoverCropDialog::Canvas : public QWidget {
public:
    explicit Canvas(const QImage &image) : m_image(image)
    {
        m_scale = qMin(1.0, qreal(kView) / qMax(image.width(), image.height()));
        m_shown = QPixmap::fromImage(image.scaled(image.size() * m_scale, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        setFixedSize(m_shown.size());
        setMouseTracking(true);
        // Most of the picture, centred, with room left to see the corner
        // that resizes it.
        const qreal side = qMin(width(), height()) * 0.9;
        m_square = QRectF((width() - side) / 2, (height() - side) / 2, side, side);
    }

    QImage cropped() const
    {
        const QRect source(int(m_square.x() / m_scale), int(m_square.y() / m_scale),
                           int(m_square.width() / m_scale), int(m_square.width() / m_scale));
        return m_image.copy(source.intersected(m_image.rect()));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(0, 0, m_shown);
        QPainterPath outside;
        outside.addRect(rect());
        QPainterPath inside;
        inside.addRoundedRect(m_square, 10, 10);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 150));
        p.setPen(QPen(Qt::white, 2));
        p.drawRoundedRect(m_square, 10, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::accent());
        p.drawEllipse(handle());
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        m_grab = e->position();
        m_start = m_square;
        m_resizing = handle().adjusted(-6, -6, 6, 6).contains(e->position());
        m_moving = !m_resizing && m_square.contains(e->position());
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPointF d = e->position() - m_grab;
        if (m_moving) {
            m_square = m_start.translated(d);
            keepInside();
        } else if (m_resizing) {
            // From the top-left corner, kept square and inside the picture.
            const qreal room = qMin(width() - m_start.left(), height() - m_start.top());
            const qreal side = qBound(kMinSide, m_start.width() + qMax(d.x(), d.y()), room);
            m_square.setSize({side, side});
        } else {
            setCursor(handle().adjusted(-6, -6, 6, 6).contains(e->position()) ? Qt::SizeFDiagCursor
                      : m_square.contains(e->position()) ? Qt::SizeAllCursor : Qt::ArrowCursor);
            return;
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_moving = m_resizing = false; }

    void wheelEvent(QWheelEvent *e) override
    {
        // Around the square's centre.
        const QPointF c = m_square.center();
        const qreal side = qBound(kMinSide, m_square.width() * (e->angleDelta().y() > 0 ? 1.08 : 1 / 1.08),
                                  qreal(qMin(width(), height())));
        m_square = QRectF(c.x() - side / 2, c.y() - side / 2, side, side);
        keepInside();
        update();
    }

private:
    QRectF handle() const
    {
        return {m_square.right() - kHandle / 2.0, m_square.bottom() - kHandle / 2.0, qreal(kHandle), qreal(kHandle)};
    }

    void keepInside()
    {
        m_square.moveLeft(qBound(0.0, m_square.left(), width() - m_square.width()));
        m_square.moveTop(qBound(0.0, m_square.top(), height() - m_square.height()));
    }

    QImage m_image;
    QPixmap m_shown;
    qreal m_scale = 1;
    QRectF m_square, m_start;
    QPointF m_grab;
    bool m_moving = false, m_resizing = false;
};

CoverCropDialog::CoverCropDialog(const QImage &image, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Choose a cover");
    m_canvas = new Canvas(image);
    auto *hint = new QLabel("Drag the square to move it; drag its corner or use the wheel to resize it.");
    hint->setObjectName("statLabel");
    auto *ok = new QPushButton("Use this");
    ok->setObjectName("primary");
    ok->setDefault(true);
    auto *cancel = new QPushButton("Cancel");
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(20, 20, 20, 16);
    box->setSpacing(12);
    box->addWidget(m_canvas, 0, Qt::AlignCenter);
    box->addWidget(hint);
    box->addLayout(buttons);
}

QImage CoverCropDialog::cropped() const
{
    return m_canvas->cropped();
}
