#include "hotspot.h"

#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString Helper = QStringLiteral("/usr/lib/mobarchy/hotspot");
}

Hotspot::Hotspot(QObject *parent)
    : QObject(parent)
{
    m_available = QFile::exists(Helper);
    m_watch.setInterval(4000);
    connect(&m_watch, &QTimer::timeout, this, &Hotspot::refresh);
    if (m_available)
        refresh();
}

void Hotspot::setWatching(bool on)
{
    if (on == m_watch.isActive())
        return;
    if (on) {
        m_watch.start();
        refresh();
    } else {
        m_watch.stop();
    }
    emit watchingChanged();
}

void Hotspot::run(const QStringList &args)
{
    if (!m_available)
        return;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int code) {
        const QJsonObject o = QJsonDocument::fromJson(p->readAllStandardOutput().trimmed()).object();
        p->deleteLater();
        m_busy = false;
        if (code != 0 || o.contains(QStringLiteral("error")))
            m_error = o.value(QStringLiteral("error")).toString(tr("The hotspot did not answer."));
        else
            m_error.clear();
        if (o.contains(QStringLiteral("on")))
            take(o);
        emit changed();
    });
    p->start(QStringLiteral("sudo"), QStringList{QStringLiteral("-n"), Helper} + args);
}

void Hotspot::take(const QJsonObject &o)
{
    m_on = o.value(QStringLiteral("on")).toBool();
    m_name = o.value(QStringLiteral("name")).toString();
    m_password = o.value(QStringLiteral("password")).toString();
    m_clients = o.value(QStringLiteral("clients")).toInt();
    makeQr();
}

void Hotspot::makeQr()
{
    // (special characters escaped, as the format wants)
    auto esc = [](QString s) {
        for (const QChar c : {u'\\', u';', u',', u':', u'"'})
            s.replace(c, QStringLiteral("\\") + c);
        return s;
    };
    const QString text = QStringLiteral("WIFI:T:WPA;S:%1;P:%2;;").arg(esc(m_name), esc(m_password));
    static QString made;
    if (text == made || m_name.isEmpty())
        return;
    made = text;
    // a new file each time (an Image caches by its source)
    const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                         + QStringLiteral("/mobarchy-hotspot-%1.png").arg(++m_qrSerial);
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, path](int code) {
        p->deleteLater();
        if (code == 0) {
            QFile::remove(m_qr);
            m_qr = path;
            emit changed();
        }
    });
    p->start(QStringLiteral("qrencode"), {QStringLiteral("-o"), path, QStringLiteral("-s"), QStringLiteral("10"),
                                          QStringLiteral("-m"), QStringLiteral("2"), text});
}

void Hotspot::refresh()
{
    if (!m_busy)
        run({QStringLiteral("status")});
}

void Hotspot::setOn(bool on)
{
    m_busy = true;
    emit changed();
    run({on ? QStringLiteral("on") : QStringLiteral("off")});
}

void Hotspot::configure(const QString &name, const QString &password)
{
    m_busy = true;
    emit changed();
    run({QStringLiteral("set"), name, password});
}
