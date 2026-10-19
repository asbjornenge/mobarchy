#include "pinlock.h"

#include "launch.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStandardPaths>

#include <crypt.h>
#include <security/pam_appl.h>
#include <thread>
#include <unistd.h>
#include <pwd.h>

namespace {

const QString Accounts = QStringLiteral("org.freedesktop.Accounts");
const QString UserIface = QStringLiteral("org.freedesktop.Accounts.User");
const QString Secrets = QStringLiteral("org.freedesktop.secrets");
const QString SecretsPath = QStringLiteral("/org/freedesktop/secrets");

// The Secret Service's secret: (session, parameters, value, content type).
struct Secret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const Secret &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, Secret &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

QString stateFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/mobarchy/pin.json");
}

// Waiting after the fifth wrong PIN in a row: 30 s, doubling, at most an hour.
int waitAfter(int failures)
{
    if (failures < 5)
        return 0;
    return std::min(3600, 30 << std::min(failures - 5, 7));
}

struct Conversation {
    QByteArray pin;
};

int converse(int n, const pam_message **msg, pam_response **resp, void *data)
{
    auto *c = static_cast<Conversation *>(data);
    auto *r = static_cast<pam_response *>(calloc(size_t(n), sizeof(pam_response)));
    if (!r)
        return PAM_BUF_ERR;
    for (int i = 0; i < n; ++i) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            r[i].resp = strdup(c->pin.constData());
    }
    *resp = r;
    return PAM_SUCCESS;
}

bool pamCheck(const QByteArray &user, const QByteArray &pin)
{
    Conversation c{pin};
    pam_conv conv{converse, &c};
    pam_handle_t *h = nullptr;
    if (pam_start("mobarchy-lock", user.constData(), &conv, &h) != PAM_SUCCESS)
        return false;
    int rc = pam_authenticate(h, 0);
    if (rc == PAM_SUCCESS)
        rc = pam_acct_mgmt(h, 0);
    pam_end(h, rc);
    return rc == PAM_SUCCESS;
}

QByteArray userName()
{
    const passwd *pw = getpwuid(getuid());
    return pw ? QByteArray(pw->pw_name) : qgetenv("USER");
}

}  // namespace

Q_DECLARE_METATYPE(Secret)

PinLock::PinLock(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<Secret>();
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        emit waitChanged();
        if (wait() == 0)
            m_tick.stop();
    });
    loadState();
    // (the runtime directory goes with the session: a restart starts without)
    m_givenSinceBoot = QFile::exists(runtimeDir() + QStringLiteral("/pin-given"));
    m_wasLocked = QFile::exists(runtimeDir() + QStringLiteral("/locked"));
    QDBusReply<QDBusObjectPath> user = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(Accounts, QStringLiteral("/org/freedesktop/Accounts"), Accounts,
                                       QStringLiteral("FindUserById"))
        << qlonglong(getuid()));
    if (user.isValid())
        m_userPath = user.value().path();
    else
        qWarning() << "pin: AccountsService has no user for us:" << user.error().message();
    refresh();
}

void PinLock::refresh()
{
    if (m_userPath.isEmpty())
        return;
    // A PIN is set when the account has a password that isn't locked ("!" in shadow).
    auto get = [this](const char *name) {
        QDBusReply<QDBusVariant> r = QDBusConnection::systemBus().call(
            QDBusMessage::createMethodCall(Accounts, m_userPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                           QStringLiteral("Get"))
            << UserIface << QString::fromLatin1(name));
        return r.isValid() ? r.value().variant() : QVariant();
    };
    const bool has = !get("Locked").toBool() && get("PasswordMode").toInt() == 0;
    if (has != m_hasPin) {
        m_hasPin = has;
        emit hasPinChanged();
    }
}

int PinLock::wait() const
{
    if (!m_waitUntil.isValid())
        return 0;
    return int(std::max<qint64>(0, (QDateTime::currentDateTimeUtc().msecsTo(m_waitUntil) + 999) / 1000));
}

