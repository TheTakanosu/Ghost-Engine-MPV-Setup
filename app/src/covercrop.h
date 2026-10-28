#pragma once

#include <QDialog>
#include <QImage>

// Picks the square of a picture that becomes a playlist's cover: the square
// stays fixed while the picture is dragged and zoomed behind it, and the
// rest of the picture is dimmed, the way profile pictures are cropped.
class CoverCropDialog : public QDialog {
public:
    explicit CoverCropDialog(const QImage &image, QWidget *parent = nullptr);

    // The chosen square, at the picture's own resolution.
    QImage cropped() const;

private:
    class Canvas;
    Canvas *m_canvas;
};
