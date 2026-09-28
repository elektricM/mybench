#include "mybench/settingsjson.h"

#include <QJsonArray>
#include <QMetaEnum>
#include <QObject>
#include <QMetaSequence>
#include <QVariant>

#include <cmath>

namespace mybench::json {

namespace {

QJsonValue fromVariant(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float: {
        const double v = value.toDouble();
        return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Undefined);
    }
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
        return value.toInt();
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return value.toLongLong();
    case QMetaType::QString:
        return value.toString();
    case QMetaType::QUrl:
        return value.toUrl().toString();
    case QMetaType::QColor:
    case QMetaType::QDateTime:
        return QJsonValue::fromVariant(value);
    default:
        break;
    }
    if (value.typeId() == QMetaType::QVariantMap)
        return QJsonObject::fromVariantMap(value.toMap());
    if (value.canConvert<QMetaSequence::Iterable>()) {
        QJsonArray array;
        const QMetaSequence::Iterable iterable = value.value<QMetaSequence::Iterable>();
        for (auto it = iterable.constBegin(); it != iterable.constEnd(); ++it) {
            const QJsonValue v = fromVariant(QVariant(*it));
            array.append(v.isUndefined() ? QJsonValue() : v);
        }
        return array;
    }
    const QJsonValue v = QJsonValue::fromVariant(value);
    return v.isNull() ? QJsonValue(QJsonValue::Undefined) : v;
}

}  // namespace

QJsonValue readProperty(const QObject* object, const QMetaProperty& property)
{
    const QVariant value = property.read(object);
    if (!value.isValid())
        return QJsonValue(QJsonValue::Undefined);
    if (property.isEnumType()) {
        const QMetaEnum e = property.enumerator();
        const int raw = value.toInt();
        if (e.isFlag())
            return QString::fromLatin1(e.valueToKeys(raw));
        const char* key = e.valueToKey(raw);
        return key ? QJsonValue(QString::fromLatin1(key)) : QJsonValue(raw);
    }
    return fromVariant(value);
}

bool writeProperty(QObject* object, const QMetaProperty& property, const QJsonValue& value)
{
    if (value.isUndefined() || !property.isWritable())
        return false;
    if (property.isEnumType()) {
        const QMetaEnum e = property.enumerator();
        int raw = 0;
        bool ok = false;
        if (value.isString()) {
            const QByteArray key = value.toString().toLatin1();
            raw = e.isFlag() ? e.keysToValue(key.constData(), &ok) : e.keyToValue(key.constData(), &ok);
        } else if (value.isDouble()) {
            raw = value.toInt();
            ok = e.isFlag() || e.valueToKey(raw) != nullptr;
        }
        if (!ok)
            return false;
        QVariant v(raw);
        if (!v.convert(property.metaType()))
            v = QVariant(raw);
        return property.write(object, v);
    }

    const QMetaType type = property.metaType();
    QVariant v;
    if (type.id() == QMetaType::QVariant) {
        v = value.toVariant();
    } else if (value.isArray() && type.id() == QMetaType::fromType<QList<double>>().id()) {
        QList<double> list;
        for (const QJsonValue& element : value.toArray())
            list.append(element.isDouble() ? element.toDouble() : std::nan(""));
        v = QVariant::fromValue(list);
    } else if (value.isArray() && type.id() == QMetaType::fromType<QList<int>>().id()) {
        QList<int> list;
        for (const QJsonValue& element : value.toArray())
            list.append(element.toInt());
        v = QVariant::fromValue(list);
    } else {
        v = value.toVariant();
        if (!v.convert(type))
            return false;
    }
    return property.write(object, v);
}

QJsonObject readProperties(const QObject* object, const QList<QMetaProperty>& properties)
{
    QJsonObject json;
    for (const QMetaProperty& property : properties) {
        const QJsonValue value = readProperty(object, property);
        if (!value.isUndefined())
            json.insert(QString::fromLatin1(property.name()), value);
    }
    return json;
}

int writeProperties(QObject* object, const QList<QMetaProperty>& properties, const QJsonObject& json)
{
    int written = 0;
    for (const QMetaProperty& property : properties) {
        const auto it = json.constFind(QString::fromLatin1(property.name()));
        if (it != json.constEnd() && writeProperty(object, property, *it))
            ++written;
    }
    return written;
}

}  // namespace mybench::json
