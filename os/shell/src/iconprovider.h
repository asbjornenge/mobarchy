// image://icon/<name or absolute path>: icons from the icon theme for QML Images.
// An Image provider (not Pixmap): Qt Quick may call it from its loader threads, so it
// avoids QIcon/QPixmap (GUI thread only) and reads the file with QImageReader.
#pragma once

#include "icontheme.h"

#include <QImageReader>
#include <QQuickImageProvider>

class IconProvider : public QQuickImageProvider
{
public:
    explicit IconProvider(const QString &theme)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_theme(theme)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int extent = requestedSize.width() > 0 ? requestedSize.width() : 128;
        QString file = m_theme.lookup(id, extent);
        if (file.isEmpty())
            file = m_theme.lookup(QStringLiteral("application-x-executable"), extent);
        QImage image;
        if (!file.isEmpty()) {
            QImageReader reader(file);
            const QSize natural = reader.size();
            // Vectors render at the size asked for; bitmaps are scaled to fit it.
            if (natural.isValid() && (file.endsWith(QStringLiteral(".svg")) || natural.width() > extent))
                reader.setScaledSize(natural.scaled(extent, extent, Qt::KeepAspectRatio));
            image = reader.read();
        }
        if (size)
            *size = image.size();
        return image;
    }

private:
    IconTheme m_theme;
};
