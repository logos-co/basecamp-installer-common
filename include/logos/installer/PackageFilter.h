#pragma once

#include <QHash>
#include <QList>
#include <QSortFilterProxyModel>
#include <QStringList>

namespace logos::installer {

// Filters and sorts a PackageModel, or another PackageFilter over one. Roles
// are found by name, so filters can be chained, as App Manager chains one per
// repository section behind its search.
class PackageFilter : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum InstallState { AnyInstallState = 0, Installed = 1, NotInstalled = 2 };
    Q_ENUM(InstallState)

    explicit PackageFilter(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* sourceModel) override;

    // What the list is about. Its categories and types follow it, the
    // filters below don't. Empty: every type.
    void setTypeScope(const QStringList& types);
    QStringList typeScope() const { return m_typeScope; }
    void setExcludedNames(const QStringList& names);
    QStringList excludedNames() const { return m_excludedNames; }

    // The filters, all of which a row must pass. Text ignores case.
    // Search looks at the name, the display name and the description.
    void setSearchText(const QString& text);
    QString searchText() const { return m_searchText; }
    void setTypeFilter(const QString& type);
    QString typeFilter() const { return m_typeFilter; }
    void setCategoryFilter(const QString& category);
    QString categoryFilter() const { return m_categoryFilter; }
    // InstallState; installed means on disk.
    void setInstallStateFilter(int state);
    int installStateFilter() const { return m_installStateFilter; }
    // Rows this host can't install, which aren't installed. Hidden by default.
    void setShowUnavailable(bool show);
    bool showUnavailable() const { return m_showUnavailable; }
    void setRepositoryUrlFilter(const QString& url);
    QString repositoryUrlFilter() const { return m_repositoryUrlFilter; }
    // Only rows no catalog publishes; wins over the repository filter.
    void setLocalOnly(bool localOnly);
    bool localOnly() const { return m_localOnly; }

    // Sections (sourceKey) in this order, unknown ones last, in either sort
    // direction. Within a section: by the named role (versions by semver,
    // text ignoring case, ties by name), or with none, in the model's order.
    void setSourceOrder(const QStringList& sourceKeys);
    QStringList sourceOrder() const { return m_sourceOrder; }
    void setSortRoleByName(const QString& roleName);
    QString sortRoleName() const { return m_sortRoleName; }
    void setSortOrderInt(int order);
    int sortOrderInt() const { return static_cast<int>(m_sortOrder); }

    int count() const { return rowCount(); }
    // Of the rows in scope that "show unavailable" lets through: each
    // category once whatever its case, first letter capitalised, sorted; and
    // each type once.
    QStringList categories() const { return m_categories; }
    QStringList types() const { return m_types; }

signals:
    void countChanged();
    void categoriesChanged();
    void typesChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    int role(const QByteArray& name) const;
    QString text(const QModelIndex& sourceIndex, const QByteArray& roleName) const;
    bool inScope(const QModelIndex& sourceIndex) const;
    bool hiddenAsUnavailable(const QModelIndex& sourceIndex) const;
    void refilter();
    void resort();
    void recomputeLists();
    void updateCount();

    QStringList m_typeScope;
    QStringList m_excludedNames;
    QString m_searchText;
    QString m_typeFilter;
    QString m_categoryFilter;
    int m_installStateFilter = AnyInstallState;
    bool m_showUnavailable = false;
    QString m_repositoryUrlFilter;
    bool m_localOnly = false;
    QStringList m_sourceOrder;
    QString m_sortRoleName;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    QStringList m_categories;
    QStringList m_types;
    int m_lastCount = 0;
    mutable QHash<QByteArray, int> m_roles;
    QList<QMetaObject::Connection> m_sourceConnections;
};

}  // namespace logos::installer
