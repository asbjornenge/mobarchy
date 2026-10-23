// org.mobarchy.Browser on the session bus (ADR 0023): a second start hands its URLs to the
// running browser, and the agent opens pages and reads which are open.
#pragma once

#include <QObject>
#include <QStringList>

class BrowserBus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mobarchy.Browser")
    // What the UI shows, kept up to date by it: the tabs' titles and URLs, and which is current.
    Q_PROPERTY(QStringList titles MEMBER m_titles)
    Q_PROPERTY(QStringList urls MEMBER m_urls)
    Q_PROPERTY(int current MEMBER m_current)

public:
    static constexpr const char *Service = "org.mobarchy.Browser";
    static constexpr const char *Path = "/org/mobarchy/Browser";

    explicit BrowserBus(QObject *parent = nullptr) : QObject(parent) {}
    static BrowserBus *instance();
    // The URLs given on the command line, for the first window.
    Q_INVOKABLE QStringList startUrls() const { return m_start; }
    void setStartUrls(const QStringList &urls) { m_start = urls; }
    // Puts the page on the home screen as a web app (mobarchy-webapp add): webAppAdded after.
    Q_INVOKABLE void addWebApp(const QString &url, const QString &name);

public slots:
    // Opens the URL in a new tab and shows the browser.
    Q_SCRIPTABLE void Open(const QString &url) { emit openRequested(url); }
    // Shows the browser as it is (a second start without URLs).
    Q_SCRIPTABLE void Show() { emit showRequested(); }
    // The open tabs: (title, URL), and the current one's index.
    Q_SCRIPTABLE QStringList Tabs() const;
    Q_SCRIPTABLE int Current() const { return m_current; }
    // Closes the tab at index (as Tabs lists them), e.g. a sign-in page an app is done with.
    Q_SCRIPTABLE void Close(int index) { emit closeRequested(index); }

signals:
    void openRequested(const QString &url);
    void closeRequested(int index);
    void showRequested();
    void webAppAdded(bool ok, const QString &message);

private:
    QStringList m_titles, m_urls, m_start;
    int m_current = -1;
};
