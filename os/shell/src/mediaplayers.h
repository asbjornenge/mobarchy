// What's playing: the media players on the session bus (MPRIS, org.mpris.MediaPlayer2.*),
// and the one in charge, the player that last started playing (or, with none playing, the
// one used last). The "now playing" cards and the media keys (a headset's buttons) act on it.
#pragma once

#include <QDBusMessage>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class MediaPlayers : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)      // e.g. "Firefox"
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)
    Q_PROPERTY(bool canControl READ canControl NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(qint64 length READ length NOTIFY changed)             // ms, 0: unknown
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)  // ms

public:
    explicit MediaPlayers(QObject *parent = nullptr);

    bool available() const { return !m_active.isEmpty(); }
    QString identity() const { return active().identity; }
    QString desktopEntry() const { return active().desktopEntry; }
    QString title() const;
    QString artist() const;
    QString album() const;
    QString artUrl() const;
    bool playing() const { return active().status == QLatin1String("Playing"); }
    bool canGoNext() const { return active().canNext; }
    bool canGoPrevious() const { return active().canPrevious; }
    bool canControl() const { return active().canPlay || active().canPause; }
    bool canSeek() const { return active().canSeek && length() > 0; }
    qint64 length() const;
    qint64 position() const;

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    // Ask the player where it is now (Position is not signalled).
    Q_INVOKABLE void refreshPosition();

signals:
    void changed();
    void positionChanged();

private slots:
    void nameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated, const QDBusMessage &message);
    void seeked(qlonglong position, const QDBusMessage &message);

private:
    struct Player {
        QString service;      // org.mpris.MediaPlayer2.<name>
        QString identity, desktopEntry;
        QString status;       // Playing, Paused, Stopped
        QVariantMap metadata;
        bool canNext = false, canPrevious = false, canPlay = false, canPause = false, canSeek = false;
        qint64 position = 0;  // µs, at positionAt
        qint64 positionAt = 0;
        qint64 lastActive = 0;  // when it last started playing or was controlled
    };
    void add(const QString &service, const QString &owner);
    void apply(Player &p, const QString &interface, const QVariantMap &props);
    void choose();
    void call(const QString &method, const QVariantList &args = {});
    const Player &active() const;
    Player *bySender(const QString &sender);

    QHash<QString, Player> m_players;  // by unique name (the sender of signals)
    QString m_active;                  // unique name
    QElapsedTimer m_clock;
    QTimer m_tick;
};
