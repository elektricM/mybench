// Fonts: platform fonts for the Theme (QML singleton `Fonts`).
//
//     Fonts.ui     the system UI font (San Francisco, Segoe UI, Cantarell/Noto…)
//     Fonts.mono   the system fixed-pitch font (SF Mono/Menlo, Consolas, DejaVu Sans Mono…)
//     Fonts.uiPixelSize  the UI font's size in pixels (macOS 13, Windows 9 pt = 12 at 96 dpi,
//                  KDE/GNOME the user's size): Theme.fontSizeBase, every type size derives from it
//
// Numeric readouts use the UI font with tabular figures (font.features: { "tnum": 1 }).
#pragma once

#include <QFont>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace mybench {

class Fonts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QFont ui READ ui CONSTANT)
    Q_PROPERTY(QFont mono READ mono CONSTANT)
    Q_PROPERTY(QString uiFamily READ uiFamily CONSTANT)
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)
    Q_PROPERTY(int uiPixelSize READ uiPixelSize CONSTANT)

public:
    explicit Fonts(QObject* parent = nullptr);

    QFont ui() const;
    QFont mono() const;
    QString uiFamily() const { return ui().family(); }
    QString monoFamily() const { return mono().family(); }
    int uiPixelSize() const;
};

}  // namespace mybench
