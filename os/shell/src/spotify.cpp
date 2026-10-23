#include "spotify.h"

#include <QDBusConnection>
#include <QDBusMessage>
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
        if (QFile::exists(cacheDir() + QStringLiteral("/credentials.json"))) {
            m_signInPoll.stop();
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
        setState(QStringLiteral("signedOut"));
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

void Spotify::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    run({QStringLiteral("get"), QStringLiteral("key"), QStringLiteral("playback")}, [this](bool ok, const QByteArray &out) {
        m_refreshing = false;
        if (!ok)
            return;
        const QJsonObject o = QJsonDocument::fromJson(out).object();
        QVariantMap p;
        const QJsonObject item = o.value(QStringLiteral("item")).toObject();
        if (!item.isEmpty()) {
            const QVariantMap t = track(item);
            p = {{QStringLiteral("title"), t.value(QStringLiteral("name"))},
                 {QStringLiteral("artists"), t.value(QStringLiteral("artists"))},
                 {QStringLiteral("album"), t.value(QStringLiteral("album"))},
                 {QStringLiteral("image"), t.value(QStringLiteral("image"))},
                 {QStringLiteral("trackId"), t.value(QStringLiteral("id"))},
                 {QStringLiteral("durationMs"), t.value(QStringLiteral("durationMs"))},
                 {QStringLiteral("progressMs"), o.value(QStringLiteral("progress_ms")).toDouble()},
                 {QStringLiteral("playing"), o.value(QStringLiteral("is_playing")).toBool()},
                 {QStringLiteral("shuffle"), o.value(QStringLiteral("shuffle_state")).toBool()},
                 {QStringLiteral("repeat"), o.value(QStringLiteral("repeat_state")).toString()},
                 {QStringLiteral("contextUri"), o.value(QStringLiteral("context")).toObject().value(QStringLiteral("uri")).toString()},
                 {QStringLiteral("device"), o.value(QStringLiteral("device")).toObject().value(QStringLiteral("name")).toString()}};
        }
        if (p != m_playback) {
            m_playback = p;
            emit playbackChanged();
        }
    });
}

void Spotify::signIn()
{
    // The daemon signs in itself when it has no credentials: Spotify's pages open in the
    // browser (the Web API's, then the streaming's), its own callback takes each answer,
    // and once connected it keeps the credentials. (spotify_player's `authenticate`
    // doesn't: it drops the streaming's.) Here: start it, and wait for them.
    setState(QStringLiteral("signingIn"));
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
    control(args);
}

void Spotify::playTrack(const QString &id)
{
    control({QStringLiteral("start"), QStringLiteral("track"), QStringLiteral("--id"), id});
}

void Spotify::playLiked(bool shuffle)
{
    QStringList args{QStringLiteral("start"), QStringLiteral("liked")};
    if (shuffle)
        args << QStringLiteral("--random");
    control(args);
}

void Spotify::playPause()
{
    // at once on the screen; the daemon's answer follows
    if (!m_playback.isEmpty()) {
        m_playback[QStringLiteral("playing")] = !m_playback.value(QStringLiteral("playing")).toBool();
        emit playbackChanged();
    }
    control({QStringLiteral("play-pause")});
}

void Spotify::next() { control({QStringLiteral("next")}); }
void Spotify::previous() { control({QStringLiteral("previous")}); }
void Spotify::toggleShuffle() { control({QStringLiteral("shuffle")}); }
void Spotify::cycleRepeat() { control({QStringLiteral("repeat")}); }

void Spotify::seekTo(qint64 ms)
{
    // (the CLI seeks by an offset)
    const qint64 at = qint64(m_playback.value(QStringLiteral("progressMs")).toDouble());
    control({QStringLiteral("seek"), QStringLiteral("--"), QString::number(ms - at)});
}

void Spotify::like(bool like)
{
    QStringList args{QStringLiteral("like")};
    if (!like)
        args << QStringLiteral("--unlike");
    run(args);
}
