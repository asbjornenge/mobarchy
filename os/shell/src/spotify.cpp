#include "spotify.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QProcess>

namespace {
const QString Service = QStringLiteral("org.mobarchy.Spotify");
const QString Path = QStringLiteral("/org/mobarchy/Spotify");
const QString Iface = QStringLiteral("org.mobarchy.Spotify");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString Unit = QStringLiteral("mobarchy-spotify.service");

QString resumeFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/mobarchy/spotify-resume.json");
}

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

// a{sv} from D-Bus (a QDBusArgument inside a variant) as a map
QVariantMap mapOf(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>()) {
        QVariantMap m;
        v.value<QDBusArgument>() >> m;
        return m;
    }
    return v.toMap();
}

// the service's lists keep the URI; the app's rows want an id too (the URI's last part)
QVariantList withIds(const QVariant &json)
{
    QVariantList out;
    for (const QVariant &v : json.toList()) {
        QVariantMap m = v.toMap();
        const QString uri = m.value(QStringLiteral("uri")).toString();
        if (!m.contains(QStringLiteral("id")))
            m[QStringLiteral("id")] = uri.section(u':', -1);
        if (m.value(QStringLiteral("artists")).typeId() == QMetaType::QVariantList)
            m[QStringLiteral("artists")] = m.value(QStringLiteral("artists")).toStringList().join(QStringLiteral(", "));
        out << m;
    }
    return out;
}

QString uriOf(const QString &type, const QString &id)
{
    return id.startsWith(QLatin1String("spotify:")) ? id : QStringLiteral("spotify:%1:%2").arg(type, id);
}
}  // namespace

Spotify::Spotify(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Service, Path, Properties, QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    bus.connect(Service, Path, Iface, QStringLiteral("SignInUrl"), this, SLOT(onSignInUrl(QString)));
    bus.connect(Service, Path, Iface, QStringLiteral("SignedIn"), this, SLOT(onSignedIn()));
    // (the service restarted, or came up after the app: all again)
    auto *watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Spotify::fetchAll);
    // the service runs on (Spotify plays on with the app closed); started here if it isn't
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("start"), Unit});
    QFile f(resumeFile());
    if (f.open(QIODevice::ReadOnly))
        m_resume = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    m_startRetry.setInterval(1500);
    connect(&m_startRetry, &QTimer::timeout, this, &Spotify::fetchAll);
    m_startRetry.start();
    fetchAll();
}

void Spotify::setWatching(bool watching)
{
    if (watching == m_watching)
        return;
    m_watching = watching;
    emit watchingChanged();
}

void Spotify::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
    if (state == u"ready")
        loadLibrary();
}

void Spotify::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Spotify::fetchAll()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, Path, Properties, QStringLiteral("GetAll"));
    msg << Iface;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return;             // (not up yet: again in a moment)
        m_startRetry.stop();
        apply(reply.value());
    });
}

void Spotify::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface == Iface)
        apply(changed);
}

void Spotify::apply(const QVariantMap &props)
{
    bool playbackChanged_ = false;
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        QVariant v = it.value();
        if (it.key() == u"Track")
            v = mapOf(v);
        m_props[it.key()] = v;
        if (it.key() != u"State" && it.key() != u"Error")
            playbackChanged_ = true;
    }
    if (props.contains(QStringLiteral("Error")))
        setError(m_props.value(QStringLiteral("Error")).toString());
    if (props.contains(QStringLiteral("State")))
        setState(m_props.value(QStringLiteral("State")).toString());
    if (playbackChanged_) {
        emit playbackChanged();
        noteResume();
    }
}

void Spotify::noteResume()
{
    const QVariantMap p = playback();
    if (p.value(QStringLiteral("kind")).toString() != u"episode")
        return;
    m_resume[p.value(QStringLiteral("uri")).toString()] = QVariantMap{
        {QStringLiteral("positionMs"), p.value(QStringLiteral("progressMs"))},
        {QStringLiteral("durationMs"), p.value(QStringLiteral("durationMs"))},
        {QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch()}};
    // (on a pause at once, while it plays now and then)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!p.value(QStringLiteral("playing")).toBool() || now - m_resumeSaved > 15000)
        saveResume();
}

void Spotify::saveResume()
{
    m_resumeSaved = QDateTime::currentMSecsSinceEpoch();
    QDir().mkpath(QFileInfo(resumeFile()).path());
    QFile f(resumeFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_resume)).toJson(QJsonDocument::Compact));
}

QVariantList Spotify::withResume(const QVariantList &episodes) const
{
    // Spotify's resume point, or ours when we have one (librespot doesn't report positions)
    QVariantList out;
    for (QVariant v : episodes) {
        QVariantMap e = v.toMap();
        const QVariantMap mine = m_resume.value(e.value(QStringLiteral("uri")).toString()).toMap();
        if (!mine.isEmpty()) {
            const qint64 pos = mine.value(QStringLiteral("positionMs")).toLongLong();
            const qint64 dur = e.value(QStringLiteral("durationMs")).toLongLong();
            e[QStringLiteral("resumeMs")] = pos;
            e[QStringLiteral("fullyPlayed")] = dur > 0 && pos > dur - 30000;   // (the last half minute: heard)
        }
        out << e;
    }
    return out;
}

