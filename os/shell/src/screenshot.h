// A screenshot (power and volume down together): grim takes the screens as they are into
// ~/Pictures/Screenshots/Skjermbilde_<date>_<time>.png. taken() when it's saved (the
// shell flashes then: after the picture, so the flash isn't in it).
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Screenshot : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Screenshot(QObject *parent = nullptr);
    Q_INVOKABLE void take();

signals:
    void taken(const QString &path);
    void failed(const QString &error);

private:
    bool m_busy = false;
};
