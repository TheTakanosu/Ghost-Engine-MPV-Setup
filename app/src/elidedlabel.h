#pragma once

#include <QLabel>
#include <QResizeEvent>

// One line, cut short with "…" to the room it is given. A plain QLabel asks
// for its whole text's width, so one long next title held the panel wider
// than the window and pushed the cover and the photos off its right edge.
class ElidedLabel : public QLabel {
public:
    void setFullText(const QString &text)
    {
        m_full = text;
        setToolTip(text);
        fit();
    }
    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }
    QSize sizeHint() const override { return {0, QLabel::sizeHint().height()}; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        fit();
    }

private:
    void fit() { setText(fontMetrics().elidedText(m_full, Qt::ElideRight, width())); }
    QString m_full;
};
