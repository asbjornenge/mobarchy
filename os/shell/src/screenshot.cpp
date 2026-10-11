#include "screenshot.h"

#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>

Screenshot::Screenshot(QObject *parent)
    : QObject(parent)
{
}

void Screenshot::take()
{
    if (m_busy)
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                        + QStringLiteral("/Screenshots");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/Skjermbilde_")
                         + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"))
                         + QStringLiteral(".png");
    auto *grim = new QProcess(this);
    m_busy = true;
    connect(grim, &QProcess::finished, this, [this, grim, path](int code, QProcess::ExitStatus status) {
        m_busy = false;
        grim->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            emit taken(path);
        } else {
            const QString error = QString::fromUtf8(grim->readAllStandardError()).trimmed();
            qWarning("screenshot: grim failed: %s", qPrintable(error));
            emit failed(error);
        }
    });
    connect(grim, &QProcess::errorOccurred, this, [this, grim](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_busy = false;
        grim->deleteLater();
        qWarning("screenshot: grim is not there");
        emit failed(QStringLiteral("grim is not installed"));
    });
    grim->start(QStringLiteral("grim"), {path});
}
