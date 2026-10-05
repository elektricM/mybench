// WindowChrome on platforms without window buttons in the client area: nothing to report.
#include "windowchrome.h"

QRectF WindowChrome::buttonsRect(QWindow*) const
{
    return {};
}

void WindowChrome::setTitleHidden(QWindow*, bool) const {}
void WindowChrome::centerButtons(QWindow*, qreal) const {}
