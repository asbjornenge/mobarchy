// The owner's pictures (the Photos app): every photo, screenshot and video under ~/Pictures,
// newest first, each with its album (the folder it is in: Camera, Screenshots, ...). It
// follows the folders, so a new screenshot or photo shows at once. Deleting moves to the
// trash, and only what is under ~/Pictures.
//
// With `service` on (the Photos app's), it also answers org.mobarchy.Photos.Show(path) on the
// session bus: the agent's photos_show, or a second start of the app, shows a picture in the
// running one.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class PhotoLibrary : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    // "" for all of them, or an album's name
    Q_PROPERTY(QString album READ album WRITE setAlbum NOTIFY albumChanged)
    // [{name, count}], the biggest first ("" not included: that is all of them)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY albumsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool service READ service WRITE setService NOTIFY serviceChanged)

public:
    enum Role { FilePathRole = Qt::UserRole + 1, AlbumRole, TimeRole, VideoRole, DayRole };

    explicit PhotoLibrary(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString album() const { return m_album; }
    void setAlbum(const QString &album);
    QVariantList albums() const;
    int count() const { return int(m_shown.size()); }
    bool service() const { return m_service; }
    void setService(bool on);

    // the row a picture is in now (-1: not shown, e.g. another album)
    Q_INVOKABLE int indexOf(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QDateTime timeAt(int row) const;
    // to the trash; false if it isn't one of the pictures
    Q_INVOKABLE bool trash(const QString &path);

    static QString root();

signals:
    void albumChanged();
    void albumsChanged();
    void countChanged();
    void serviceChanged();
    void showRequested(const QString &path);

public slots:
    // (org.mobarchy.Photos)
    void Show(const QString &path);

private:
    struct Picture {
        QString path;
        QString album;
        QDateTime time;
        bool video = false;
    };
    void scan();
    void filter();

    QFileSystemWatcher m_watcher;
    QTimer m_rescan;
    QList<Picture> m_all;      // newest first
    QList<int> m_shown;        // rows of m_all in the album
    QString m_album;
    bool m_service = false;
};

// Opens the Photos app at a picture: shows it in the running one (org.mobarchy.Photos.Show),
// or starts it (mobarchy-shell --photos <path>, in an app scope of its own, as the shell
// starts apps).
class Photos : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Photos(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE static void open(const QString &path);
};
