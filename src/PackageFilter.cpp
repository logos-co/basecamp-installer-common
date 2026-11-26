#include "logos/installer/PackageFilter.h"

#include "logos/installer/RowState.h"

#include <QSet>

#include <algorithm>

namespace logos::installer {

namespace {

const QList<QByteArray> kSearchRoles{"name", "displayName", "description"};
const QList<QByteArray> kVersionRoles{"version", "installedVersion", "latestVersion"};
// The roles the category and type lists depend on.
const QList<QByteArray> kListRoles{"name", "type", "category", "hasInstallableVersion",
                                   "isInstalled"};

}  // namespace

PackageFilter::PackageFilter(QObject* parent) : QSortFilterProxyModel(parent)
{
    const auto count = [this] { updateCount(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, count);
    connect(this, &QAbstractItemModel::rowsRemoved, this, count);
    connect(this, &QAbstractItemModel::modelReset, this, count);
    connect(this, &QAbstractItemModel::layoutChanged, this, count);
}

void PackageFilter::setSourceModel(QAbstractItemModel* source)
{
    for (const QMetaObject::Connection& c : m_sourceConnections) disconnect(c);
    m_sourceConnections.clear();
    m_roles.clear();
    QSortFilterProxyModel::setSourceModel(source);
    if (source) {
        const auto structural = [this] {
            m_roles.clear();
            recomputeLists();
        };
        m_sourceConnections
            << connect(source, &QAbstractItemModel::modelReset, this, structural)
            << connect(source, &QAbstractItemModel::layoutChanged, this, structural)
            << connect(source, &QAbstractItemModel::rowsInserted, this, [this] { recomputeLists(); })
            << connect(source, &QAbstractItemModel::rowsRemoved, this, [this] { recomputeLists(); })
            << connect(source, &QAbstractItemModel::dataChanged, this,
                       [this](const QModelIndex&, const QModelIndex&, const QList<int>& changed) {
                           if (!changed.isEmpty()) {
                               bool relevant = false;
                               for (const QByteArray& name : kListRoles)
                                   relevant = relevant || changed.contains(role(name));
                               if (!relevant) return;
                           }
                           recomputeLists();
                       });
    }
    resort();
    recomputeLists();
    updateCount();
}

int PackageFilter::role(const QByteArray& name) const
{
    const auto cached = m_roles.constFind(name);
    if (cached != m_roles.cend()) return *cached;
    if (!sourceModel()) return -1;
    const QHash<int, QByteArray> names = sourceModel()->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == name) {
            m_roles.insert(name, it.key());
            return it.key();
        }
    }
    return -1;
}

QString PackageFilter::text(const QModelIndex& sourceIndex, const QByteArray& roleName) const
{
    const int id = role(roleName);
    return id < 0 ? QString() : sourceModel()->data(sourceIndex, id).toString();
}

bool PackageFilter::inScope(const QModelIndex& sourceIndex) const
{
    if (!m_excludedNames.isEmpty() && m_excludedNames.contains(text(sourceIndex, "name")))
        return false;
    return m_typeScope.isEmpty()
        || m_typeScope.contains(text(sourceIndex, "type"), Qt::CaseInsensitive);
}

bool PackageFilter::hiddenAsUnavailable(const QModelIndex& sourceIndex) const
{
    if (m_showUnavailable) return false;
    const int installable = role("hasInstallableVersion");
    const int installed = role("isInstalled");
    if (installable < 0) return false;
    return !sourceModel()->data(sourceIndex, installable).toBool()
        && !(installed >= 0 && sourceModel()->data(sourceIndex, installed).toBool());
}

bool PackageFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (!sourceModel()) return false;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!inScope(idx)) return false;
    if (!m_typeFilter.isEmpty()
        && text(idx, "type").compare(m_typeFilter, Qt::CaseInsensitive) != 0)
        return false;
    if (!m_categoryFilter.isEmpty()
        && text(idx, "category").compare(m_categoryFilter, Qt::CaseInsensitive) != 0)
        return false;
    if (hiddenAsUnavailable(idx)) return false;
    if (m_installStateFilter != AnyInstallState) {
        const int installedRole = role("isInstalled");
        const bool installed = installedRole >= 0
            && sourceModel()->data(idx, installedRole).toBool();
        if (installed != (m_installStateFilter == Installed)) return false;
    }
    if (!m_searchText.isEmpty()) {
        bool hit = false;
        for (const QByteArray& name : kSearchRoles)
            hit = hit || text(idx, name).contains(m_searchText, Qt::CaseInsensitive);
        if (!hit) return false;
    }
    if (m_localOnly) return text(idx, "repositoryUrl").isEmpty();
    return m_repositoryUrlFilter.isEmpty() || text(idx, "repositoryUrl") == m_repositoryUrlFilter;
}

