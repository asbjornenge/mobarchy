#include "telephony.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcTel, "mobarchy.telephony")

// ofono lists objects (modems, calls) as an array of (path, properties).
struct OfonoObject {
    QDBusObjectPath path;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(OfonoObject)
using OfonoObjectList = QList<OfonoObject>;

const QDBusArgument &operator>>(const QDBusArgument &arg, OfonoObject &o)
{
    arg.beginStructure();
    arg >> o.path >> o.properties;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const OfonoObject &o)
{
    arg.beginStructure();
    arg << o.path << o.properties;
    arg.endStructure();
    return arg;
}

namespace {

const QString Service = QStringLiteral("org.ofono");
const QString Manager = QStringLiteral("org.ofono.Manager");
const QString ModemIface = QStringLiteral("org.ofono.Modem");
const QString SimIface = QStringLiteral("org.ofono.SimManager");
const QString NetIface = QStringLiteral("org.ofono.NetworkRegistration");
const QString CallManagerIface = QStringLiteral("org.ofono.VoiceCallManager");
const QString CallIface = QStringLiteral("org.ofono.VoiceCall");
const QString MessageIface = QStringLiteral("org.ofono.MessageManager");

QDBusConnection bus() { return QDBusConnection::systemBus(); }

// ofono's SIM "Retries" (a{sy}) — how many PIN tries are left — is not read yet (the
// a{sy} demarshalling needs care); pinRetries stays -1 (unknown) for now.
int retriesFor(const QVariant &, const QString &) { return -1; }

// the digits of a phone number ("+47 912 34 567" -> "4791234567")
QString digits(const QString &number)
{
    QString d;
    for (const QChar c : number)
        if (c.isDigit())
            d += c;
    return d;
}

// a value in an a{sv} may arrive as a QDBusArgument (an array, a nested dict); read it as a
// string list when that is what it is (e.g. the modem's Interfaces).
QStringList asStringList(const QVariant &v)
{
    if (v.canConvert<QStringList>())
        return v.toStringList();
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<QStringList>(v.value<QDBusArgument>());
    return {};
}

// A fire-and-forget method call; `onError` reports a failure through failed().
void call(const QString &path, const QString &iface, const QString &method,
          const QVariantList &args, Telephony *t, const QString &what)
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, path, iface, method);
    m.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m), t);
    QObject::connect(w, &QDBusPendingCallWatcher::finished, t, [t, what](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> r = *w;
        if (r.isError()) {
            qCWarning(lcTel) << what << "failed:" << r.error().name() << r.error().message();
            emit t->failed(what, r.error().message());
        }
        w->deleteLater();
    });
}

}  // namespace

Telephony::Telephony(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<OfonoObject>();
    qDBusRegisterMetaType<OfonoObjectList>();

    auto *watcher = new QDBusServiceWatcher(Service, bus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Telephony::serviceUp);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Telephony::serviceDown);

    bus().connect(Service, QStringLiteral("/"), Manager, QStringLiteral("ModemAdded"), this,
                  SLOT(modemAdded(QDBusObjectPath, QVariantMap)));
    bus().connect(Service, QStringLiteral("/"), Manager, QStringLiteral("ModemRemoved"), this,
                  SLOT(modemRemoved(QDBusObjectPath)));
    findModems();

    // the device's call audio, on the session bus
    const QString ca = QStringLiteral("org.mobarchy.CallAudio");
    auto *caWatch = new QDBusServiceWatcher(ca, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(caWatch, &QDBusServiceWatcher::serviceOwnerChanged, this, &Telephony::readCallAudio);
    QDBusConnection::sessionBus().connect(ca, QStringLiteral("/org/mobarchy/CallAudio"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
        SLOT(callAudioChanged(QString, QVariantMap, QStringList)));
    readCallAudio();
}

void Telephony::readCallAudio()
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.mobarchy.CallAudio"),
        QStringLiteral("/org/mobarchy/CallAudio"), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    m << QStringLiteral("org.mobarchy.CallAudio");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> r = *w;
        m_callAudio = !r.isError();
        if (m_callAudio)
            callAudioChanged(QString(), r.value(), {});
        else
            emit audioChanged();
        w->deleteLater();
    });
}

void Telephony::callAudioChanged(const QString &, const QVariantMap &changed, const QStringList &)
{
    if (changed.contains(QStringLiteral("Speaker")))
        m_speaker = changed.value(QStringLiteral("Speaker")).toBool();
    if (changed.contains(QStringLiteral("Muted")))
        m_muted = changed.value(QStringLiteral("Muted")).toBool();
    emit audioChanged();
}

