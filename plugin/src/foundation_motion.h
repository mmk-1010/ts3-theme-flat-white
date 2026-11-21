#pragma once

#include <QApplication>
#include <QEasingCurve>
#include <QScrollBar>
#include <QWheelEvent>
#include <windows.h>

#include "foundation_tokens.h"

namespace foundation {

inline bool animationsEnabled() {
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled;
}

inline QEasingCurve motionEasing() { return QEasingCurve(QEasingCurve::OutCubic); }

inline bool isMouseWheelNotch(const QWheelEvent& event) {
    const QPoint angle = event.angleDelta();
    return event.pixelDelta().isNull() && angle.x() == 0 &&
           (angle.y() == 120 || angle.y() == -120) && event.phase() == Qt::NoScrollPhase &&
           event.modifiers() == Qt::NoModifier && event.buttons() == Qt::NoButton &&
           event.source() == Qt::MouseEventNotSynthesized;
}

inline int wheelTarget(const QScrollBar& bar, int base, int verticalAngle) {
    const qint64 lines = QApplication::wheelScrollLines();
    const qint64 distance = qMin<qint64>(bar.pageStep(), lines * bar.singleStep());
    if (distance <= 0) return base;
    const bool upwards = (verticalAngle > 0) == bar.invertedControls();
    return int(qBound<qint64>(bar.minimum(), base + (upwards ? -distance : distance), bar.maximum()));
}

}  // namespace foundation
