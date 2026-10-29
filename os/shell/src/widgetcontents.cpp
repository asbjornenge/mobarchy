#include "widgetcontents.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

WidgetContents::WidgetContents(QObject *parent)
    : QObject(parent)
    , m_dir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
            + QStringLiteral("/mobarchy/widgets"))
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(100);
    connect(&m_settle, &QTimer::timeout, this, &WidgetContents::load);
    // (each file is written to a temporary one and renamed: a change of the directory)
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    QDir().mkpath(m_dir);
    m_watcher.addPath(m_dir);
    load();
}

void WidgetContents::load()
{
    QVariantMap contents;
    const QDir dir(m_dir);
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile in(dir.filePath(name));
        if (in.open(QIODevice::ReadOnly))
            contents.insert(name.chopped(5), QJsonDocument::fromJson(in.readAll()).object().toVariantMap());
    }
    if (contents == m_contents)
        return;
    m_contents = contents;
    emit changed();
}

QString WidgetContents::markdown(const QString &text, const QFont &font, const QColor &link) const
{
    // (no HTML in it, and no images: the shell would fetch them, and it fetches nothing for a
    // widget, ADR 0046)
    QTextDocument doc;
    doc.setDefaultFont(font);
    doc.setMarkdown(text, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub
                                                         | QTextDocument::MarkdownNoHTML));
    QList<QPair<int, int>> images;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCursor cursor(&doc);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            if (fragment.charFormat().isImageFormat()) {
                images.prepend({fragment.position(), fragment.length()});
            } else if (fragment.charFormat().isAnchor()) {
                QTextCharFormat format;
                format.setForeground(link);
                cursor.mergeCharFormat(format);
            }
        }
    }
    for (const auto &[at, length] : std::as_const(images)) {
        QTextCursor cursor(&doc);
        cursor.setPosition(at);
        cursor.setPosition(at + length, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    return doc.toHtml();
}