void Telephony::setSpeaker(bool on)
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.mobarchy.CallAudio"),
        QStringLiteral("/org/mobarchy/CallAudio"), QStringLiteral("org.mobarchy.CallAudio"),
        QStringLiteral("SetSpeaker"));
    m << on;
    QDBusConnection::sessionBus().asyncCall(m);
}

void Telephony::setMuted(bool on)
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.mobarchy.CallAudio"),
        QStringLiteral("/org/mobarchy/CallAudio"), QStringLiteral("org.mobarchy.CallAudio"),
        QStringLiteral("SetMuted"));
    m << on;
    QDBusConnection::sessionBus().asyncCall(m);
}

void Telephony::serviceUp()
{
    findModems();
}

void Telephony::serviceDown()
{
    dropModem();
}

void Telephony::findModems()
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), Manager,
                                                    QStringLiteral("GetModems"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<OfonoObjectList> r = *w;
        if (!r.isError()) {
            const OfonoObjectList modems = r.value();
            if (!modems.isEmpty() && m_modem.isEmpty())
                useModem(modems.first().path.path(), modems.first().properties);
        }
        w->deleteLater();
    });
}

void Telephony::modemAdded(const QDBusObjectPath &path, const QVariantMap &properties)
{
    if (m_modem.isEmpty())
        useModem(path.path(), properties);
}

void Telephony::modemRemoved(const QDBusObjectPath &path)
{
    if (path.path() == m_modem)
        dropModem();
}

void Telephony::useModem(const QString &path, const QVariantMap &properties)
{
    qCInfo(lcTel) << "modem" << path;
    m_modem = path;
    m_online = properties.value(QStringLiteral("Online")).toBool();

    bus().connect(Service, m_modem, ModemIface, QStringLiteral("PropertyChanged"), this,
                  SLOT(modemPropertyChanged(QString, QDBusVariant)));
    bus().connect(Service, m_modem, SimIface, QStringLiteral("PropertyChanged"), this,
                  SLOT(simPropertyChanged(QString, QDBusVariant)));
    bus().connect(Service, m_modem, NetIface, QStringLiteral("PropertyChanged"), this,
                  SLOT(networkPropertyChanged(QString, QDBusVariant)));
    bus().connect(Service, m_modem, CallManagerIface, QStringLiteral("CallAdded"), this,
                  SLOT(callAdded(QDBusObjectPath, QVariantMap)));
    bus().connect(Service, m_modem, CallManagerIface, QStringLiteral("CallRemoved"), this,
                  SLOT(callRemoved(QDBusObjectPath)));
    bus().connect(Service, m_modem, MessageIface, QStringLiteral("IncomingMessage"), this,
                  SLOT(incomingMessage(QString, QVariantMap)));

    watchInterfaces(asStringList(properties.value(QStringLiteral("Interfaces"))));
    emit changed();
}

void Telephony::dropModem()
{
    if (m_modem.isEmpty())
        return;
    bus().disconnect(Service, m_modem, QString(), QString(), this, nullptr);
    const QList<QString> callPaths = m_callMap.keys();
    for (const QString &p : callPaths)
        bus().disconnect(Service, p, CallIface, QStringLiteral("PropertyChanged"), this, nullptr);
    m_modem.clear();
    m_interfaces.clear();
    m_online = m_simPresent = false;
    m_pinRequired = QStringLiteral("none");
    m_pinRetries = -1;
    m_operator.clear();
    m_registration = QStringLiteral("unregistered");
    m_signal = 0;
    m_callMap.clear();
    m_ringing.clear();
    rebuildCalls();
    emit changed();
}

void Telephony::watchInterfaces(const QStringList &interfaces)
{
    m_interfaces = interfaces;
    if (interfaces.contains(SimIface))
        refreshSim();
    if (interfaces.contains(NetIface))
        refreshNetwork();
    if (interfaces.contains(CallManagerIface))
        refreshCalls();
}

// ---- SIM ----

void Telephony::refreshSim()
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, m_modem, SimIface,
                                                    QStringLiteral("GetProperties"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> r = *w;
        if (!r.isError()) {
            const QVariantMap p = r.value();
            m_simPresent = p.value(QStringLiteral("Present")).toBool();
            m_pinRequired = p.value(QStringLiteral("PinRequired"), QStringLiteral("none")).toString();
            m_pinRetries = retriesFor(p.value(QStringLiteral("Retries")), m_pinRequired);
            emit changed();
        }
        w->deleteLater();
    });
}