void PinLock::setBusy(bool busy)
{
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void PinLock::check(const QString &pin, std::function<void(bool)> done)
{
    setBusy(true);
    QPointer<PinLock> self(this);
    const QByteArray user = userName();
    const QByteArray secret = pin.toUtf8();
    std::thread([self, user, secret, done] {
        const bool ok = pamCheck(user, secret);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, ok, done] {
            if (!self)
                return;
            self->setBusy(false);
            done(ok);
        });
    }).detach();
}

void PinLock::counted(bool ok)
{
    if (ok) {
        m_failures = 0;
        m_waitUntil = {};
    } else {
        ++m_failures;
        if (const int s = waitAfter(m_failures)) {
            m_waitUntil = QDateTime::currentDateTimeUtc().addSecs(s);
            m_tick.start();
        }
    }
    saveState();
    emit waitChanged();
}

void PinLock::verify(const QString &pin)
{
    if (m_busy || wait() > 0) {
        emit verified(false);
        return;
    }
    check(pin, [this](bool ok) {
        counted(ok);
        if (ok)
            setGiven();
        emit verified(ok);
    });
}

QString PinLock::runtimeDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/mobarchy");
}

void PinLock::setGiven()
{
    if (m_givenSinceBoot)
        return;
    QDir().mkpath(runtimeDir());
    QFile f(runtimeDir() + QStringLiteral("/pin-given"));
    f.open(QIODevice::WriteOnly);
    m_givenSinceBoot = true;
    emit givenSinceBootChanged();
    // The keyring is open now (pam_gnome_keyring): the accounts' sync, which waits for that
    // rather than asking for the keyring's password, can run.
    launch::detached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("start"),
                                                   QStringLiteral("--no-block"),
                                                   QStringLiteral("mobarchy-accounts-sync.service")});
}

void PinLock::change(const QString &oldPin, const QString &newPin)
{
    if (m_busy)
        return;
    if (newPin.size() < MinLength) {
        emit changed(false, tr("A PIN has at least %1 digits").arg(MinLength));
        return;
    }
    auto apply = [this, oldPin, newPin] {
        QString error;
        // The keyring first: if that fails, the password stays as it was. Without a PIN the
        // keyring has an empty password, unless it came from a backup of a phone that had
        // one: then it opens with that PIN, which the owner most likely sets again.
        bool keyed = rekey(m_hasPin ? oldPin : QString(), newPin, &error);
        if (!keyed && !m_hasPin) {
            QString again;
            keyed = rekey(newPin, newPin, &again);
            if (!keyed)
                error = tr("The keyring is locked with an earlier PIN (from before a restore): "
                           "set that PIN, the one the phone had before.");
        }
        if (!keyed || !setPassword(newPin, &error)) {
            emit changed(false, error);
            return;
        }
        setGiven();         // (before hasPin turns true: the shell locks at a PIN not yet given)
        refresh();
        emit changed(true, {});
    };
    if (!m_hasPin) {
        apply();
        return;
    }
    if (wait() > 0) {
        emit changed(false, tr("Wait %1 s").arg(wait()));
        return;
    }
    check(oldPin, [this, apply](bool ok) {
        counted(ok);
        if (ok)
            apply();
        else
            emit changed(false, tr("Wrong PIN"));
    });
}

void PinLock::remove(const QString &oldPin)
{
    if (m_busy || !m_hasPin)
        return;
    if (wait() > 0) {
        emit changed(false, tr("Wait %1 s").arg(wait()));
        return;
    }
    check(oldPin, [this, oldPin](bool ok) {
        counted(ok);
        if (!ok) {
            emit changed(false, tr("Wrong PIN"));
            return;
        }
        QString error;
        if (!rekey(oldPin, QString(), &error)) {
            emit changed(false, error);
            return;
        }
        // Locked again ("!" before the hash): no password, no PIN.
        QDBusMessage reply = QDBusConnection::systemBus().call(
            QDBusMessage::createMethodCall(Accounts, m_userPath, UserIface, QStringLiteral("SetLocked")) << true);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            emit changed(false, reply.errorMessage());
            return;
        }
        refresh();
        emit changed(true, {});
    });
}