QVariantMap Spotify::playback() const
{
    const QVariantMap t = m_props.value(QStringLiteral("Track")).toMap();
    if (t.value(QStringLiteral("title")).toString().isEmpty())
        return {};
    const bool playing = m_props.value(QStringLiteral("Playing")).toBool();
    // the position now: as said at PositionAt, moved on since while it plays
    qint64 at = m_props.value(QStringLiteral("PositionMs")).toLongLong();
    if (playing)
        at += QDateTime::currentMSecsSinceEpoch() - m_props.value(QStringLiteral("PositionAt")).toLongLong();
    const qint64 duration = t.value(QStringLiteral("durationMs")).toLongLong();
    return {{QStringLiteral("title"), t.value(QStringLiteral("title"))},
            {QStringLiteral("artists"), t.value(QStringLiteral("artists")).toStringList().join(QStringLiteral(", "))},
            {QStringLiteral("album"), t.value(QStringLiteral("album"))},
            {QStringLiteral("image"), t.value(QStringLiteral("image"))},
            {QStringLiteral("uri"), t.value(QStringLiteral("uri"))},
            {QStringLiteral("trackId"), t.value(QStringLiteral("uri"))},
            {QStringLiteral("kind"), t.value(QStringLiteral("kind")).toString().isEmpty() ? QStringLiteral("track") : t.value(QStringLiteral("kind"))},
            {QStringLiteral("durationMs"), duration},
            {QStringLiteral("progressMs"), qBound(qint64(0), at, duration > 0 ? duration : at)},
            {QStringLiteral("playing"), playing},
            {QStringLiteral("shuffle"), m_props.value(QStringLiteral("Shuffle")).toBool()},
            {QStringLiteral("repeat"), m_props.value(QStringLiteral("Repeat")).toString()},
            {QStringLiteral("contextUri"), m_props.value(QStringLiteral("ContextUri")).toString()}};
}

void Spotify::call(const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, Path, Iface, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qWarning() << "Spotify" << method << reply.error().message();
            setError(reply.error().message());
        }
    });
}

void Spotify::callJson(const QString &method, const QVariantList &args, std::function<void(const QVariant &)> done)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, Path, Iface, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, method, done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            qWarning() << "Spotify" << method << reply.error().message();
            return;
        }
        done(QJsonDocument::fromJson(reply.value().toUtf8()).toVariant());
    });
}

// --- signing in --------------------------------------------------------------------------

void Spotify::signIn()
{
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("start"), Unit});
    call(QStringLiteral("SignIn"));
}

void Spotify::onSignInUrl(const QString &url)
{
    // the page before it goes first (an old one approved again would answer the wrong callback)
    closeSignInTabs();
    emit signInUrl(url);
}

void Spotify::onSignedIn()
{
    closeSignInTabs();
    emit signedInBack();
}

void Spotify::signOut()
{
    call(QStringLiteral("SignOut"));
    m_playlists.clear();
    m_liked.clear();
    m_albums.clear();
    m_shows.clear();
    m_audiobooks.clear();
    m_loaded.clear();
    emit libraryChanged();
    emit loadedChanged();
}

// --- the library ---------------------------------------------------------------------------

