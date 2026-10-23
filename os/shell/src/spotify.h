// Spotify (the cassette player): spotify-player (spotify_player) runs as a Spotify Connect
// device in the background (mobarchy-spotify.service: it plays on the phone, and MPRIS gives
// the lock screen and Frigg its controls); this drives it through its CLI, whose answers
// are JSON. Signing in is spotify_player's own (OAuth in the browser, twice: the Web API's
// and the streaming's).
#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class Spotify : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "starting", "signedOut", "signingIn", "ready"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // What plays: {title, artists, album, image, durationMs, progressMs, playing, shuffle,
    // repeat ("off", "track", "context"), trackId, contextUri, device}; empty: nothing
    Q_PROPERTY(QVariantMap playback READ playback NOTIFY playbackChanged)
    // the library: [{id, name, owner}], tracks [{id, name, artists, album, image, durationMs}],
    // albums [{id, name, artists, image}]
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList liked READ liked NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY libraryChanged)
    // a search's results: {tracks, albums, playlists, artists}
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)
    // an opened playlist or album: {type, id, name, tracks}
    Q_PROPERTY(QVariantMap opened READ opened NOTIFY openedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // follow what plays (once a second) while something shows it
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)

public:
    explicit Spotify(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QVariantMap playback() const { return m_playback; }
    QVariantList playlists() const { return m_playlists; }
    QVariantList liked() const { return m_liked; }
    QVariantList albums() const { return m_albums; }
    QVariantMap results() const { return m_results; }
    QVariantMap opened() const { return m_opened; }
    QString error() const { return m_error; }
    bool watching() const { return m_watching; }
    void setWatching(bool watching);

    // Sign in: Spotify's page in the browser, twice (signInUrl: open each, once); both answers
    // go to spotify_player's own callback (127.0.0.1:8989). Done: the pages close, and
    // signedInBack (the app comes back to the front).
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void loadLibrary();
    Q_INVOKABLE void search(const QString &query);
    // a playlist's or album's tracks (opened)
    Q_INVOKABLE void open(const QString &type, const QString &id, const QString &name);
    Q_INVOKABLE void playContext(const QString &type, const QString &id, bool shuffle = false);
    Q_INVOKABLE void playTrack(const QString &id);
    Q_INVOKABLE void playLiked(bool shuffle = false);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // to a position in the track (ms)
    Q_INVOKABLE void seekTo(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void like(bool like = true);

signals:
    void stateChanged();
    void playbackChanged();
    void libraryChanged();
    void resultsChanged();
    void openedChanged();
    void errorChanged();
    void watchingChanged();
    void signInUrl(const QString &url);
    void signedInBack();

private:
    // spotify_player with args; done(ok, stdout) when it ends
    void run(const QStringList &args, std::function<void(bool, const QByteArray &)> done = {});
    void control(const QStringList &args);   // then the playback again, soon
    void refresh();
    void checkSignedIn();
    void setState(const QString &state);
    void setError(const QString &error);

    QString m_state = QStringLiteral("starting");
    QVariantMap m_playback;
    QVariantList m_playlists, m_liked, m_albums;
    QVariantMap m_results, m_opened;
    QString m_error;
    bool m_watching = false;
    bool m_refreshing = false;
    QTimer m_poll;
    QProcess *m_signIn = nullptr;
    QTemporaryDir *m_noBrowser = nullptr;
    int m_urls = 0;
};