bool PackageFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const auto rank = [this](const QModelIndex& i) {
        const QString key = text(i, "sourceKey");
        const int pos = m_sourceOrder.indexOf(key);
        return std::pair<int, QString>{pos >= 0 ? int(pos) : int(m_sourceOrder.size()), key};
    };
    const auto a = rank(left);
    const auto b = rank(right);
    const int byKey = a.second.compare(b.second, Qt::CaseInsensitive);
    if (a.first != b.first || byKey != 0) {
        // Qt flips the result for a descending sort; sections keep their order.
        const bool ascending = a.first < b.first || (a.first == b.first && byKey < 0);
        return sortOrder() == Qt::DescendingOrder ? !ascending : ascending;
    }

    const int sortBy = m_sortRoleName.isEmpty() ? -1 : role(m_sortRoleName.toUtf8());
    if (sortBy < 0) return left.row() < right.row();

    const QVariant l = sourceModel()->data(left, sortBy);
    const QVariant r = sourceModel()->data(right, sortBy);
    if (kVersionRoles.contains(m_sortRoleName.toUtf8())) {
        const QString lv = l.toString();
        const QString rv = r.toString();
        // No version sorts lowest.
        if (lv.isEmpty() != rv.isEmpty()) return lv.isEmpty();
        if (!lv.isEmpty()) {
            const int c = versionCompare(lv, rv);
            if (c != 0) return c < 0;
        }
    } else if (l.userType() == QMetaType::QString && r.userType() == QMetaType::QString) {
        const int c = l.toString().compare(r.toString(), Qt::CaseInsensitive);
        if (c != 0) return c < 0;
    } else if (l != r) {
        return QSortFilterProxyModel::lessThan(left, right);
    }
    const int byName = text(left, "name").compare(text(right, "name"), Qt::CaseInsensitive);
    return byName != 0 ? byName < 0 : left.row() < right.row();
}

void PackageFilter::resort()
{
    const int sortBy = m_sortRoleName.isEmpty() ? -1 : role(m_sortRoleName.toUtf8());
    if (sortBy < 0 && m_sourceOrder.isEmpty()) {
        setSortRole(Qt::DisplayRole);
        sort(-1);
        return;
    }
    const int keyRole = role("sourceKey");
    setSortRole(sortBy >= 0 ? sortBy : (keyRole >= 0 ? keyRole : int(Qt::DisplayRole)));
    sort(0, sortBy >= 0 ? m_sortOrder : Qt::AscendingOrder);
}

void PackageFilter::refilter()
{
    invalidateFilter();
    updateCount();
}

void PackageFilter::recomputeLists()
{
    QStringList categories, types;
    QSet<QString> seenCategories, seenTypes;
    if (QAbstractItemModel* src = sourceModel()) {
        for (int i = 0; i < src->rowCount(); ++i) {
            const QModelIndex idx = src->index(i, 0);
            if (!inScope(idx) || hiddenAsUnavailable(idx)) continue;
            QString category = text(idx, "category");
            if (!category.isEmpty() && !seenCategories.contains(category.toLower())) {
                seenCategories.insert(category.toLower());
                category[0] = category[0].toUpper();
                categories.append(category);
            }
            const QString type = text(idx, "type");
            if (!type.isEmpty() && !seenTypes.contains(type.toLower())) {
                seenTypes.insert(type.toLower());
                types.append(type);
            }
        }
    }
    const auto ignoringCase = [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    };
    std::sort(categories.begin(), categories.end(), ignoringCase);
    std::sort(types.begin(), types.end(), ignoringCase);
    if (categories != m_categories) {
        m_categories = categories;
        emit categoriesChanged();
    }
    if (types != m_types) {
        m_types = types;
        emit typesChanged();
    }
}

void PackageFilter::updateCount()
{
    if (rowCount() == m_lastCount) return;
    m_lastCount = rowCount();
    emit countChanged();
}

void PackageFilter::setTypeScope(const QStringList& types)
{
    if (types == m_typeScope) return;
    m_typeScope = types;
    refilter();
    recomputeLists();
}

void PackageFilter::setExcludedNames(const QStringList& names)
{
    if (names == m_excludedNames) return;
    m_excludedNames = names;
    refilter();
    recomputeLists();
}

void PackageFilter::setSearchText(const QString& text)
{
    if (text == m_searchText) return;
    m_searchText = text;
    refilter();
}

void PackageFilter::setTypeFilter(const QString& type)
{
    if (type == m_typeFilter) return;
    m_typeFilter = type;
    refilter();
}

void PackageFilter::setCategoryFilter(const QString& category)
{
    if (category == m_categoryFilter) return;
    m_categoryFilter = category;
    refilter();
}

void PackageFilter::setInstallStateFilter(int state)
{
    if (state == m_installStateFilter) return;
    m_installStateFilter = state;
    refilter();
}

void PackageFilter::setShowUnavailable(bool show)
{
    if (show == m_showUnavailable) return;
    m_showUnavailable = show;
    refilter();
    recomputeLists();
}

void PackageFilter::setRepositoryUrlFilter(const QString& url)
{
    if (url == m_repositoryUrlFilter) return;
    m_repositoryUrlFilter = url;
    refilter();
}

void PackageFilter::setLocalOnly(bool localOnly)
{
    if (localOnly == m_localOnly) return;
    m_localOnly = localOnly;
    refilter();
}

void PackageFilter::setSourceOrder(const QStringList& sourceKeys)
{
    if (sourceKeys == m_sourceOrder) return;
    m_sourceOrder = sourceKeys;
    resort();
}

void PackageFilter::setSortRoleByName(const QString& roleName)
{
    if (roleName == m_sortRoleName) return;
    m_sortRoleName = roleName;
    resort();
}

void PackageFilter::setSortOrderInt(int order)
{
    const Qt::SortOrder o = order == Qt::DescendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (o == m_sortOrder) return;
    m_sortOrder = o;
    resort();
}

}  // namespace logos::installer
