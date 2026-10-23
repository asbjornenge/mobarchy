#include "spotify.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
// the browser's sign-in pages, once done: closed (Mobarchy's browser, org.mobarchy.Browser)
void closeSignInTabs()
{
    const QString service = QStringLiteral("org.mobarchy.Browser"), path = QStringLiteral("/org/mobarchy/Browser");
    QDBusReply<QStringList> tabs = QDBusConnection::sessionBus().call(
        QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Tabs")));
    if (!tabs.isValid())
        return;
    for (int i = int(tabs.value().size()) - 1; i >= 0; --i) {
        const QString url = tabs.value().at(i).section(u'\t', 1);
        if (url.startsWith(QLatin1String("https://accounts.spotify.com/")) || url.startsWith(QLatin1String("http://127.0.0.1:8989/"))) {
            QDBusMessage close = QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Close"));
            close << i;
            QDBusConnection::sessionBus().call(close);
        }
    }
}

const QString Program = QStringLiteral("spotify_player");
const QString Config = QStringLiteral("/usr/share/mobarchy/spotify-player");
const QString Service = QStringLiteral("mobarchy-spotify.service");
// where the daemon's sign-in pages come (its xdg-open: /usr/lib/mobarchy/spotify-signin)
QString signInFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/mobarchy-spotify-signin");
}
const QString DeviceName = QStringLiteral("Mobarchy");       // (its config's device.name)
const QString Mpris = QStringLiteral("org.mpris.MediaPlayer2.spotify_player");
const QString MprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString MprisPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/spotify-player");
}

// "spotify:track:abc" or {"id": ...} or "abc": the bare id
QString idOf(const QJsonValue &v)
{
    QString s = v.isObject() ? v.toObject().value(QStringLiteral("id")).toString() : v.toString();
    const qsizetype colon = s.lastIndexOf(u':');
    return colon >= 0 ? s.mid(colon + 1) : s;
}

QString imageOf(const QJsonObject &o)
{
    const QJsonArray images = o.value(QStringLiteral("images")).toArray();
    if (!images.isEmpty())
        return images.first().toObject().value(QStringLiteral("url")).toString();
    return o.value(QStringLiteral("image")).toString();
}

QString artistsOf(const QJsonObject &o)
{
    QStringList names;
    for (const QJsonValue &a : o.value(QStringLiteral("artists")).toArray())
        names << a.toObject().value(QStringLiteral("name")).toString();
    return names.join(QStringLiteral(", "));
}

// a duration as Spotify's ms, or Rust's {secs, nanos}
qint64 msOf(const QJsonObject &o)
{
    if (o.contains(QStringLiteral("duration_ms")))
        return qint64(o.value(QStringLiteral("duration_ms")).toDouble());
    const QJsonValue d = o.value(QStringLiteral("duration"));
    if (d.isObject())
        return qint64(d.toObject().value(QStringLiteral("secs")).toDouble() * 1000
                      + d.toObject().value(QStringLiteral("nanos")).toDouble() / 1e6);
    return qint64(d.toDouble());
}

QVariantMap track(QJsonObject o)
{
    if (o.contains(QStringLiteral("track")) && o.value(QStringLiteral("track")).isObject())
        o = o.value(QStringLiteral("track")).toObject();   // a saved track: {added_at, track}
    const QJsonObject album = o.value(QStringLiteral("album")).toObject();
    return {{QStringLiteral("id"), idOf(o.value(QStringLiteral("id")))},
            {QStringLiteral("name"), o.value(QStringLiteral("name")).toString()},
            {QStringLiteral("artists"), artistsOf(o)},
            {QStringLiteral("album"), album.value(QStringLiteral("name")).toString()},
            {QStringLiteral("albumId"), idOf(album.value(QStringLiteral("id")))},
            {QStringLiteral("image"), imageOf(album).isEmpty() ? imageOf(o) : imageOf(album)},
            {QStringLiteral("durationMs"), msOf(o)}};
}

QVariantMap album(QJsonObject o)
{
    if (o.contains(QStringLiteral("album")) && o.value(QStringLiteral("album")).isObject())
        o = o.value(QStringLiteral("album")).toObject();   // a saved album: {added_at, album}
    return {{QStringLiteral("id"), idOf(o.value(QStringLiteral("id")))},
            {QStringLiteral("name"), o.value(QStringLiteral("name")).toString()},
            {QStringLiteral("artists"), artistsOf(o)},
            {QStringLiteral("image"), imageOf(o)}};
}

