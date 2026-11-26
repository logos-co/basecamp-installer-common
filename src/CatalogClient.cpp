#include "logos/installer/CatalogClient.h"

#include "logos/installer/PackageModel.h"
#include "logos/installer/PackageModules.h"
#include "logos/installer/RepositoryLabels.h"

#include <QDebug>
#include <QPointer>
#include <QTimer>

#include <utility>

namespace logos::installer {

CatalogClient::CatalogClient(PackageModules* modules, PackageModel* model, QObject* parent)
    : QObject(parent), m_modules(modules), m_model(model), m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &CatalogClient::reload);
}

void CatalogClient::setDebounceInterval(int ms)
{
    m_debounce->setInterval(ms);
}

void CatalogClient::whenStarted(std::function<void()> then)
{
    if (m_started) {
        then();
        return;
    }
    m_waitingForStart.append(std::move(then));
    if (m_starting) return;
    m_starting = true;
    QPointer<CatalogClient> self(this);
    m_modules->startDownloader([self] {
        if (!self) return;
        self->m_starting = false;
        self->m_started = true;
        const auto waiting = std::exchange(self->m_waitingForStart, {});
        for (const auto& call : waiting) call();
    });
}

void CatalogClient::load()
{
    whenStarted([this] {
        readDownloadSource();
        readStorageState();
        reload();
    });
}

void CatalogClient::refresh()
{
    setLoading(true);
    QPointer<CatalogClient> self(this);
    whenStarted([self] {
        if (!self) return;
        // The downloader serves each repository's index from a cache for its
        // life: only this makes the next load a fetch.
        self->m_modules->refreshCatalog([self](const QString& error) {
            if (!self) return;
            if (!error.isEmpty()) qWarning() << "package_downloader.refreshCatalog:" << error;
            self->reload();
        });
    });
}

void CatalogClient::onPackagesChanged()
{
    m_debounce->start();
}

void CatalogClient::onCatalogChanged()
{
    m_debounce->start();
    readDownloadSource();
}

void CatalogClient::onStorageStateChanged(const QString& state, const QString& error)
{
    setStorage(state, error);
}

void CatalogClient::onSubscriptionArmed()
{
    readStorageState();
}

void CatalogClient::reload()
{
    m_debounce->stop();
    const int generation = ++m_generation;
    setLoading(true);
    QPointer<CatalogClient> self(this);
    const auto current = [self, generation] { return self && self->m_generation == generation; };

    // A read that failed keeps what was shown: a module that didn't answer
    // is not an empty catalog.
    const auto failed = [self, current](const char* what) {
        if (!current()) return;
        qWarning() << "CatalogClient: could not read" << what << "- keeping the last list";
        self->m_loadError = QString::fromLatin1(what);
        self->setLoading(false);
        emit self->loadFailed();
    };

    m_modules->listRepositories([self, current, failed](const QVariantList& repositories, bool read) {
        if (!current()) return;
        if (!read) return failed("the repositories");
        self->m_modules->getCatalog([self, current, failed, repositories](const QVariantList& catalog, bool read) {
            if (!current()) return;
            if (!read) return failed("the catalog");
            self->m_modules->getInstalledPackages(
                [self, current, failed, repositories, catalog](const QVariantList& installed, bool read) {
                    if (!current()) return;
                    if (!read) return failed("the installed packages");
                    self->m_modules->getValidVariants(
                        [self, current, failed, repositories, catalog, installed](const QStringList& variants,
                                                                                bool read) {
                            if (!current()) return;
                            if (!read) return failed("this host's variants");
                            self->applyRepositories(repositories);
                            self->m_model->update(catalog, installed, variants);
                            self->m_hasLoaded = true;
                            self->m_loadError.clear();
                            self->setLoading(false);
                            emit self->loaded();
                        });
                });
        });
    });
}

void CatalogClient::applyRepositories(const QVariantList& repositories)
{
    QString defaultUrl;
    for (const QVariant& v : repositories) {
        const QVariantMap r = v.toMap();
        if (r.value(QStringLiteral("isDefault")).toBool()) {
            defaultUrl = r.value(QStringLiteral("url")).toString();
            break;
        }
    }
    QVariantMap labels = logos::installer::repositoryLabels(repositories);
    labels.insert(QStringLiteral("local"), localRepositoryLabel());
    const QStringList order = logos::installer::sourceOrder(repositories);
    if (repositories == m_repositories && labels == m_repositoryLabels && order == m_sourceOrder
        && defaultUrl == m_defaultRepositoryUrl)
        return;
    m_repositories = repositories;
    m_repositoryLabels = labels;
    m_sourceOrder = order;
    m_defaultRepositoryUrl = defaultUrl;
    emit repositoriesChanged();
}

void CatalogClient::readDownloadSource()
{
    QPointer<CatalogClient> self(this);
    m_modules->getDownloadSource([self](const QString& source) {
        if (!self || source == self->m_downloadSource) return;
        self->m_downloadSource = source;
        emit self->downloadSourceChanged();
    });
}

void CatalogClient::readStorageState()
{
    QPointer<CatalogClient> self(this);
    m_modules->getStorageState([self](const QVariantMap& result) {
        if (!self || result.isEmpty()) return;
        self->setStorage(result.value(QStringLiteral("state")).toString(),
                         result.value(QStringLiteral("error")).toString());
    });
}

void CatalogClient::setStorage(const QString& state, const QString& error)
{
    if (state != m_storageState || error != m_storageError) {
        m_storageState = state;
        m_storageError = error;
        emit storageStateChanged();
    }
    // On its way up: asked again until it settles, in case an event is lost.
    const bool settling = state == QLatin1String("waiting") || state == QLatin1String("starting");
    if (!settling || m_storagePollPending) return;
    m_storagePollPending = true;
    QPointer<CatalogClient> self(this);
    QTimer::singleShot(m_storagePollMs, this, [self] {
        if (!self) return;
        self->m_storagePollPending = false;
        self->readStorageState();
    });
}

void CatalogClient::setLoading(bool loading)
{
    if (loading == m_loading) return;
    m_loading = loading;
    emit loadingChanged();
}

}  // namespace logos::installer
