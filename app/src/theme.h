#pragma once

#include <QColor>
#include <QString>

class QApplication;

// One place for the look: a soft dark theme with Ghost Engine's violet as the
// only accent colour.
namespace theme {

void apply(QApplication &app);

// Ctrl + / Ctrl - : every font and row grows together. 1.0 is normal.
qreal zoom();
void setZoom(QApplication &app, qreal factor);

QColor window();
QColor card();
QColor cardHover();
QColor cardSelected();
QColor accent();
QColor accentText();
QColor text();
QColor muted();

// A muted tint picked from the name, so a cover-less list keeps its colour.
QColor placeholderFor(const QString &name);

} // namespace theme
