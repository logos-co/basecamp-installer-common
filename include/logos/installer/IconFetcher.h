#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace logos::installer {

class PackageModel;

// Fetches the catalog's icons for a PackageModel and gives each package its
// own copy as a data: URL, which a sandboxed view can show (Basecamp's
// SandboxImages makes an image of it that only that view's engine reads).
// An icon is used only when it is a PNG within the limits below and, where
// the catalog names its sha256, has that hash. Fetched icons are kept by hash
// in memory and, given a directory, on disk.
class IconFetcher : public QObject {
    Q_OBJECT

public:
    // Fetch `url`, answering once with its bytes or an error.
    using Done = std::function<void(const QByteArray& data, const QString& error)>;
    using Fetch = std::function<void(const QUrl& url, Done done)>;

    static constexpr qint64 kMaxBytes = 1024 * 1024;
    static constexpr int kMaxSide = 4096;
    static constexpr int kParallel = 4;

    explicit IconFetcher(PackageModel* model, QObject* parent = nullptr);

    // Instead of HTTP(S) through a QNetworkAccessManager.
    void setFetch(Fetch fetch);
    void setCacheDirectory(const QString& directory);
    // Fetch again what failed or was refused, as a reload should.
    void retry();

    // Why `data` can't be shown as an icon expected to hash to `sha256`
    // (empty: no hash to check), or "" when it can.
    static QString refusal(const QByteArray& data, const QString& sha256);

signals:
    void iconRefused(const QString& name, const QString& reason);

private:
    struct Want { QString name; bool fullBleed = false; };

    void scheduleScan();
    void scan();
    void startNext();
    void fetched(const QString& key, const QString& sha256, const QByteArray& data,
                 const QString& error);
    QString diskPath(const QString& sha256) const;
    void defaultFetch(const QUrl& url, Done done);

    QPointer<PackageModel> m_model;
    Fetch m_fetch;
    QNetworkAccessManager* m_network = nullptr;
    QString m_cacheDirectory;
    bool m_scanQueued = false;

    // By icon: the sha256, or the URL when the catalog names none.
    QHash<QString, QString> m_dataUrlByKey;
    QHash<QString, QList<Want>> m_waitingByKey;
    QHash<QString, QUrl> m_urlByKey;
    QHash<QString, QString> m_sha256ByKey;
    QList<QString> m_queue;
    QSet<QString> m_inFlight;
    QSet<QString> m_refused;
    // The icon each package shows now.
    QHash<QString, QString> m_keyByName;
};

}  // namespace logos::installer