void Telephony::simPropertyChanged(const QString &name, const QDBusVariant &value)
{
    if (name == QLatin1String("Present"))
        m_simPresent = value.variant().toBool();
    else if (name == QLatin1String("PinRequired"))
        m_pinRequired = value.variant().toString();
    else if (name == QLatin1String("Retries"))
        m_pinRetries = retriesFor(value.variant(), m_pinRequired);
    else
        return;
    emit changed();
}

void Telephony::enterPin(const QString &pin)
{
    if (m_modem.isEmpty())
        return;
    call(m_modem, SimIface, QStringLiteral("EnterPin"), {m_pinRequired, pin}, this, tr("Unlock SIM"));
}

void Telephony::resetPin(const QString &puk, const QString &newPin)
{
    if (m_modem.isEmpty())
        return;
    call(m_modem, SimIface, QStringLiteral("ResetPin"), {m_pinRequired, puk, newPin}, this, tr("Unlock SIM"));
}

// ---- network / radio ----

void Telephony::setOnline(bool on)
{
    if (m_modem.isEmpty())
        return;
    call(m_modem, ModemIface, QStringLiteral("SetProperty"),
         {QStringLiteral("Online"), QVariant::fromValue(QDBusVariant(on))}, this, tr("Radio"));
}

void Telephony::modemPropertyChanged(const QString &name, const QDBusVariant &value)
{
    if (name == QLatin1String("Online"))
        m_online = value.variant().toBool();
    else if (name == QLatin1String("Interfaces"))
        watchInterfaces(asStringList(value.variant()));
    else
        return;
    emit changed();
}

void Telephony::refreshNetwork()
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, m_modem, NetIface,
                                                    QStringLiteral("GetProperties"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> r = *w;
        if (!r.isError()) {
            const QVariantMap p = r.value();
            m_registration = p.value(QStringLiteral("Status"), m_registration).toString();
            m_operator = p.value(QStringLiteral("Name")).toString();
            m_signal = p.value(QStringLiteral("Strength")).toInt();
            m_technology = p.value(QStringLiteral("Technology")).toString();
            emit changed();
        }
        w->deleteLater();
    });
}

void Telephony::networkPropertyChanged(const QString &name, const QDBusVariant &value)
{
    if (name == QLatin1String("Status"))
        m_registration = value.variant().toString();
    else if (name == QLatin1String("Name"))
        m_operator = value.variant().toString();
    else if (name == QLatin1String("Strength"))
        m_signal = value.variant().toInt();
    else if (name == QLatin1String("Technology"))
        m_technology = value.variant().toString();
    else
        return;
    emit changed();
}

// ---- calls ----

void Telephony::refreshCalls()
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, m_modem, CallManagerIface,
                                                    QStringLiteral("GetCalls"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<OfonoObjectList> r = *w;
        if (!r.isError())
            for (const OfonoObject &o : r.value())
                callAdded(o.path, o.properties);
        w->deleteLater();
    });
}

void Telephony::callAdded(const QDBusObjectPath &path, const QVariantMap &properties)
{
    const QString p = path.path();
    const bool isNew = !m_callMap.contains(p);
    Call c;
    c.state = properties.value(QStringLiteral("State")).toString();
    c.number = properties.value(QStringLiteral("LineIdentification")).toString();
    c.name = properties.value(QStringLiteral("Name")).toString();
    m_callMap.insert(p, c);
    if (isNew)
        bus().connect(Service, p, CallIface, QStringLiteral("PropertyChanged"), this,
                      SLOT(callPropertyChanged(QString, QDBusVariant, QDBusMessage)));
    if (c.state == QLatin1String("incoming") && !m_ringing.contains(p)) {
        m_ringing.insert(p);
        emit incomingCall(p, c.number);
    }
    rebuildCalls();
}

void Telephony::callRemoved(const QDBusObjectPath &path)
{
    const QString p = path.path();
    if (!m_callMap.remove(p))
        return;
    bus().disconnect(Service, p, CallIface, QStringLiteral("PropertyChanged"), this, nullptr);
    m_ringing.remove(p);
    rebuildCalls();
}

// ofono sends (name, value) on the call's own path; Qt fills the trailing QDBusMessage, so
// message.path() tells us which call changed.
void Telephony::callPropertyChanged(const QString &name, const QDBusVariant &value,
                                    const QDBusMessage &message)
{
    auto it = m_callMap.find(message.path());
    if (it == m_callMap.end())
        return;
    if (name == QLatin1String("State")) {
        it->state = value.variant().toString();
        if (it->state == QLatin1String("incoming") && !m_ringing.contains(it.key())) {
            m_ringing.insert(it.key());
            emit incomingCall(it.key(), it->number);
        }
    } else if (name == QLatin1String("LineIdentification"))
        it->number = value.variant().toString();
    else if (name == QLatin1String("Name"))
        it->name = value.variant().toString();
    else
        return;
    rebuildCalls();
}