QVariantMap playlist(const QJsonObject &o)
{
    // spotify-player's own: owner as [name, id]; the Web API's: owner {display_name}
    const QJsonValue owner = o.value(QStringLiteral("owner"));
    const QString ownerName = owner.isArray() ? owner.toArray().first().toString()
                            : owner.toObject().value(QStringLiteral("display_name")).toString();
    return {{QStringLiteral("id"), idOf(o.value(QStringLiteral("id")))},
            {QStringLiteral("name"), o.value(QStringLiteral("name")).toString()},
            {QStringLiteral("owner"), ownerName},
            {QStringLiteral("image"), imageOf(o)}};
}

// a list in any of the shapes: [..], {items: [..]}
QJsonArray listOf(const QJsonValue &v)
{
    if (v.isArray())
        return v.toArray();
    return v.toObject().value(QStringLiteral("items")).toArray();
}

template <typename F>
QVariantList mapped(const QJsonArray &a, F f)
{
    QVariantList out;
    for (const QJsonValue &v : a)
        if (v.isObject())
            out << f(v.toObject());
    return out;
}

// the first array named `key` anywhere inside (a context: {"Playlist": {playlist, tracks}})
QJsonArray findArray(const QJsonValue &v, const QString &key)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.value(key).isArray())
            return o.value(key).toArray();
        for (const QJsonValue &c : o) {
            const QJsonArray found = findArray(c, key);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}
}  // namespace

Spotify::Spotify(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &Spotify::refresh);
    m_signInPoll.setInterval(1000);
    connect(&m_signInPoll, &QTimer::timeout, this, [this] {
        // a page the daemon wants opened: the one before it goes first (each is the next
        // approval; an old one approved again would answer the wrong callback)
        QFile f(signInFile());
        if (f.open(QIODevice::ReadOnly)) {
            const QStringList urls = QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
            if (urls.size() > m_urlsSeen) {
                m_urlsSeen = int(urls.size());
                closeSignInTabs();
                emit signInUrl(urls.last());
            }
        }
        if (QFile::exists(cacheDir() + QStringLiteral("/credentials.json"))) {
            m_signInPoll.stop();
            QFile::remove(signInFile());
            closeSignInTabs();
            setState(QStringLiteral("ready"));
            emit signedInBack();
        } else if (++m_signInWait > 600) {     // ten minutes
            m_signInPoll.stop();
            setError(tr("Signing in didn't finish"));
            setState(QStringLiteral("signedOut"));
        }
    });
    checkSignedIn();
}

void Spotify::setWatching(bool watching)
{
    if (watching == m_watching)
        return;
    m_watching = watching;
    emit watchingChanged();
    if (watching && m_state == u"ready") {
        refresh();
        m_poll.start();
    } else {
        m_poll.stop();
    }
}

void Spotify::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
    if (state == u"ready") {
        loadLibrary();
        if (m_watching) {
            refresh();
            m_poll.start();
        }
    }
}

void Spotify::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Spotify::checkSignedIn()
{
    // spotify_player keeps the Web API's token and librespot's credentials in its cache
    const QDir cache(cacheDir());
    const bool signedIn = cache.exists(QStringLiteral("credentials.json"));
    if (!signedIn) {
        // (the daemon waiting for an approval already: go on with it)
        if (QFile::exists(signInFile())) {
            setState(QStringLiteral("signingIn"));
            m_urlsSeen = 0;
            m_signInWait = 0;
            m_signInPoll.start();
        } else {
            setState(QStringLiteral("signedOut"));
        }
        return;
    }
    // the daemon (it plays on and is a Connect device while the app is closed)
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("start"), Service});
    setState(QStringLiteral("ready"));
}

void Spotify::run(const QStringList &args, std::function<void(bool, const QByteArray &)> done)
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, args, done](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        const QByteArray out = p->readAllStandardOutput();
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok) {
            const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
            qWarning() << "spotify_player" << args << "failed:" << err.left(300);
            setError(err.section(u'\n', -1).left(200));
        } else if (!m_error.isEmpty()) {
            setError({});
        }
        if (done)
            done(ok, out);
    });
    p->start(Program, QStringList{QStringLiteral("-c"), Config} + args);
}

void Spotify::control(const QStringList &args)
{
    run(QStringList{QStringLiteral("playback")} + args, [this](bool, const QByteArray &) {
        QTimer::singleShot(250, this, &Spotify::refresh);
    });
}

