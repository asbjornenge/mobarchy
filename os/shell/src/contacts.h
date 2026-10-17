// The owner's contacts (ADR 0031, 0032): the owner's own vCard file
// (~/.local/share/mobarchy/contacts.vcf, the agent's tools write it) and each internet
// account's (contacts/<id>.vcf, from mobarchy-accounts), one person per name; the owner's
// words for people from relations.json ("far"). Read-only here; followed, so a contact Frigg
// adds or a sync brings shows at once.
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Contacts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{name, phones: [{number, type}], relations: [..]}], by name
    Q_PROPERTY(QVariantList all READ all NOTIFY changed)

public:
    explicit Contacts(QObject *parent = nullptr);

    QVariantList all() const { return m_all; }
    // the contacts whose name, relation or number matches (the shell's search box)
    Q_INVOKABLE QVariantList filter(const QString &query) const;
    // the name for a number (however it is written: the last 8 digits), "" if none
    Q_INVOKABLE QString nameFor(const QString &number) const;

    static QString path();
    static bool sameNumber(const QString &a, const QString &b);

signals:
    void changed();

private:
    void load();
    QFileSystemWatcher m_watcher;
    QVariantList m_all;
};
