#include "fingerprint.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QTimer>

namespace {
const QString Service = QStringLiteral("net.reactivated.Fprint");
const QString ManagerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const QString Manager = QStringLiteral("net.reactivated.Fprint.Manager");
const QString Device = QStringLiteral("net.reactivated.Fprint.Device");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");

QDBusConnection bus() { return QDBusConnection::systemBus(); }
}  // namespace

// The device: the default one (fprintd's, and mobarchy-fingerprint's only one).
static QString devicePath = QStringLiteral("/net/reactivated/Fprint/Device/0");

Fingerprint::Fingerprint(QObject *parent)
    : QObject(parent)
{
    bus().connect(Service, QString(), Device, QStringLiteral("EnrollStatus"), this,
                  SLOT(enrollStatus(QString, bool)));
    bus().connect(Service, QString(), Device, QStringLiteral("VerifyStatus"), this,
                  SLOT(verifyStatus(QString, bool)));
    bus().connect(Service, QString(), Properties, QStringLiteral("PropertiesChanged"), this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    auto *watcher = new QDBusServiceWatcher(Service, bus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        m_claimed = false;
        if (m_mode != None)
            end();
        refresh();
    });
    refresh();
}

QString Fingerprint::label(const QString &finger)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("left-thumb"), tr("Left thumb")},
        {QStringLiteral("left-index-finger"), tr("Left index finger")},
        {QStringLiteral("left-middle-finger"), tr("Left middle finger")},
        {QStringLiteral("left-ring-finger"), tr("Left ring finger")},
        {QStringLiteral("left-little-finger"), tr("Left little finger")},
        {QStringLiteral("right-thumb"), tr("Right thumb")},
        {QStringLiteral("right-index-finger"), tr("Right index finger")},
        {QStringLiteral("right-middle-finger"), tr("Right middle finger")},
        {QStringLiteral("right-ring-finger"), tr("Right ring finger")},
        {QStringLiteral("right-little-finger"), tr("Right little finger")},
    };
    return names.value(finger, finger);
}

qreal Fingerprint::progress() const
{
    return m_stages > 0 ? qMin(1.0, qreal(m_passed) / m_stages) : 0;
}

void Fingerprint::refresh()
{
    auto get = QDBusMessage::createMethodCall(Service, ManagerPath, Manager,
                                              QStringLiteral("GetDefaultDevice"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(get), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        const bool available = !reply.isError();
        if (available)
            devicePath = reply.value().path();
        if (available != m_available) {
            m_available = available;
            emit availableChanged();
        }
        if (!available) {
            if (!m_fingers.isEmpty()) {
                m_fingers.clear();
                emit fingersChanged();
            }
            return;
        }
        auto list = QDBusMessage::createMethodCall(Service, devicePath, Device,
                                                   QStringLiteral("ListEnrolledFingers"));
        list << QString();
        auto *l = new QDBusPendingCallWatcher(bus().asyncCall(list), this);
        connect(l, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *l) {
            l->deleteLater();
            QDBusPendingReply<QStringList> reply = *l;
            // (no fingers is an error: NoEnrolledPrints)
            const QStringList fingers = reply.isError() ? QStringList() : reply.value();
            if (fingers != m_fingers) {
                m_fingers = fingers;
                emit fingersChanged();
            }
        });
    });
}

void Fingerprint::enroll(const QString &finger) { start(Enroll, finger); }
void Fingerprint::verify() { start(Verify, QStringLiteral("any")); }

void Fingerprint::start(Mode mode, const QString &finger)
{
    if (m_mode != None)
        return;
    m_mode = mode;
    m_finger = finger;
    m_passed = 0;
    setHint(QString());
    emit mode == Enroll ? enrollingChanged() : verifyingChanged();
    emit progressChanged();
    if (m_claimed) {
        startAction();
        return;
    }
    auto claim = QDBusMessage::createMethodCall(Service, devicePath, Device, QStringLiteral("Claim"));
    claim << QString();
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(claim), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qWarning() << "fingerprint: claim:" << reply.error().message();
            const Mode mode = m_mode;
            end();
            if (mode == Enroll)
                emit enrolled(false, tr("The fingerprint sensor is busy"));
            return;
        }
        m_claimed = true;
        if (m_mode != None)
            startAction();
        else
            end();          // stopped meanwhile: release it
    });
}

void Fingerprint::startAction()
{
    if (m_mode == Enroll)
        readStages();
    auto call = QDBusMessage::createMethodCall(Service, devicePath, Device,
                                               m_mode == Enroll ? QStringLiteral("EnrollStart")
                                                                : QStringLiteral("VerifyStart"));
    call << m_finger;
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (!reply.isError())
            return;
        qWarning() << "fingerprint:" << reply.error().message();
        const Mode mode = m_mode;
        end();
        if (mode == Enroll)
            emit enrolled(false, reply.error().name().endsWith(QLatin1String("PermissionDenied"))
                                     ? tr("Not allowed") : tr("The fingerprint sensor isn't available"));
    });
}

