#include "logos/installer/IconFetcher.h"

#include "logos/installer/PackageModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

namespace logos::installer {

namespace {

const QByteArray kPngMagic("\x89PNG\r\n\x1a\n", 8);

quint32 bigEndian32(const QByteArray& data, int at)
{
    return (quint32(quint8(data[at])) << 24) | (quint32(quint8(data[at + 1])) << 16)
         | (quint32(quint8(data[at + 2])) << 8) | quint32(quint8(data[at + 3]));
}

bool isSha256(const QString& s)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    return hex.match(s).hasMatch();
}

}  // namespace

IconFetcher::IconFetcher(PackageModel* model, QObject* parent)
    : QObject(parent), m_model(model)
{
    m_fetch = [this](const QUrl& url, Done done) { defaultFetch(url, std::move(done)); };
    connect(model, &QAbstractItemModel::rowsInserted, this, &IconFetcher::scheduleScan);
    connect(model, &QAbstractItemModel::modelReset, this, &IconFetcher::scheduleScan);
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                if (roles.isEmpty() || roles.contains(PackageModel::CatalogIconUrlRole)) scheduleScan();
            });
    scheduleScan();
}

void IconFetcher::setFetch(Fetch fetch)
{
    m_fetch = std::move(fetch);
}

void IconFetcher::setCacheDirectory(const QString& directory)
{
    m_cacheDirectory = directory;
    if (!directory.isEmpty()) QDir().mkpath(directory);
}

void IconFetcher::retry()
{
    m_refused.clear();
    scheduleScan();
}

QString IconFetcher::refusal(const QByteArray& data, const QString& sha256)
{
    if (data.size() > kMaxBytes) return QStringLiteral("larger than %1 bytes").arg(kMaxBytes);
    // The index publishes PNG only; anything else may not be what it claims.
    if (!data.startsWith(kPngMagic) || data.size() < 24 || data.mid(12, 4) != "IHDR")
        return QStringLiteral("not a PNG");
    const quint32 width = bigEndian32(data, 16);
    const quint32 height = bigEndian32(data, 20);
    if (width == 0 || height == 0 || width > quint32(kMaxSide) || height > quint32(kMaxSide))
        return QStringLiteral("%1x%2 pixels").arg(width).arg(height);
    if (!sha256.isEmpty()
        && QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != sha256.toLatin1())
        return QStringLiteral("not the sha256 the catalog names");
    return {};
}

void IconFetcher::scheduleScan()
{
    if (m_scanQueued) return;
    m_scanQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_scanQueued = false;
        scan();
    });
}

void IconFetcher::scan()
{
    if (!m_model) return;
    QSet<QString> named;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex idx = m_model->index(row);
        const QString name = idx.data(PackageModel::NameRole).toString();
        if (name.isEmpty() || named.contains(name)) continue;
        const QString url = idx.data(PackageModel::CatalogIconUrlRole).toString();
        if (url.isEmpty()) continue;
        named.insert(name);
        const QString sha = idx.data(PackageModel::CatalogIconSha256Role).toString().toLower();
        const QString key = sha.isEmpty() ? url : sha;
        if (m_keyByName.value(name) == key || m_refused.contains(key)) continue;
        const bool fullBleed = idx.data(PackageModel::SupportsFullBleedIconRole).toBool();

        if (!m_dataUrlByKey.contains(key) && !diskPath(sha).isEmpty()) {
            QFile file(diskPath(sha));
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray data = file.read(kMaxBytes + 1);
                if (refusal(data, sha).isEmpty())
                    m_dataUrlByKey.insert(key, QStringLiteral("data:image/png;base64,")
                                                   + QString::fromLatin1(data.toBase64()));
            }
        }
        const auto known = m_dataUrlByKey.constFind(key);
        if (known != m_dataUrlByKey.cend()) {
            m_keyByName.insert(name, key);
            m_model->setIcon(name, *known, fullBleed);
            continue;
        }

        QList<Want>& waiting = m_waitingByKey[key];
        const bool alreadyWaiting = std::any_of(waiting.cbegin(), waiting.cend(),
                                                [&](const Want& w) { return w.name == name; });
        if (!alreadyWaiting) waiting.append({name, fullBleed});
        if (!m_inFlight.contains(key) && !m_queue.contains(key)) {
            m_urlByKey.insert(key, QUrl(url));
            m_sha256ByKey.insert(key, sha);
            m_queue.append(key);
        }
    }

    // The catalog no longer names an icon for these.
    for (auto it = m_keyByName.begin(); it != m_keyByName.end();) {
        if (named.contains(it.key())) { ++it; continue; }
        const QString name = it.key();
        it = m_keyByName.erase(it);
        m_model->setIcon(name, {}, false);
    }
    startNext();
}

void IconFetcher::startNext()
{
    while (m_inFlight.size() < kParallel && !m_queue.isEmpty()) {
        const QString key = m_queue.takeFirst();
        m_inFlight.insert(key);
        const QString sha = m_sha256ByKey.value(key);
        QPointer<IconFetcher> self(this);
        m_fetch(m_urlByKey.value(key), [self, key, sha](const QByteArray& data, const QString& error) {
            if (self) self->fetched(key, sha, data, error);
        });
    }
}

void IconFetcher::fetched(const QString& key, const QString& sha256, const QByteArray& data,
                          const QString& error)
{
    m_inFlight.remove(key);
    const QList<Want> waiting = m_waitingByKey.take(key);
    const QString reason = !error.isEmpty() ? error : refusal(data, sha256);
    if (!reason.isEmpty()) {
        m_refused.insert(key);
        for (const Want& w : waiting) emit iconRefused(w.name, reason);
    } else {
        const QString dataUrl = QStringLiteral("data:image/png;base64,")
                              + QString::fromLatin1(data.toBase64());
        m_dataUrlByKey.insert(key, dataUrl);
        // Fetched because the copy on disk was missing or damaged.
        const QString path = diskPath(sha256);
        if (!path.isEmpty()) {
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
        }
        if (m_model) {
            for (const Want& w : waiting) {
                m_keyByName.insert(w.name, key);
                m_model->setIcon(w.name, dataUrl, w.fullBleed);
            }
        }
    }
    startNext();
}

QString IconFetcher::diskPath(const QString& sha256) const
{
    // The hash names the file, and comes from the catalog: only a real one.
    if (m_cacheDirectory.isEmpty() || !isSha256(sha256)) return {};
    return QDir(m_cacheDirectory).filePath(sha256 + QStringLiteral(".png"));
}

void IconFetcher::defaultFetch(const QUrl& url, Done done)
{
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")) {
        done({}, QStringLiteral("not an http(s) URL"));
        return;
    }
    if (!m_network) m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > kMaxBytes || total > kMaxBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->errorString());
            return;
        }
        done(reply->read(kMaxBytes + 1), {});
    });
}

}  // namespace logos::installer
