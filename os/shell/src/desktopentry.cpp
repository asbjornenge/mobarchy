#include "desktopentry.h"

#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

namespace desktopentry {

namespace {

// The spec's escapes for string values: \s \n \t \r \\ (anything else is kept as is).
QString unescape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value[i];
        if (c != u'\\' || i + 1 == value.size()) {
            out += c;
            continue;
        }
        switch (value[++i].unicode()) {
        case 's': out += u' '; break;
        case 'n': out += u'\n'; break;
        case 't': out += u'\t'; break;
        case 'r': out += u'\r'; break;
        case '\\': out += u'\\'; break;
        default: out += u'\\'; out += value[i]; break;
        }
    }
    return out;
}

QStringList list(const QString &value)
{
    QStringList items = value.split(u';', Qt::SkipEmptyParts);
    for (QString &item : items)
        item = item.trimmed();
    return items;
}

bool isTrue(const QString &value)
{
    return value == u"true";
}

bool canExecute(const QString &program)
{
    if (QFileInfo(program).isAbsolute())
        return QFileInfo(program).isExecutable();
    return !QStandardPaths::findExecutable(program).isEmpty();
}

}  // namespace

QStringList languagesFor(const QString &locale)
{
    // lang_COUNTRY.ENCODING@MODIFIER; the encoding plays no part in matching.
    QString rest = locale;
    QString modifier;
    if (const qsizetype at = rest.indexOf(u'@'); at >= 0) {
        modifier = rest.mid(at + 1);
        rest.truncate(at);
    }
    if (const qsizetype dot = rest.indexOf(u'.'); dot >= 0)
        rest.truncate(dot);
    const qsizetype us = rest.indexOf(u'_');
    const QString lang = us >= 0 ? rest.left(us) : rest;
    const QString country = us >= 0 ? rest.mid(us + 1) : QString();
    if (lang.isEmpty() || lang == u"C" || lang == u"POSIX")
        return {};
    QStringList out;
    if (!country.isEmpty() && !modifier.isEmpty())
        out << lang + u'_' + country + u'@' + modifier;
    if (!country.isEmpty())
        out << lang + u'_' + country;
    if (!modifier.isEmpty())
        out << lang + u'@' + modifier;
    out << lang;
    return out;
}

DesktopEntry parse(const QString &id, const QString &path, const QByteArray &data,
                   const QStringList &languages, const QStringList &desktops)
{
    QHash<QString, QString> keys;
    bool inMain = false;
    for (const QByteArray &rawLine : data.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u'[')) {
            inMain = line == u"[Desktop Entry]";
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (!inMain || eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (!keys.contains(key))  // the first occurrence wins
            keys.insert(key, unescape(line.mid(eq + 1).trimmed()));
    }

    DesktopEntry e;
    e.id = id;
    e.path = path;
    e.name = keys.value(QStringLiteral("Name"));
    for (const QString &lang : languages) {
        if (const auto it = keys.constFind(QStringLiteral("Name[%1]").arg(lang)); it != keys.cend()) {
            e.name = *it;
            break;
        }
    }
    e.icon = keys.value(QStringLiteral("Icon"));
    e.exec = keys.value(QStringLiteral("Exec"));
    e.workingDir = keys.value(QStringLiteral("Path"));
    e.wmClass = keys.value(QStringLiteral("StartupWMClass"));
    e.terminal = isTrue(keys.value(QStringLiteral("Terminal")));

    bool shown = true;
    const QStringList onlyShowIn = list(keys.value(QStringLiteral("OnlyShowIn")));
    const QStringList notShowIn = list(keys.value(QStringLiteral("NotShowIn")));
    if (!onlyShowIn.isEmpty())
        shown = std::any_of(desktops.cbegin(), desktops.cend(),
                            [&](const QString &d) { return onlyShowIn.contains(d); });
    if (std::any_of(desktops.cbegin(), desktops.cend(),
                    [&](const QString &d) { return notShowIn.contains(d); }))
        shown = false;
    const QString tryExec = keys.value(QStringLiteral("TryExec"));

    e.visible = shown && keys.value(QStringLiteral("Type")) == u"Application"
        && !isTrue(keys.value(QStringLiteral("NoDisplay")))
        && !isTrue(keys.value(QStringLiteral("Hidden")))
        && !e.name.isEmpty() && !e.exec.isEmpty()
        && (tryExec.isEmpty() || canExecute(tryExec));
    return e;
}

QStringList execArgs(const DesktopEntry &entry)
{
    // Split on unquoted whitespace. Inside double quotes, \" \` \$ \\ are escapes.
    QStringList words;
    QString word;
    bool inWord = false, quoted = false;
    const QString &s = entry.exec;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (quoted) {
            if (c == u'\\' && i + 1 < s.size() && QStringLiteral("\"`$\\").contains(s[i + 1]))
                word += s[++i];
            else if (c == u'"')
                quoted = false;
            else
                word += c;
        } else if (c == u' ' || c == u'\t' || c == u'\n') {
            if (inWord)
                words << word;
            word.clear();
            inWord = false;
        } else if (c == u'"') {
            quoted = inWord = true;
        } else {
            word += c;
            inWord = true;
        }
    }
    if (quoted)
        return {};
    if (inWord)
        words << word;

    // Field codes. Nothing is opened, so the file and URL codes expand to nothing.
    QStringList args;
    for (const QString &w : std::as_const(words)) {
        if (w == u"%f" || w == u"%F" || w == u"%u" || w == u"%U")
            continue;
        if (w == u"%i") {
            if (!entry.icon.isEmpty())
                args << QStringLiteral("--icon") << entry.icon;
            continue;
        }
        QString arg;
        for (qsizetype i = 0; i < w.size(); ++i) {
            if (w[i] != u'%' || i + 1 == w.size()) {
                arg += w[i];
                continue;
            }
            switch (w[++i].unicode()) {
            case '%': arg += u'%'; break;
            case 'c': arg += entry.name; break;
            case 'k': arg += entry.path; break;
            default: break;  // %f %u %i inside a word, deprecated and unknown codes
            }
        }
        args << arg;
    }
    return args;
}

}  // namespace desktopentry
