#include "keytext.h"

#include <QKeySequence>

QStringList KeyText::all(const QVariant& keys)
{
    QList<QKeySequence> sequences;
    if (keys.metaType().id() == QMetaType::Int)
        sequences = QKeySequence::keyBindings(static_cast<QKeySequence::StandardKey>(keys.toInt()));
    else if (keys.canConvert<QString>() && !keys.toString().isEmpty())
        sequences = {QKeySequence::fromString(keys.toString(), QKeySequence::PortableText)};
    QStringList texts;
    for (const QKeySequence& sequence : std::as_const(sequences)) {
        const QString text = sequence.toString(QKeySequence::PortableText);
        if (!text.isEmpty())
            texts.append(text);
    }
    return texts;
}

QString KeyText::portable(const QVariant& keys)
{
    const QStringList texts = all(keys);
    return texts.isEmpty() ? QString() : texts.first();
}