// Something new to play: on this phone (the Connect device "Mobarchy"), not on whichever
// device Spotify had active (another phone, a speaker), then the playback
void Spotify::startHere(const QStringList &args)
{
    run({QStringLiteral("connect"), QStringLiteral("--name"), DeviceName}, [this, args](bool, const QByteArray &) {
        control(args);
    });
}

void Spotify::refresh()
{
    // What plays, from the daemon's MPRIS (local: no Web API request, which a poll would
    // spend on Spotify's rate limit)
    if (m_refreshing)
        return;
    m_refreshing = true;
    QDBusMessage msg = QDBusMessage::createMethodCall(Mpris, MprisPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    msg << MprisPlayer;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_refreshing = false;
        QDBusPendingReply<QVariantMap> reply = *w;
        QVariantMap p;
        if (!reply.isError()) {
            const QVariantMap props = reply.value();
            const QVariantMap meta = qdbus_cast<QVariantMap>(props.value(QStringLiteral("Metadata")));
            const QString title = meta.value(QStringLiteral("xesam:title")).toString();
            if (!title.isEmpty()) {
                const QString trackPath = meta.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path();
                p = {{QStringLiteral("title"), title},
                     {QStringLiteral("artists"), meta.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", "))},
                     {QStringLiteral("album"), meta.value(QStringLiteral("xesam:album")).toString()},
                     {QStringLiteral("image"), meta.value(QStringLiteral("mpris:artUrl")).toString()},
                     // (no ids in its MPRIS: the track by its title, artists and length)
                     {QStringLiteral("trackId"), title + u'|' + meta.value(QStringLiteral("mpris:length")).toString()},
                     {QStringLiteral("trackPath"), trackPath},
                     {QStringLiteral("durationMs"), meta.value(QStringLiteral("mpris:length")).toLongLong() / 1000},
                     {QStringLiteral("progressMs"), props.value(QStringLiteral("Position")).toLongLong() / 1000},
                     {QStringLiteral("playing"), props.value(QStringLiteral("PlaybackStatus")).toString() == u"Playing"},
                     {QStringLiteral("shuffle"), m_shuffle},
                     {QStringLiteral("repeat"), m_repeat}};
            }
        }
        if (p != m_playback) {
            m_playback = p;
            emit playbackChanged();
        }
    });
}

void Spotify::signIn()
{
    // The daemon signs in itself when it has no credentials: Spotify's pages (its Web API
    // client's, ncspot's fallback's, then the streaming's), its own callback taking each
    // answer, and once connected it keeps the credentials. (spotify_player's `authenticate`
    // doesn't: it drops the streaming's.) Here: start it, open its pages, wait for them.
    setState(QStringLiteral("signingIn"));
    QFile::remove(signInFile());
    m_urlsSeen = 0;
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"), Service});
    m_signInWait = 0;
    m_signInPoll.start();
}

void Spotify::loadLibrary()
{
    run({QStringLiteral("get"), QStringLiteral("key"), QStringLiteral("user-playlists")}, [this](bool ok, const QByteArray &out) {
        if (!ok)
            return;
        m_playlists = mapped(listOf(QJsonDocument::fromJson(out).isArray() ? QJsonValue(QJsonDocument::fromJson(out).array())
                                                                           : QJsonValue(QJsonDocument::fromJson(out).object())),
                             playlist);
        emit libraryChanged();
    });
    run({QStringLiteral("get"), QStringLiteral("key"), QStringLiteral("user-liked-tracks")}, [this](bool ok, const QByteArray &out) {
        if (!ok)
            return;
        const QJsonDocument d = QJsonDocument::fromJson(out);
        m_liked = mapped(listOf(d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object())), track);
        emit libraryChanged();
    });
    run({QStringLiteral("get"), QStringLiteral("key"), QStringLiteral("user-saved-albums")}, [this](bool ok, const QByteArray &out) {
        if (!ok)
            return;
        const QJsonDocument d = QJsonDocument::fromJson(out);
        m_albums = mapped(listOf(d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object())), album);
        emit libraryChanged();
    });
}