void Spotify::loadLibrary()
{
    callJson(QStringLiteral("Playlists"), {}, [this](const QVariant &v) {
        m_playlists = withIds(v);
        emit libraryChanged();
    });
    callJson(QStringLiteral("LikedTracks"), {}, [this](const QVariant &v) {
        m_liked = withIds(v);
        emit libraryChanged();
    });
    callJson(QStringLiteral("SavedAlbums"), {}, [this](const QVariant &v) {
        m_albums = withIds(v);
        emit libraryChanged();
    });
    callJson(QStringLiteral("Shows"), {}, [this](const QVariant &v) {
        m_shows = withIds(v);
        emit libraryChanged();
    });
    callJson(QStringLiteral("Audiobooks"), {}, [this](const QVariant &v) {
        m_audiobooks = withIds(v);
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
    callJson(QStringLiteral("Search"), {query}, [this](const QVariant &v) {
        const QVariantMap r = v.toMap();
        m_results = {{QStringLiteral("tracks"), withIds(r.value(QStringLiteral("tracks")))},
                     {QStringLiteral("albums"), withIds(r.value(QStringLiteral("albums")))},
                     {QStringLiteral("playlists"), withIds(r.value(QStringLiteral("playlists")))},
                     {QStringLiteral("artists"), withIds(r.value(QStringLiteral("artists")))}};
        emit resultsChanged();
    });
}

void Spotify::open(const QString &type, const QString &id, const QString &name)
{
    const QString uri = uriOf(type, id);
    m_opened = {{QStringLiteral("type"), type}, {QStringLiteral("id"), id}, {QStringLiteral("uri"), uri},
                {QStringLiteral("name"), name}, {QStringLiteral("tracks"), QVariantList()}, {QStringLiteral("loading"), true}};
    emit openedChanged();
    const QString method = type == u"show" ? QStringLiteral("Episodes")
                         : type == u"audiobook" ? QStringLiteral("Chapters") : QStringLiteral("ContextTracks");
    callJson(method, {uri}, [this, uri](const QVariant &v) {
        if (m_opened.value(QStringLiteral("uri")) != uri)
            return;
        m_opened[QStringLiteral("tracks")] = withResume(withIds(v));
        m_opened[QStringLiteral("loading")] = false;
        emit openedChanged();
    });
}

// --- playing ---------------------------------------------------------------------------------

void Spotify::playContext(const QString &type, const QString &id, bool shuffle)
{
    const QString uri = uriOf(type, id);
    // into the deck: the one opened on the shelf, with its songs
    m_loaded = m_opened.value(QStringLiteral("uri")).toString() == uri ? m_opened
             : QVariantMap{{QStringLiteral("type"), type}, {QStringLiteral("uri"), uri}};
    emit loadedChanged();
    call(QStringLiteral("Load"), {uri, QString(), shuffle});
}

void Spotify::playTrack(const QString &id)
{
    const QString type = m_loaded.value(QStringLiteral("type")).toString();
    if (type == u"show" || type == u"audiobook") {
        playEpisode(id);
        return;
    }
    const QString uri = uriOf(QStringLiteral("track"), id);
    // one of the deck's tape's songs: the tape from there on
    for (const QVariant &t : m_loaded.value(QStringLiteral("tracks")).toList()) {
        if (t.toMap().value(QStringLiteral("uri")).toString() == uri && !m_loaded.value(QStringLiteral("uri")).toString().isEmpty()) {
            call(QStringLiteral("Load"), {m_loaded.value(QStringLiteral("uri")).toString(), uri, false});
            return;
        }
    }
    m_loaded.clear();
    emit loadedChanged();
    call(QStringLiteral("PlayTrack"), {uri});
}

void Spotify::playEpisode(const QString &id)
{
    const QString uri = uriOf(QStringLiteral("episode"), id);
    // from the opened show or book, or the deck's
    const QVariantMap from = m_opened.value(QStringLiteral("tracks")).toList().isEmpty() ? m_loaded : m_opened;
    QVariantMap episode;
    for (const QVariant &t : from.value(QStringLiteral("tracks")).toList())
        if (t.toMap().value(QStringLiteral("uri")).toString() == uri)
            episode = t.toMap();
    const qint64 at = episode.value(QStringLiteral("fullyPlayed")).toBool() ? 0 : episode.value(QStringLiteral("resumeMs")).toLongLong();
    if (from.value(QStringLiteral("uri")) != m_loaded.value(QStringLiteral("uri"))) {
        m_loaded = from;
        emit loadedChanged();
    }
    call(QStringLiteral("LoadAt"), {from.value(QStringLiteral("uri")).toString(), uri, qlonglong(at)});
}

void Spotify::continueBook()
{
    const QVariantList chapters = m_opened.value(QStringLiteral("tracks")).toList();
    for (const QVariant &c : chapters) {
        if (!c.toMap().value(QStringLiteral("fullyPlayed")).toBool()) {
            playEpisode(c.toMap().value(QStringLiteral("id")).toString());
            return;
        }
    }
    if (!chapters.isEmpty())
        playEpisode(chapters.first().toMap().value(QStringLiteral("id")).toString());
}

void Spotify::playLiked(bool shuffle)
{
    m_loaded = {{QStringLiteral("type"), QStringLiteral("liked")}, {QStringLiteral("uri"), QStringLiteral("spotify:collection")},
                {QStringLiteral("name"), tr("Liked Songs")}, {QStringLiteral("tracks"), m_liked}};
    emit loadedChanged();
    call(QStringLiteral("Load"), {QStringLiteral("spotify:collection"), QString(), shuffle});
}

void Spotify::playPause() { call(QStringLiteral("PlayPause")); }
void Spotify::next() { call(QStringLiteral("Next")); }
void Spotify::previous() { call(QStringLiteral("Previous")); }
void Spotify::seekTo(qint64 ms) { call(QStringLiteral("Seek"), {qlonglong(ms)}); }

void Spotify::toggleShuffle()
{
    call(QStringLiteral("SetShuffle"), {!m_props.value(QStringLiteral("Shuffle")).toBool()});
}

void Spotify::cycleRepeat()
{
    const QString r = m_props.value(QStringLiteral("Repeat")).toString();
    call(QStringLiteral("SetRepeat"), {r == u"off" ? QStringLiteral("context") : r == u"context" ? QStringLiteral("track") : QStringLiteral("off")});
}
