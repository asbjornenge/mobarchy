// Where the phone is, from GeoClue (whose source is the phone's own GNSS: mobarchy-location,
// ADR 0033). One fix at a time: refresh() starts a GeoClue client, takes the first location
// and stops it again, so the GNSS runs for a fix and not all the time.
#pragma once

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <functional>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class GeoLocation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(double accuracy READ accuracy NOTIFY changed)     // metres
    // looking for a fix now
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // GeoClue's accuracy level (4 city, 6 street, 8 exact)
    Q_PROPERTY(int accuracyLevel MEMBER m_level)

public:
    explicit GeoLocation(QObject *parent = nullptr);
    ~GeoLocation() override;

    bool valid() const { return m_valid; }
    double latitude() const { return m_latitude; }
    double longitude() const { return m_longitude; }
    double accuracy() const { return m_accuracy; }
    bool busy() const { return m_creating || !m_client.isEmpty(); }

    // a fresh fix (within `timeoutSeconds`; else the last one stays)
    Q_INVOKABLE void refresh(int timeoutSeconds = 120);

signals:
    void changed();
    void busyChanged();
    void failed(const QString &reason);

private slots:
    void locationUpdated(const QDBusObjectPath &old, const QDBusObjectPath &location);

private:
    void stop();
    void send(const QDBusMessage &m, std::function<void(const QDBusMessage &)> then);
    void setClientProperty(const QString &name, const QVariant &value, std::function<void()> then);

    bool m_creating = false;    // CreateClient asked, not answered yet

    QString m_client;       // the GeoClue client's object path while looking
    QTimer m_timeout;
    int m_level = 6;
    bool m_valid = false;
    double m_latitude = 0, m_longitude = 0, m_accuracy = 0;
};
