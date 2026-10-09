#include "browserbus.h"

#include <QProcess>

BrowserBus *BrowserBus::instance()
{
    static BrowserBus *bus = new BrowserBus;
    return bus;
}

QStringList BrowserBus::Tabs() const
{
    QStringList tabs;
    for (int i = 0; i < m_urls.size(); i++)
        tabs << m_titles.value(i) + QLatin1Char('\t') + m_urls.at(i);
    return tabs;
}

void BrowserBus::addWebApp(const QString &url, const QString &name)
{
    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(proc->readAll()).trimmed();
        emit webAppAdded(status == QProcess::NormalExit && code == 0, out.section(QLatin1Char('\n'), -1));
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        emit webAppAdded(false, proc->errorString());
        proc->deleteLater();
    });
    QStringList args{QStringLiteral("add"), url};
    if (!name.isEmpty())
        args << QStringLiteral("--name") << name;
    proc->start(QStringLiteral("mobarchy-webapp"), args);
}
