// The owner's contacts (ADR 0031): ~/.local/share/mobarchy/contacts.vcf, the vCard file the
// agent's tools also read and write (mobarchy-mcp contacts.py). Read-only here: the names, the
// phone numbers and the owner's word for each person (X-MOBARCHY-RELATION); followed, so a
// contact Frigg adds shows at once.
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