void Telephony::incomingMessage(const QString &text, const QVariantMap &info)
{
    const QString from = info.value(QStringLiteral("Sender")).toString();
    qCInfo(lcTel) << "SMS from" << from;
    if (m_keeper)
        storeMessage(from, text, true);
    else  // the shell's instance writes it; show it once that is done
        QTimer::singleShot(500, this, &Telephony::messagesChanged);
    emit messageReceived(from, text);
}

void Telephony::dial(const QString &number)
{
    qCInfo(lcTel) << "dial" << number << "on" << m_modem;
    if (m_modem.isEmpty() || number.isEmpty()) {
        emit failed(tr("Call"), m_modem.isEmpty() ? tr("no modem") : tr("no number"));
        return;
    }
    call(m_modem, CallManagerIface, QStringLiteral("Dial"), {number, QStringLiteral("")}, this, tr("Call"));
}

void Telephony::answer(const QString &path)
{
    call(path, CallIface, QStringLiteral("Answer"), {}, this, tr("Answer"));
}

void Telephony::hangup(const QString &path)
{
    call(path, CallIface, QStringLiteral("Hangup"), {}, this, tr("Hang up"));
}

void Telephony::hangupAll()
{
    if (!m_modem.isEmpty())
        call(m_modem, CallManagerIface, QStringLiteral("HangupAll"), {}, this, tr("Hang up"));
}

void Telephony::sendTones(const QString &tones)
{
    if (!m_modem.isEmpty())
        call(m_modem, CallManagerIface, QStringLiteral("SendTones"), {tones}, this, QString());
}

void Telephony::rebuildCalls()
{
    // active/held/dialing/alerting first, incoming next, then the rest
    QList<QString> paths = m_callMap.keys();
    std::sort(paths.begin(), paths.end());
    m_calls.clear();
    for (const QString &p : std::as_const(paths)) {
        const Call &c = m_callMap.value(p);
        const bool incoming = c.state == QLatin1String("incoming") || c.state == QLatin1String("waiting");
        const bool active = c.state == QLatin1String("active") || c.state == QLatin1String("held");
        m_calls.append(QVariantMap{{QStringLiteral("path"), p},
                                   {QStringLiteral("state"), c.state},
                                   {QStringLiteral("number"), c.number},
                                   {QStringLiteral("name"), c.name},
                                   {QStringLiteral("incoming"), incoming},
                                   {QStringLiteral("active"), active}});
    }
    emit callsChanged();
}

// ---- SMS storage (~/.local/share/mobarchy/messages.json), shared with the agent ----

void Telephony::sendMessage(const QString &to, const QString &text)
{
    qCInfo(lcTel) << "SMS to" << to << "on" << m_modem;
    if (m_modem.isEmpty() || to.isEmpty()) {
        emit failed(tr("Send message"), m_modem.isEmpty() ? tr("no modem") : tr("no number"));
        return;
    }
    call(m_modem, MessageIface, QStringLiteral("SendMessage"), {to, text}, this, tr("Send message"));
    storeMessage(to, text, false);
}

QVariantMap Telephony::threads() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + QStringLiteral("/mobarchy/messages.json");
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(in.readAll()).object().toVariantMap();
}

void Telephony::storeMessage(const QString &number, const QString &text, bool incoming)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/mobarchy");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/messages.json");
    QJsonObject root;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly))
        root = QJsonDocument::fromJson(in.readAll()).object();
    // One thread per person however the number is written (+4791234567 from the network,
    // 91234567 as typed): numbers are the same thread when their last 8 digits are; the
    // thread keeps the fullest form (with the country code).
    QString key = number;
    const QString tail = digits(number).right(8);
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (tail.size() == 8 && digits(it.key()).right(8) == tail) {
            key = it.key();
            break;
        }
    }
    QJsonArray thread = root.value(key).toArray();
    thread.append(QJsonObject{{QStringLiteral("text"), text},
                              {QStringLiteral("incoming"), incoming},
                              {QStringLiteral("time"), QDateTime::currentSecsSinceEpoch()}});
    root.remove(key);
    const QString fullest = digits(number).size() > digits(key).size() ? number : key;
    root[fullest] = thread;
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        out.commit();
    }
    emit messagesChanged();
}
