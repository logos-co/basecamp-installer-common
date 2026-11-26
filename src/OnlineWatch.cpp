#include "logos/installer/OnlineWatch.h"

#include <QDebug>
#include <QNetworkInformation>

namespace logos::installer {

OnlineWatch::OnlineWatch(QObject* parent) : QObject(parent)
{
    const QByteArray forced = qgetenv("BASECAMP_FORCE_OFFLINE");
    if (forced == "1" || forced == "0") {
        m_online = forced == "0";
        return;
    }

    // The platform's own backend first: by features alone Qt may take glib,
    // which outside Linux never sees the connection go.
    const bool loaded = QNetworkInformation::loadDefaultBackend()
        || QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
    if (!loaded) {
        qInfo() << "OnlineWatch: no reachability backend among"
                << QNetworkInformation::availableBackends() << "- assuming online";
        return;
    }
    const auto apply = [this] {
        const auto r = QNetworkInformation::instance()->reachability();
        setOnline(r != QNetworkInformation::Reachability::Disconnected
                  && r != QNetworkInformation::Reachability::Local);
    };
    connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this, apply);
    apply();
}

void OnlineWatch::setOnline(bool online)
{
    if (online == m_online) return;
    m_online = online;
    emit onlineChanged();
}

}  // namespace logos::installer
