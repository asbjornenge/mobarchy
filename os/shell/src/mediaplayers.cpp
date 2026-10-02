#include "mediaplayers.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

#include <algorithm>

namespace {

const QString Prefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString Path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString RootIface = QStringLiteral("org.mpris.MediaPlayer2");
const QString PlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString PropertiesIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString DBusService = QStringLiteral("org.freedesktop.DBus");

QDBusConnection bus() { return QDBusConnection::sessionBus(); }

// Values in signals arrive as QDBusArgument; in replies, already unpacked.
QVariantMap toMap(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    return v.toMap();
}

}  // namespace

MediaPlayers::MediaPlayers(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &MediaPlayers::positionChanged);

    bus().connect(DBusService, QStringLiteral("/org/freedesktop/DBus"), DBusService,
                  QStringLiteral("NameOwnerChanged"), this,
                  SLOT(nameOwnerChanged(QString, QString, QString)));
    // Every player's signals: told apart by their sender.
    bus().connect(QString(), Path, PropertiesIface, QStringLiteral("PropertiesChanged"), this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    bus().connect(QString(), Path, PlayerIface, QStringLiteral("Seeked"), this,
                  SLOT(seeked(qlonglong, QDBusMessage)));

    auto list = QDBusMessage::createMethodCall(DBusService, QStringLiteral("/org/freedesktop/DBus"),
                                               DBusService, QStringLiteral("ListNames"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(list), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QStringList> names = *w;
        for (const QString &name : names.value()) {
            if (!name.startsWith(Prefix))
                continue;
            auto owner = QDBusMessage::createMethodCall(DBusService, QStringLiteral("/org/freedesktop/DBus"),
                                                        DBusService, QStringLiteral("GetNameOwner"));
            owner << name;
            auto *o = new QDBusPendingCallWatcher(bus().asyncCall(owner), this);
            connect(o, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *o) {
                o->deleteLater();
                QDBusPendingReply<QString> unique = *o;
                if (!unique.isError())
                    add(name, unique.value());
            });
        }
    });
}

void MediaPlayers::nameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    if (!name.startsWith(Prefix))
        return;
    if (!oldOwner.isEmpty() && m_players.remove(oldOwner))
        choose();
    if (!newOwner.isEmpty())
        add(name, newOwner);
}

void MediaPlayers::add(const QString &service, const QString &owner)
{
    Player &p = m_players[owner];
    p.service = service;
    for (const QString &iface : {RootIface, PlayerIface}) {
        auto msg = QDBusMessage::createMethodCall(service, Path, PropertiesIface, QStringLiteral("GetAll"));
        msg << iface;
        auto *w = new QDBusPendingCallWatcher(bus().asyncCall(msg), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, owner, iface](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            QDBusPendingReply<QVariantMap> props = *w;
            auto it = m_players.find(owner);
            if (props.isError() || it == m_players.end())
                return;
            apply(*it, iface, props.value());
            choose();
        });
    }
}

void MediaPlayers::apply(Player &p, const QString &interface, const QVariantMap &props)
{
    if (interface == RootIface) {
        if (props.contains(QStringLiteral("Identity")))
            p.identity = props.value(QStringLiteral("Identity")).toString();
        if (props.contains(QStringLiteral("DesktopEntry")))
            p.desktopEntry = props.value(QStringLiteral("DesktopEntry")).toString();
        return;
    }
    if (interface != PlayerIface)
        return;
    if (props.contains(QStringLiteral("PlaybackStatus"))) {
        const QString status = props.value(QStringLiteral("PlaybackStatus")).toString();
        if (status == QLatin1String("Playing") && p.status != status)
            p.lastActive = m_clock.elapsed();
        if (p.status != status) {
            // Where it stopped or starts: the estimate's base must be right.
            p.position = p.status == QLatin1String("Playing")
                ? p.position + (m_clock.elapsed() - p.positionAt) * 1000 : p.position;
            p.positionAt = m_clock.elapsed();
        }
        p.status = status;
    }
    if (props.contains(QStringLiteral("Metadata"))) {
        const QVariantMap metadata = toMap(props.value(QStringLiteral("Metadata")));
        if (metadata.value(QStringLiteral("mpris:trackid")) != p.metadata.value(QStringLiteral("mpris:trackid"))
            || metadata.value(QStringLiteral("xesam:title")) != p.metadata.value(QStringLiteral("xesam:title"))) {
            p.position = 0;
            p.positionAt = m_clock.elapsed();
        }
        p.metadata = metadata;
    }
    if (props.contains(QStringLiteral("Position"))) {
        p.position = props.value(QStringLiteral("Position")).toLongLong();
        p.positionAt = m_clock.elapsed();
    }
    const auto flag = [&props](const char *name, bool &field) {
        const QString key = QString::fromLatin1(name);
        if (props.contains(key))
            field = props.value(key).toBool();
    };
    flag("CanGoNext", p.canNext);
    flag("CanGoPrevious", p.canPrevious);
    flag("CanPlay", p.canPlay);
    flag("CanPause", p.canPause);
    flag("CanSeek", p.canSeek);
}

