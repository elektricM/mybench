// JSON conversion of QObject properties (instrument settings, sessions).
//
// Enums and flags are stored by key name ("Sine", "Rising|Falling") so that settings survive
// reordering of enum values; numbers, bools, strings, lists and maps map to JSON directly;
// non-finite doubles are skipped (JSON has no NaN). Unknown or invalid values are ignored on
// restore, so an old settings file never breaks an instrument.
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaProperty>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace mybench::json {

// JSON form of `property` of `object`; undefined QJsonValue if it cannot be represented.
QJsonValue readProperty(const QObject* object, const QMetaProperty& property);
// Writes a JSON value to `property`; returns false if it could not be converted.
bool writeProperty(QObject* object, const QMetaProperty& property, const QJsonValue& value);

// Every property in `properties` -> { name: value }.
QJsonObject readProperties(const QObject* object, const QList<QMetaProperty>& properties);
// Writes the members of `json` whose name matches one of `properties`. Returns how many.
int writeProperties(QObject* object, const QList<QMetaProperty>& properties, const QJsonObject& json);

}  // namespace mybench::json
