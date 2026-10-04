// KeyText: the text of a shortcut spec as the shell writes it (a portable string such as
// "Ctrl+Shift+S", or a StandardKey), for key hints and tests. QML:
//     KeyText.portable(StandardKey.Save)        // "Ctrl+S" ("Ctrl" = ⌘ on macOS; Theme.keys() shows it)
//     KeyText.all(StandardKey.NextChild)        // every binding of a standard key on this platform
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class KeyText : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit KeyText(QObject* parent = nullptr) : QObject(parent) {}

    // The first (main) binding, portable text; "" for none.
    Q_INVOKABLE static QString portable(const QVariant& keys);
    // Every binding, portable text.
    Q_INVOKABLE static QStringList all(const QVariant& keys);
};