void MediaPlayers::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                     const QStringList &, const QDBusMessage &message)
{
    Player *p = bySender(message.service());
    if (!p)
        return;
    apply(*p, interface, changed);
    choose();
}

void MediaPlayers::seeked(qlonglong position, const QDBusMessage &message)
{
    Player *p = bySender(message.service());
    if (!p)
        return;
    p->position = position;
    p->positionAt = m_clock.elapsed();
    emit positionChanged();
}

MediaPlayers::Player *MediaPlayers::bySender(const QString &sender)
{
    auto it = m_players.find(sender);
    return it == m_players.end() ? nullptr : &*it;
}

void MediaPlayers::choose()
{
    // A player playing beats one that isn't; then the one started (or used) last.
    QString best;
    for (auto it = m_players.cbegin(); it != m_players.cend(); ++it) {
        if (it->status.isEmpty())
            continue;  // not loaded yet
        if (best.isEmpty()) {
            best = it.key();
            continue;
        }
        const Player &b = *m_players.constFind(best);
        const bool playing = it->status == QLatin1String("Playing");
        const bool bestPlaying = b.status == QLatin1String("Playing");
        if (playing != bestPlaying ? playing : it->lastActive > b.lastActive)
            best = it.key();
    }
    m_active = best;
    if (playing())
        m_tick.start();
    else
        m_tick.stop();
    emit changed();
    emit positionChanged();
}

const MediaPlayers::Player &MediaPlayers::active() const
{
    static const Player none;
    auto it = m_players.constFind(m_active);
    return it == m_players.cend() ? none : *it;
}

QString MediaPlayers::title() const
{
    return active().metadata.value(QStringLiteral("xesam:title")).toString();
}

QString MediaPlayers::artist() const
{
    return active().metadata.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", "));
}

QString MediaPlayers::album() const
{
    return active().metadata.value(QStringLiteral("xesam:album")).toString();
}

QString MediaPlayers::artUrl() const
{
    return active().metadata.value(QStringLiteral("mpris:artUrl")).toString();
}

qint64 MediaPlayers::length() const
{
    return active().metadata.value(QStringLiteral("mpris:length")).toLongLong() / 1000;
}

qint64 MediaPlayers::position() const
{
    const Player &p = active();
    qint64 us = p.position;
    if (p.status == QLatin1String("Playing"))
        us += (m_clock.elapsed() - p.positionAt) * 1000;
    const qint64 ms = us / 1000;
    return length() > 0 ? std::clamp<qint64>(ms, 0, length()) : std::max<qint64>(ms, 0);
}

void MediaPlayers::call(const QString &method, const QVariantList &args)
{
    auto it = m_players.find(m_active);
    if (it == m_players.end())
        return;
    it->lastActive = m_clock.elapsed();  // the one being controlled stays in charge
    auto msg = QDBusMessage::createMethodCall(it->service, Path, PlayerIface, method);
    msg.setArguments(args);
    bus().asyncCall(msg);
}

void MediaPlayers::playPause() { call(QStringLiteral("PlayPause")); }
void MediaPlayers::play() { call(QStringLiteral("Play")); }
void MediaPlayers::pause() { call(QStringLiteral("Pause")); }
void MediaPlayers::next() { call(QStringLiteral("Next")); }
void MediaPlayers::previous() { call(QStringLiteral("Previous")); }

void MediaPlayers::seek(qint64 ms)
{
    const QVariant track = active().metadata.value(QStringLiteral("mpris:trackid"));
    if (!track.canConvert<QDBusObjectPath>())
        return;
    call(QStringLiteral("SetPosition"), {track, QVariant::fromValue(qlonglong(ms * 1000))});
}

void MediaPlayers::refreshPosition()
{
    auto it = m_players.find(m_active);
    if (it == m_players.end())
        return;
    auto msg = QDBusMessage::createMethodCall(it->service, Path, PropertiesIface, QStringLiteral("Get"));
    msg << PlayerIface << QStringLiteral("Position");
    const QString owner = m_active;
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, owner](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> v = *w;
        auto it = m_players.find(owner);
        if (v.isError() || it == m_players.end())
            return;
        it->position = v.value().variant().toLongLong();
        it->positionAt = m_clock.elapsed();
        emit positionChanged();
    });
}
