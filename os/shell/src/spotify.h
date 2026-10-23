// Spotify (the cassette player, ADR 0041/0042): mobarchy-spotify, Mobarchy's own Spotify
// service on librespot (mobarchy-spotify.service), plays on the phone as a Spotify Connect
// device and is driven here over D-Bus (org.mobarchy.Spotify): the controls go straight to
// its local player, what plays comes as it changes, and the library and search come from
// Spotify's Web API through it. Signing in is its own: the pages it wants opened come here.
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class Spotify : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "starting", "signedOut", "signingIn", "connecting", "ready", "error"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // What plays: {title, artists, album, image, uri, trackId, durationMs, progressMs, playing,
    // shuffle, repeat ("off", "track", "context"), contextUri}; empty: nothing
    Q_PROPERTY(QVariantMap playback READ playback NOTIFY playbackChanged)
    // the library: playlists [{id, uri, name, owner, image}], tracks [{id, uri, name, artists,
    // album, image, durationMs}], albums [{id, uri, name, artists, image}]
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList liked READ liked NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY libraryChanged)
    // a search's results: {tracks, albums, playlists, artists}
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)
    // an opened playlist or album: {type, id, uri, name, tracks, loading}
    Q_PROPERTY(QVariantMap opened READ opened NOTIFY openedChanged)
    // the tape in the deck: the playlist or album playing from ({type, uri, name, tracks}),
    // its songs listed under the deck; empty: a song on its own
    Q_PROPERTY(QVariantMap loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // (kept for the app: what plays comes as it changes, nothing is polled)
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)

public:
    explicit Spotify(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QVariantMap playback() const;
    QVariantList playlists() const { return m_playlists; }
    QVariantList liked() const { return m_liked; }
    QVariantList albums() const { return m_albums; }
    QVariantMap results() const { return m_results; }
    QVariantMap opened() const { return m_opened; }
    QVariantMap loaded() const { return m_loaded; }
    QString error() const { return m_error; }
    bool watching() const { return m_watching; }
    void setWatching(bool watching);

    // Sign in: the service's pages to approve come as signInUrl (two: the library's, then
    // the streaming's); signedInBack once done.
    Q_INVOKABLE void signIn();
    // forget the account
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void loadLibrary();
    Q_INVOKABLE void search(const QString &query);
    // a playlist's or album's tracks (opened)
    Q_INVOKABLE void open(const QString &type, const QString &id, const QString &name);
    Q_INVOKABLE void playContext(const QString &type, const QString &id, bool shuffle = false);
    // a song: from the tape in the deck when it is one of its songs (it plays on from there),
    // else on its own
    Q_INVOKABLE void playTrack(const QString &id);
    Q_INVOKABLE void playLiked(bool shuffle = false);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // to a position in the track (ms)
    Q_INVOKABLE void seekTo(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();

signals:
    void stateChanged();
    void playbackChanged();
    void libraryChanged();
    void resultsChanged();
    void openedChanged();
    void loadedChanged();
    void errorChanged();
    void watchingChanged();
    // a sign-in page to open in the browser
    void signInUrl(const QString &url);
    void signedInBack();

private slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &);
    void onSignInUrl(const QString &url);
    void onSignedIn();

private:
    void call(const QString &method, const QVariantList &args = {});
    // a method answering JSON: done(parsed) when it comes
    void callJson(const QString &method, const QVariantList &args, std::function<void(const QVariant &)> done);
    void fetchAll();
    void apply(const QVariantMap &props);
    void setState(const QString &state);
    void setError(const QString &error);

    QString m_state = QStringLiteral("starting");
    QVariantMap m_props;            // the service's properties, as it said them
    QVariantList m_playlists, m_liked, m_albums;
    QVariantMap m_results, m_opened, m_loaded;
    QString m_error;
    bool m_watching = false;
    QTimer m_startRetry;
};
