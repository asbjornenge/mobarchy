#include "contacts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
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
    const QString data = QFileInfo(path()).absolutePath();
    QDir().mkpath(data + QStringLiteral("/contacts"));
    m_watcher.addPath(data);
    m_watcher.addPath(data + QStringLiteral("/contacts"));
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

// the cards of one vCard file: [{name, phones}]
static QVariantList parseFile(const QString &file)
{
    QFile f(file);
    QVariantList out;
    if (!f.open(QIODevice::ReadOnly))
        return out;
    // unfold (a line starting with a space or tab continues the one before)
    QStringList lines;
    for (QString raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        raw.remove(QLatin1Char('\r'));
        if ((raw.startsWith(QLatin1Char(' ')) || raw.startsWith(QLatin1Char('\t'))) && !lines.isEmpty())
            lines.last() += raw.mid(1);
        else if (!raw.isEmpty())
            lines << raw;
    }
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
            phones.clear(); relations.clear(); fn.clear(); n.clear();
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
                out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("phones"), phones},
                                   {QStringLiteral("relations"), relations}};
        }
    }
    return out;
}

void Contacts::load()
{
    // the owner's own file, then every account's: one person per name, the numbers of all
    const QString data = QFileInfo(path()).absolutePath();
    QStringList files{path()};
    for (const QString &f : QDir(data + QStringLiteral("/contacts")).entryList({QStringLiteral("*.vcf")}, QDir::Files, QDir::Name))
        files << data + QStringLiteral("/contacts/") + f;
    if (QFileInfo::exists(path()) && !m_watcher.files().contains(path()))
        m_watcher.addPath(path());

    QList<QVariantMap> people;
    QHash<QString, int> byName;
    for (const QString &file : std::as_const(files)) {
        for (const QVariant &v : parseFile(file)) {
            QVariantMap c = v.toMap();
            const QString key = c.value(QStringLiteral("name")).toString().toLower();
            const auto it = byName.constFind(key);
            if (it == byName.constEnd()) {
                byName.insert(key, int(people.size()));
                people << c;
                continue;
            }
            QVariantMap &have = people[*it];
            QVariantList phones = have.value(QStringLiteral("phones")).toList();
            for (const QVariant &p : c.value(QStringLiteral("phones")).toList()) {
                const QString num = p.toMap().value(QStringLiteral("number")).toString();
                if (std::none_of(phones.cbegin(), phones.cend(), [&](const QVariant &q) {
                        return sameNumber(num, q.toMap().value(QStringLiteral("number")).toString()); }))
                    phones << p;
            }
            have[QStringLiteral("phones")] = phones;
        }
    }
    // the owner's words for people (relations.json: {"far": "Per Olsen"})
    QFile rel(data + QStringLiteral("/relations.json"));
    if (rel.open(QIODevice::ReadOnly)) {
        const QJsonObject words = QJsonDocument::fromJson(rel.readAll()).object();
        for (auto it = words.begin(); it != words.end(); ++it) {
            const auto who = byName.constFind(it.value().toString().toLower());
            if (who == byName.constEnd())
                continue;
            QStringList r = people[*who].value(QStringLiteral("relations")).toStringList();
            if (!r.contains(it.key()))
                r << it.key();
            people[*who][QStringLiteral("relations")] = r;
        }
    }
    QVariantList all;
    for (const QVariantMap &c : std::as_const(people))
        all << c;
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
