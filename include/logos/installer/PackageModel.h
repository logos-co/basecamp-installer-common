#pragma once

#include "Types.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace logos::installer {

class InstallProgress;

// The package list of both installer apps: one row per package per
// repository, and one per user-installed package no catalog publishes
// (repository "local"). Rows are a function of three inputs: the catalog,
// what is installed, and this host's variants. When one changes the rows are
// rebuilt in place, so a row that stays keeps its place, its selected version
// and its selection. What a row is doing right now (its install stage,
// download, error) comes from an InstallProgress.
class PackageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString hostVariant READ hostVariant NOTIFY hostVariantChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        ModuleNameRole,
        DisplayNameRole,
        DescriptionRole,
        TypeRole,
        CategoryRole,
        IconUrlRole,                 // what a view can show; "" until setIcon()
        CatalogIconUrlRole,          // the catalog's, for the backend to fetch
        SupportsFullBleedIconRole,
        ProvidesRole,
        WebProvidesRole,
        RepositoryUrlRole,
        RepositoryNameRole,
        RepositoryDisplayNameRole,
        OriginRepositoryUrlRole,
        OriginRepositoryNameRole,
        OriginRepositoryDisplayNameRole,
        SourceKeyRole,               // the section: see sourceKeyOf()
        AvailableVersionsRole,
        SelectedVersionIndexRole,
        VersionRole,                 // the selected version
        HashRole,
        LatestVersionRole,
        SizeRole,
        DateUpdatedRole,
        DependenciesRole,            // as text
        DependencyEntriesRole,       // as {name, version, signer}
        InstalledVersionRole,
        InstalledHashRole,
        InstallTypeRole,
        DownloadSourceRole,
        IsInstalledRole,             // on disk (and, counting dependencies, none missing)
        MissingDependenciesRole,
        InstallStatusRole,           // InstallStatus, Installing and Failed included
        RowActionRole,
        UpdateAvailableRole,
        IsVariantAvailableRole,
        IsSourceAvailableRole,
        NotAvailableReasonRole,
        HasInstallableVersionRole,
        IsSelectedRole,
        InstallStageRole,
        ErrorMessageRole,
        QueuedRole,
        DownloadReceivedRole,
        DownloadTotalRole,
        DownloadTransportRole,
        DownloadFellBackRole,
        PlanInstallStageRole,        // the whole install this row's app started
        PlanDownloadReceivedRole,
        PlanDownloadTotalRole,
        PlanDownloadTransportRole,
        PlanDownloadFellBackRole,
        CatalogIconSha256Role,       // what the fetched icon must hash to
    };
    Q_ENUM(Role)

    explicit PackageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // All three inputs at once, rebuilt once.
    void update(const QVariantList& catalogRows, const QVariantList& installedPackages,
                const QStringList& validVariants);
    // package_downloader's getCatalog().
    void setCatalog(const QVariantList& catalogRows);
    // package_manager's getInstalledPackages().
    void setInstalled(const QVariantList& installedPackages);
    // package_manager's getValidVariants(). Until it is set, every package
    // counts as available.
    void setValidVariants(const QStringList& variants);
    QStringList validVariants() const { return m_validVariants; }
    QString hostVariant() const { return m_validVariants.value(0); }

    void setProgress(InstallProgress* progress);

    // For App Manager, where an app stands for the modules it brings: once
    // the app itself is current, its status also counts each package
    // installing it would bring that is on disk, against the version it
    // would bring (packagesBroughtBy()): the most urgent of DifferentHash,
    // DowngradeAvailable and UpgradeAvailable. With a dependency missing it
    // is not installed. Its action follows its status. Off: each row is only
    // itself.
    void setStatusIncludesDependencies(bool on);
    bool statusIncludesDependencies() const { return m_statusIncludesDependencies; }

    // The dependencies keeping `name` from loading, as the host reports them.
    void setMissingDependencies(const QString& name, const QStringList& dependencies);

    // The icon a view can show for `name` (for a sandboxed view, the
    // backend's copy of catalogIconUrl).
    void setIcon(const QString& name, const QString& url, bool fullBleed);

    // Out of range selects the newest.
    void selectVersion(int row, int versionIndex);
    void setSelected(int row, bool selected);
    void clearSelection();
    QList<int> selectedRows() const;

    // The row of `name` in `repositoryUrl`; with no repository, its first row.
    int rowOf(const QString& name, const QString& repositoryUrl = {}) const;
    // Every role of `row`, by role name.
    QVariantMap rowAt(int row) const;

    // What installing `row` at its selected version brings, as Basecamp's
    // resolver plans it for App Manager: the package itself (`subject`), then
    // each package that version needs, and theirs, once each, nearest first,
    // each at the newest version this host can fetch that satisfies every
    // range asked of it, or at the version `versions` picks for it (name to
    // version; `picked`); `conflict` when the version doesn't satisfy them.
    // Then the optional packages any of them offers (`optional`, `offeredBy`,
    // `selected`: as `optional` has it by name, else when it is on disk or
    // `row` isn't), a ticked one followed by what it needs that nothing listed
    // brings (`requiredFor`: its name), and what that offers in turn. A
    // package is looked for in the row's repository, then anywhere, then
    // among what is installed. Each is {name, displayName, description,
    // version, installedVersion, size, rowAction (its own at that version),
    // installing, installStage, queued, download{Received,Total,Transport,
    // FellBack}, optional, found (false: no repository has it and it isn't
    // installed), installedOnly (on disk, in no repository: an optional one
    // is kept), and for a picker: repositoryUrl, versions (newest first),
    // versionsServed (whether this host can fetch each) and
    // selectedVersionIndex}.
    QVariantList packagesBroughtBy(int row, const QVariantMap& optional = {},
                                   const QVariantMap& versions = {}) const;

signals:
    void hostVariantChanged();
    void selectionChanged();

private:
    struct Icon { QString url; bool fullBleed = false; };

    void takeInstalled(const QVariantList& installedPackages);
    void takeValidVariants(const QStringList& variants);
    void rebuild();
    QList<QVariantMap> buildRows() const;
    void applyRows(const QList<QVariantMap>& fresh);
    void reindex();
    void emitRowsNamed(const QString& name, const QList<int>& roles);
    bool belongs(const QVariantMap& row) const;
    InstallStatus effectiveStatus(const QVariantMap& row) const;
    RowAction effectiveAction(const QVariantMap& row) const;

    QVariantList m_catalog;
    QHash<QString, QVariantMap> m_installedByName;
    QStringList m_installedOrder;
    QStringList m_validVariants;
    bool m_statusIncludesDependencies = false;
    QHash<QString, QStringList> m_missingByName;
    QHash<QString, Icon> m_iconByName;
    QHash<QString, int> m_selectedVersionByKey;
    QSet<QString> m_selectedKeys;
    QPointer<InstallProgress> m_progress;

    QList<QVariantMap> m_rows;
    QHash<QString, int> m_rowByKey;
    QMultiHash<QString, int> m_rowsByName;
};

}  // namespace logos::installer