bool PinLock::setPassword(const QString &newPin, QString *error)
{
    // AccountsService takes the crypt(3) hash; the default method (yescrypt) and a fresh salt.
    char *salt = crypt_gensalt_ra(nullptr, 0, nullptr, 0);
    if (!salt) {
        *error = tr("No salt for the PIN");
        return false;
    }
    crypt_data data = {};
    const char *hash = crypt_r(newPin.toUtf8().constData(), salt, &data);
    free(salt);
    if (!hash || hash[0] == '*') {
        *error = tr("The PIN could not be hashed");
        return false;
    }
    QDBusMessage reply = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(Accounts, m_userPath, UserIface, QStringLiteral("SetPassword"))
        << QString::fromLatin1(hash) << QString());
    explicit_bzero(&data, sizeof(data));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = reply.errorMessage();
        return false;
    }
    return true;
}

bool PinLock::rekey(const QString &oldPin, const QString &newPin, QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusReply<QDBusObjectPath> keyring = bus.call(
        QDBusMessage::createMethodCall(Secrets, SecretsPath, QStringLiteral("org.freedesktop.Secret.Service"),
                                       QStringLiteral("ReadAlias"))
        << QStringLiteral("default"));
    if (!keyring.isValid() || keyring.value().path() == QLatin1String("/"))
        return true;  // no keyring (yet): nothing to re-encrypt
    QDBusMessage open = bus.call(
        QDBusMessage::createMethodCall(Secrets, SecretsPath, QStringLiteral("org.freedesktop.Secret.Service"),
                                       QStringLiteral("OpenSession"))
        << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString())));
    if (open.type() == QDBusMessage::ErrorMessage || open.arguments().size() < 2) {
        *error = tr("Keyring: %1").arg(open.errorMessage());
        return false;
    }
    const QDBusObjectPath session = open.arguments().at(1).value<QDBusObjectPath>();
    const auto secret = [&](const QString &pin) {
        return QVariant::fromValue(Secret{session, {}, pin.toUtf8(), QStringLiteral("text/plain")});
    };
    // GNOME Keyring's own interface for changing a keyring's password without its prompt.
    QDBusMessage reply = bus.call(
        QDBusMessage::createMethodCall(Secrets, SecretsPath,
                                       QStringLiteral("org.gnome.keyring.InternalUnsupportedGuiltRiddenInterface"),
                                       QStringLiteral("ChangeWithMasterPassword"))
        << QVariant::fromValue(keyring.value()) << secret(oldPin) << secret(newPin));
    bus.call(QDBusMessage::createMethodCall(Secrets, session.path(), QStringLiteral("org.freedesktop.Secret.Session"),
                                            QStringLiteral("Close")));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = tr("Keyring: %1").arg(reply.errorMessage());
        return false;
    }
    return true;
}

void PinLock::setLocked(bool locked)
{
    const QString dir = runtimeDir();
    const QString marker = dir + QStringLiteral("/locked");
    if (locked && m_hasPin) {
        QDir().mkpath(dir);
        QFile f(marker);
        f.open(QIODevice::WriteOnly);
    } else {
        QFile::remove(marker);
    }
}

void PinLock::loadState()
{
    QFile f(stateFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_failures = o.value(QStringLiteral("failures")).toInt();
    m_waitUntil = QDateTime::fromString(o.value(QStringLiteral("wait_until")).toString(), Qt::ISODate);
    if (wait() > 0)
        m_tick.start();
}

void PinLock::saveState()
{
    QDir().mkpath(QFileInfo(stateFile()).path());
    QFile f(stateFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("failures"), m_failures},
                                      {QStringLiteral("wait_until"), m_waitUntil.toString(Qt::ISODate)}})
                .toJson(QJsonDocument::Compact));
}