void Spotify::search(const QString &query)
{
    if (query.trimmed().isEmpty()) {
        m_results.clear();
        emit resultsChanged();
        return;
    }
    run({QStringLiteral("search"), query}, [this](bool ok, const QByteArray &out) {
        if (!ok)
            return;
        const QJsonObject o = QJsonDocument::fromJson(out).object();
        m_results = {{QStringLiteral("tracks"), mapped(listOf(o.value(QStringLiteral("tracks"))), track)},
                     {QStringLiteral("albums"), mapped(listOf(o.value(QStringLiteral("albums"))), album)},
                     {QStringLiteral("playlists"), mapped(listOf(o.value(QStringLiteral("playlists"))), playlist)},
                     {QStringLiteral("artists"), mapped(listOf(o.value(QStringLiteral("artists"))), [](const QJsonObject &a) {
                          return QVariantMap{{QStringLiteral("id"), idOf(a.value(QStringLiteral("id")))},
                                             {QStringLiteral("name"), a.value(QStringLiteral("name")).toString()},
                                             {QStringLiteral("image"), imageOf(a)}};
                      })}};
        emit resultsChanged();
    });
}

void Spotify::open(const QString &type, const QString &id, const QString &name)
{
    m_opened = {{QStringLiteral("type"), type}, {QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                {QStringLiteral("tracks"), QVariantList()}, {QStringLiteral("loading"), true}};
    emit openedChanged();
    run({QStringLiteral("get"), QStringLiteral("item"), type, QStringLiteral("--id"), id},
        [this, type, id](bool ok, const QByteArray &out) {
            if (m_opened.value(QStringLiteral("id")) != id)
                return;
            m_opened[QStringLiteral("loading")] = false;
            if (ok) {
                const QJsonValue v = QJsonDocument::fromJson(out).isArray()
                                   ? QJsonValue(QJsonDocument::fromJson(out).array())
                                   : QJsonValue(QJsonDocument::fromJson(out).object());
                QVariantList tracks = mapped(v.isArray() ? v.toArray() : findArray(v, QStringLiteral("tracks")), track);
                m_opened[QStringLiteral("tracks")] = tracks;
            }
            emit openedChanged();
        });
}

void Spotify::playContext(const QString &type, const QString &id, bool shuffle)
{
    QStringList args{QStringLiteral("start"), QStringLiteral("context"), type, QStringLiteral("--id"), id};
    if (shuffle)
        args << QStringLiteral("--shuffle");
    startHere(args);
}

void Spotify::playTrack(const QString &id)
{
    startHere({QStringLiteral("start"), QStringLiteral("track"), QStringLiteral("--id"), id});
}

void Spotify::playLiked(bool shuffle)
{
    QStringList args{QStringLiteral("start"), QStringLiteral("liked")};
    if (shuffle)
        args << QStringLiteral("--random");
    startHere(args);
}

void Spotify::playPause()
{
    // at once on the screen; the daemon's answer follows
    if (!m_playback.isEmpty()) {
        m_playback[QStringLiteral("playing")] = !m_playback.value(QStringLiteral("playing")).toBool();
        emit playbackChanged();
    }
    mpris(QStringLiteral("PlayPause"));
}

void Spotify::next() { mpris(QStringLiteral("Next")); }
void Spotify::previous() { mpris(QStringLiteral("Previous")); }

void Spotify::toggleShuffle()
{
    m_shuffle = !m_shuffle;
    control({QStringLiteral("shuffle")});
}

void Spotify::cycleRepeat()
{
    // (spotify-player's cycle: off, track, context)
    m_repeat = m_repeat == u"off" ? QStringLiteral("track") : m_repeat == u"track" ? QStringLiteral("context") : QStringLiteral("off");
    control({QStringLiteral("repeat")});
}

void Spotify::mpris(const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Mpris, MprisPath, MprisPlayer, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QTimer::singleShot(200, this, &Spotify::refresh);
    });
}

void Spotify::signOut()
{
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("stop"), Service});
    QDir cache(cacheDir());
    for (const QString &f : cache.entryList({QStringLiteral("credentials.json"), QStringLiteral("*_token.json"),
                                             QStringLiteral("*_cache.json")}, QDir::Files))
        cache.remove(f);
    m_playback.clear();
    m_playlists.clear();
    m_liked.clear();
    m_albums.clear();
    emit playbackChanged();
    emit libraryChanged();
    m_poll.stop();
    setState(QStringLiteral("signedOut"));
}

void Spotify::seekTo(qint64 ms)
{
    // (spotify-player's MPRIS has no track ids to SetPosition with: a relative Seek, µs)
    if (m_playback.isEmpty())
        return;
    const qint64 at = qint64(m_playback.value(QStringLiteral("progressMs")).toDouble());
    mpris(QStringLiteral("Seek"), {qlonglong(ms - at) * 1000});
}

void Spotify::like(bool like)
{
    QStringList args{QStringLiteral("like")};
    if (!like)
        args << QStringLiteral("--unlike");
    run(args);
}
