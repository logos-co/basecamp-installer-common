#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QTimer;

namespace logos::installer {

class PackageModel;
class PackageModules;

// Keeps a PackageModel filled: the repositories, the catalog, the installed
// packages and this host's variants, and the downloader's download source and
// storage state. A load answers in that order, and an answer that a newer
// load overtook is dropped. A load with a read that failed changes nothing
// but loadError().
// The backend forwards the modules' events.
class CatalogClient : public QObject {
    Q_OBJECT

public:
    CatalogClient(PackageModules* modules, PackageModel* model, QObject* parent = nullptr);

    // The first time: start the downloader, which does nothing until asked.
    void load();
    // Reload: the downloader fetches every repository again first.
    void refresh();

    // package_manager installed or removed something (its *FileInstalled and
    // *Uninstalled events): load again, soon, once for a burst.
    void onPackagesChanged();
    // package_downloader's catalogChanged, which a new download source also
    // sends.
    void onCatalogChanged();
    // package_downloader's storageStateChanged.
    void onStorageStateChanged(const QString& state, const QString& error = {});
    // The subscription to package_downloader is armed: events sent before
    // were lost, so read the storage state again.
    void onSubscriptionArmed();

    bool isLoading() const { return m_loading; }
    bool hasLoaded() const { return m_hasLoaded; }
    // What the last load couldn't read ("the catalog", ...), or "" when it
    // read everything. With hasLoaded(), the list shown is the last good one.
    QString loadError() const { return m_loadError; }

    QVariantList repositories() const { return m_repositories; }
    int repositoryCount() const { return m_repositories.size(); }
    // URL to label, and "local" to its label.
    QVariantMap repositoryLabels() const { return m_repositoryLabels; }
    QStringList sourceOrder() const { return m_sourceOrder; }
    QString defaultRepositoryUrl() const { return m_defaultRepositoryUrl; }

    // "any", "logos", "http", or "" not known.
    QString downloadSource() const { return m_downloadSource; }
    // "off", "waiting", "idle", "starting", "running", "failed", or "".
    QString storageState() const { return m_storageState; }
    QString storageError() const { return m_storageError; }

    void setDebounceInterval(int ms);
    // How often a storage node on its way up is asked again, in case an event
    // is lost.
    void setStoragePollInterval(int ms) { m_storagePollMs = ms; }

signals:
    void loadingChanged();
    void loaded();
    void loadFailed();
    void repositoriesChanged();
    void downloadSourceChanged();
    void storageStateChanged();

private:
    void whenStarted(std::function<void()> then);
    void reload();
    void applyRepositories(const QVariantList& repositories);
    void readDownloadSource();
    void readStorageState();
    void setLoading(bool loading);
    void setStorage(const QString& state, const QString& error);

    PackageModules* m_modules;
    PackageModel* m_model;
    QTimer* m_debounce;
    int m_storagePollMs = 2000;

    bool m_started = false;
    bool m_starting = false;
    QList<std::function<void()>> m_waitingForStart;
    int m_generation = 0;
    bool m_loading = false;
    bool m_hasLoaded = false;
    QString m_loadError;

    QVariantList m_repositories;
    QVariantMap m_repositoryLabels;
    QStringList m_sourceOrder;
    QString m_defaultRepositoryUrl;
    QString m_downloadSource;
    QString m_storageState;
    QString m_storageError;
    bool m_storagePollPending = false;
};

}  // namespace logos::installer
