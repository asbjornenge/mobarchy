#include "deviceinfo.h"

namespace deviceinfo {

namespace {

// Everything after a # that is not inside a string.
QString stripComment(const QString &line)
{
    bool inString = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line[i] == u'"' && (i == 0 || line[i - 1] != u'\\'))
            inString = !inString;
        else if (line[i] == u'#' && !inString)
            return line.left(i);
    }
    return line;
}

QVariant value(const QString &raw)
{
    if (raw.size() >= 2 && raw.startsWith(u'"') && raw.endsWith(u'"')) {
        QString s = raw.mid(1, raw.size() - 2);
        return s.replace(QStringLiteral("\\\""), QStringLiteral("\"")).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    if (raw == u"true" || raw == u"false")
        return raw == u"true";
    bool ok;
    const qlonglong i = raw.toLongLong(&ok);
    if (ok)
        return i;
    const double d = raw.toDouble(&ok);
    if (ok)
        return d;
    return raw;  // something this parser does not know (arrays, dates): kept as text
}

}  // namespace

Device parse(const QByteArray &toml)
{
    Device device;
    QVariantMap *current = nullptr;
    QString table;
    for (const QByteArray &rawLine : toml.split('\n')) {
        const QString line = stripComment(QString::fromUtf8(rawLine)).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("[["))) {
            if (line == u"[[display]]") {
                device.displays.append(QVariantMap());
                current = &device.displays.last();
            } else {
                current = nullptr;  // other arrays of tables: not used
            }
            table.clear();
            continue;
        }
        if (line.startsWith(u'[')) {
            table = line.mid(1, line.size() - 2).trimmed();
            current = nullptr;
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QVariant v = value(line.mid(eq + 1).trimmed());
        if (current) {
            current->insert(key, v);
        } else if (!table.isEmpty()) {
            QVariantMap t = device.tables.value(table).toMap();
            t.insert(key, v);
            device.tables.insert(table, t);
        }
    }
    return device;
}

QVariantMap display(const Device &device, const QString &connector)
{
    for (const QVariantMap &d : device.displays) {
        if (d.value(QStringLiteral("connector")).toString() == connector)
            return d;
    }
    if (device.displays.size() == 1 && !device.displays.first().contains(QStringLiteral("connector")))
        return device.displays.first();
    return {};
}

int topInset(const Device &device, const QString &connector)
{
    return display(device, connector).value(QStringLiteral("top_inset")).toInt();
}

int sideInset(const Device &device, const QString &connector)
{
    return display(device, connector).value(QStringLiteral("side_inset")).toInt();
}

int bottomInset(const Device &device, const QString &connector)
{
    return display(device, connector).value(QStringLiteral("bottom_inset")).toInt();
}

QList<qreal> cutout(const Device &device, const QString &connector)
{
    // (an array: this parser keeps it as its text, "[x, y, r]")
    QString text = display(device, connector).value(QStringLiteral("cutout")).toString().trimmed();
    if (!text.startsWith(u'[') || !text.endsWith(u']'))
        return {};
    QList<qreal> out;
    for (const QString &part : text.mid(1, text.size() - 2).split(u',')) {
        bool ok;
        out << part.trimmed().toDouble(&ok);
        if (!ok)
            return {};
    }
    return out.size() == 3 ? out : QList<qreal>();
}

}  // namespace deviceinfo
