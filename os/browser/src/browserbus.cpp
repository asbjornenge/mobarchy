#include "browserbus.h"

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
