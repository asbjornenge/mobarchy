#include "contacts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString digits(const QString &s)
{
    QString d;
    for (const QChar c : s)
        if (c.isDigit())
            d += c;
    return d;
}

QString unescape(QString v)
{
    v.replace(QStringLiteral("\\n"), QStringLiteral("\n")).replace(QStringLiteral("\\N"), QStringLiteral("\n"));
    static const QRegularExpression esc(QStringLiteral("\\\\(.)"));
    return v.replace(esc, QStringLiteral("\\1"));
}

}  // namespace

Contacts::Contacts(QObject *parent)
    : QObject(parent)
{
    // follow the file, and its directory (it is replaced by a rename)
    QDir().mkpath(QFileInfo(path()).absolutePath());
    m_watcher.addPath(QFileInfo(path()).absolutePath());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Contacts::load);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Contacts::load);
    load();
}

QString Contacts::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/mobarchy/contacts.vcf");
}

bool Contacts::sameNumber(const QString &a, const QString &b)
{
    const QString da = digits(a), db = digits(b);
    return da.size() >= 6 && db.size() >= 6 && da.right(8) == db.right(8);
}

void Contacts::load()
{
    QFile f(path());
    QVariantList all;
    if (f.open(QIODevice::ReadOnly)) {
        if (!m_watcher.files().contains(path()))
            m_watcher.addPath(path());
        // unfold (a line starting with a space or tab continues the one before)
        QStringList lines;
        for (QString raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            raw.remove(QLatin1Char('\r'));
            if ((raw.startsWith(QLatin1Char(' ')) || raw.startsWith(QLatin1Char('\t'))) && !lines.isEmpty())
                lines.last() += raw.mid(1);
            else if (!raw.isEmpty())
                lines << raw;
        }
        QVariantMap card;
        QVariantList phones;
        QStringList relations;
        QString fn, n;
        for (const QString &line : std::as_const(lines)) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QStringList head = line.left(colon).split(QLatin1Char(';'));
            const QString prop = head.first().section(QLatin1Char('.'), -1).toUpper();
            const QString value = line.mid(colon + 1);
            if (prop == QLatin1String("BEGIN")) {
                card.clear(); phones.clear(); relations.clear(); fn.clear(); n.clear();
            } else if (prop == QLatin1String("FN")) {
                fn = unescape(value).trimmed();
            } else if (prop == QLatin1String("N")) {
                const QStringList p = value.split(QLatin1Char(';'));  // family;given;middle
                n = QStringList{p.value(1), p.value(2), p.value(0)}.join(QLatin1Char(' ')).simplified();
            } else if (prop == QLatin1String("TEL")) {
                QString type;
                for (const QString &param : head.mid(1)) {
                    const QString v = param.contains(QLatin1Char('=')) ? param.section(QLatin1Char('='), 1) : param;
                    for (const QString &t : v.split(QLatin1Char(',')))
                        if (type.isEmpty() && !t.isEmpty()
                            && t.compare(QLatin1String("voice"), Qt::CaseInsensitive) != 0
                            && t.compare(QLatin1String("pref"), Qt::CaseInsensitive) != 0)
                            type = t.toLower();
                }
                phones << QVariantMap{{QStringLiteral("number"), unescape(value).remove(QStringLiteral("tel:")).trimmed()},
                                      {QStringLiteral("type"), type}};
            } else if (prop == QLatin1String("X-MOBARCHY-RELATION")) {
                relations << unescape(value).trimmed();
            } else if (prop == QLatin1String("END")) {
                const QString name = !fn.isEmpty() ? fn : n;
                if (!name.isEmpty())
                    all << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("phones"), phones},
                                       {QStringLiteral("relations"), relations}};
            }
        }
    }
    std::sort(all.begin(), all.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_all = all;
    emit changed();
}

QVariantList Contacts::filter(const QString &query) const
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return m_all;
    const QString qd = digits(q);
    QVariantList out;
    for (const QVariant &v : m_all) {
        const QVariantMap c = v.toMap();
        bool hit = c.value(QStringLiteral("name")).toString().toLower().contains(q);
        for (const QString &r : c.value(QStringLiteral("relations")).toStringList())
            hit = hit || r.toLower().startsWith(q);
        if (!hit && qd.size() >= 3)
            for (const QVariant &p : c.value(QStringLiteral("phones")).toList())
                hit = hit || digits(p.toMap().value(QStringLiteral("number")).toString()).contains(qd);
        if (hit)
            out << c;
    }
    return out;
}

QString Contacts::nameFor(const QString &number) const
{
    for (const QVariant &v : m_all) {
        const QVariantMap c = v.toMap();
        for (const QVariant &p : c.value(QStringLiteral("phones")).toList())
            if (sameNumber(number, p.toMap().value(QStringLiteral("number")).toString()))
                return c.value(QStringLiteral("name")).toString();
    }
    return {};
}
