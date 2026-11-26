#pragma once

#include <QObject>

namespace logos::installer {

// Whether this host reaches the internet, for the download notice. A network
// with nothing behind it counts as offline. BASECAMP_FORCE_OFFLINE=1 or 0
// decides instead, for tests and sandboxes without a network. With no
// reachability backend at all, online.
class OnlineWatch : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool online READ isOnline NOTIFY onlineChanged)

public:
    explicit OnlineWatch(QObject* parent = nullptr);

    bool isOnline() const { return m_online; }

signals:
    void onlineChanged();

private:
    void setOnline(bool online);

    bool m_online = true;
};

}  // namespace logos::installer
