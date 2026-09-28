// Help: the end-user help pages (docs/help/<id>.md, compiled into the executable as
// :/mybench/help/<id>.md) for the help drawer.
//
//     Help.markdown("scope")   // Markdown text ("" if the page does not exist)
//     Help.title("scope")      // first "# " heading, else the id
//     Help.topics              // [{ id, title }] of every page, "app" first
//     Help.show("scope")       // ask the shell to open the help drawer on that page
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class Help : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList topics READ topics CONSTANT)

public:
    static Help* instance();
    static Help* create(QQmlEngine*, QJSEngine*);

    Q_INVOKABLE bool has(const QString& id) const;
    Q_INVOKABLE QString markdown(const QString& id) const;
    // The page as styled rich text for a TextEdit (textFormat: TextEdit.RichText): headings,
    // paragraph spacing, tables and code in the given colors. `style` keys (colors): text,
    // heading, muted, accent (links), border (tables), codeBackground; and bodySize (px).
    Q_INVOKABLE QString html(const QString& id, const QVariantMap& style) const;
    // Same for any Markdown text.
    Q_INVOKABLE static QString renderMarkdown(const QString& markdown, const QVariantMap& style);
    Q_INVOKABLE QString title(const QString& id) const;
    // Topic id of a link inside a help page ("scope.md", "../help/dmm.md#range" -> "scope",
    // "dmm"); "" for external links.
    Q_INVOKABLE QString topicForLink(const QString& link) const;
    // Asks the shell to open the help drawer on `id` (emits showRequested; panels' Help buttons).
    Q_INVOKABLE void show(const QString& id) { emit showRequested(id); }
    QVariantList topics() const;

signals:
    void showRequested(const QString& id);

private:
    explicit Help(QObject* parent = nullptr);
    static QString resourcePath(const QString& id);
};

}  // namespace mybench
