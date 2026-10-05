// The fingerprint sensor, through fprintd's interface on the system bus (net.reactivated.Fprint:
// mobarchy-fingerprint on comet, ADR 0021; fprintd itself elsewhere). The owner's fingers:
// which are enrolled, adding one (enrolment, touch by touch) and removing one, and verifying
// for the lock screen.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class Fingerprint : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // A sensor answers.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // The owner's enrolled fingers (fprintd's names: "right-thumb", ...).
    Q_PROPERTY(QStringList fingers READ fingers NOTIFY fingersChanged)
    // Enrolling: how far (0..1), and what to do now (empty: just touch the sensor).
    Q_PROPERTY(bool enrolling READ enrolling NOTIFY enrollingChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY hintChanged)
    Q_PROPERTY(bool verifying READ verifying NOTIFY verifyingChanged)

public:
    explicit Fingerprint(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QStringList fingers() const { return m_fingers; }
    bool enrolling() const { return m_mode == Enroll; }
    qreal progress() const;
    QString hint() const { return m_hint; }
    bool verifying() const { return m_mode == Verify; }

    Q_INVOKABLE void refresh();
    // Adds this finger (the shell has asked for the PIN first). enrolled() when it ends.
    Q_INVOKABLE void enroll(const QString &finger);
    Q_INVOKABLE void remove(const QString &finger);
    // Verifies any of the owner's fingers until a match (matched()) or stop().
    Q_INVOKABLE void verify();
    Q_INVOKABLE void stop();
    // A finger's name for people ("Right thumb").
    Q_INVOKABLE static QString label(const QString &finger);

signals:
    void availableChanged();
    void fingersChanged();
    void enrollingChanged();
    void progressChanged();
    void hintChanged();
    void verifyingChanged();
    void enrolled(bool ok, const QString &error);
    void matched();
    void noMatch();

private slots:
    void enrollStatus(const QString &result, bool done);
    void verifyStatus(const QString &result, bool done);
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &);

private:
    enum Mode { None, Enroll, Verify };
    void start(Mode mode, const QString &finger);
    void startAction();
    void end();
    void setHint(const QString &hint);
    void readStages();

    bool m_available = false;
    QStringList m_fingers;
    Mode m_mode = None;
    QString m_finger;
    bool m_claimed = false;
    int m_stages = 0;
    int m_passed = 0;
    QString m_hint;
};