void Fingerprint::readStages()
{
    auto get = QDBusMessage::createMethodCall(Service, devicePath, Properties, QStringLiteral("Get"));
    get << Device << QStringLiteral("num-enroll-stages");
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(get), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (!reply.isError()) {
            m_stages = reply.value().variant().toInt();
            emit progressChanged();
        }
    });
}

void Fingerprint::stop()
{
    if (m_mode == None)
        return;
    if (m_claimed) {
        auto call = QDBusMessage::createMethodCall(Service, devicePath, Device,
                                                   m_mode == Enroll ? QStringLiteral("EnrollStop")
                                                                    : QStringLiteral("VerifyStop"));
        bus().asyncCall(call);
    }
    end();
}

// The action is over: let the sensor go (fprintd's clients claim it only while they use it).
void Fingerprint::end()
{
    const Mode mode = m_mode;
    m_mode = None;
    setHint(QString());
    if (mode == Enroll)
        emit enrollingChanged();
    else if (mode == Verify)
        emit verifyingChanged();
    if (m_claimed) {
        m_claimed = false;
        bus().asyncCall(QDBusMessage::createMethodCall(Service, devicePath, Device, QStringLiteral("Release")));
    }
}

void Fingerprint::setHint(const QString &hint)
{
    if (hint == m_hint)
        return;
    m_hint = hint;
    emit hintChanged();
}

void Fingerprint::remove(const QString &finger)
{
    auto claim = QDBusMessage::createMethodCall(Service, devicePath, Device, QStringLiteral("Claim"));
    claim << QString();
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(claim), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, finger](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (QDBusPendingReply<>(*w).isError())
            return;
        auto del = QDBusMessage::createMethodCall(Service, devicePath, Device,
                                                  QStringLiteral("DeleteEnrolledFinger"));
        del << finger;
        auto *d = new QDBusPendingCallWatcher(bus().asyncCall(del), this);
        connect(d, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *d) {
            d->deleteLater();
            QDBusPendingReply<> reply = *d;
            if (reply.isError())
                qWarning() << "fingerprint: remove:" << reply.error().message();
            bus().asyncCall(QDBusMessage::createMethodCall(Service, devicePath, Device, QStringLiteral("Release")));
            refresh();
        });
    });
}

// fprintd's results, as what to do next
static QString hintFor(const QString &result)
{
    if (result.endsWith(QLatin1String("remove-and-retry")))
        return Fingerprint::tr("Lift, and touch again a little elsewhere");
    if (result.endsWith(QLatin1String("finger-not-centered")))
        return Fingerprint::tr("Cover the whole sensor");
    if (result.endsWith(QLatin1String("retry-scan")) || result.endsWith(QLatin1String("too-short")))
        return Fingerprint::tr("Hold still a moment longer");
    return QString();
}

void Fingerprint::enrollStatus(const QString &result, bool done)
{
    if (m_mode != Enroll)
        return;
    if (result == QLatin1String("enroll-stage-passed")) {
        m_passed++;
        emit progressChanged();
        setHint(QString());
    } else if (!done) {
        setHint(hintFor(result));
    }
    if (!done)
        return;
    const bool ok = result == QLatin1String("enroll-completed");
    if (ok) {
        m_passed = m_stages;
        emit progressChanged();
    }
    end();
    refresh();
    emit enrolled(ok, ok ? QString()
                    : result == QLatin1String("enroll-data-full") ? tr("No room for more fingers")
                    : result == QLatin1String("enroll-disconnected") ? tr("The fingerprint sensor went away")
                                                                      : tr("That didn't work: try again"));
}

void Fingerprint::verifyStatus(const QString &result, bool done)
{
    if (m_mode != Verify)
        return;
    if (!done) {
        setHint(hintFor(result));
        return;
    }
    if (result == QLatin1String("verify-match")) {
        end();
        emit matched();
        return;
    }
    if (result == QLatin1String("verify-no-match")) {
        emit noMatch();
        // fprintd's verify ends at a no-match: the next touch is another one
        QTimer::singleShot(300, this, [this] {
            if (m_mode == Verify)
                startAction();
        });
        return;
    }
    end();
}

void Fingerprint::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface == Device && changed.contains(QStringLiteral("num-enroll-stages"))) {
        m_stages = changed.value(QStringLiteral("num-enroll-stages")).toInt();
        emit progressChanged();
    }
}
