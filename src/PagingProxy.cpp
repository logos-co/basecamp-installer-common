#include "logos/installer/PagingProxy.h"

#include <algorithm>

namespace logos::installer {

PagingProxy::PagingProxy(QObject* parent) : QAbstractProxyModel(parent) {}

void PagingProxy::setSourceModel(QAbstractItemModel* source)
{
    if (sourceModel() == source) return;
    beginResetModel();
    for (const QMetaObject::Connection& c : m_sourceConnections) disconnect(c);
    m_sourceConnections.clear();
    QAbstractProxyModel::setSourceModel(source);
    if (source) {
        // Structural changes reset the page: forwarding them would hand the
        // view rows outside it.
        const auto reset = [this] { onSourceReset(); };
        m_sourceConnections
            << connect(source, &QAbstractItemModel::modelReset, this, reset)
            << connect(source, &QAbstractItemModel::rowsInserted, this, reset)
            << connect(source, &QAbstractItemModel::rowsRemoved, this, reset)
            << connect(source, &QAbstractItemModel::rowsMoved, this, reset)
            << connect(source, &QAbstractItemModel::layoutChanged, this, reset)
            << connect(source, &QAbstractItemModel::dataChanged, this,
                       &PagingProxy::onSourceDataChanged);
    }
    m_currentPage = 1;
    endResetModel();
    emit totalCountChanged();
}

void PagingProxy::onSourceReset()
{
    beginResetModel();
    const bool pageChanged = m_currentPage != 1;
    m_currentPage = 1;
    endResetModel();
    if (pageChanged) emit currentPageChanged(m_currentPage);
    emit totalCountChanged();
}

void PagingProxy::onSourceDataChanged(const QModelIndex& topLeft,
                                      const QModelIndex& bottomRight, const QList<int>& roles)
{
    const int offset = pageOffset();
    const int first = std::max(topLeft.row(), offset);
    const int last = std::min(bottomRight.row(), offset + rowCount() - 1);
    if (first > last) return;
    emit dataChanged(index(first - offset, topLeft.column()),
                     index(last - offset, bottomRight.column()), roles);
}

int PagingProxy::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !sourceModel()) return 0;
    const int remaining = sourceModel()->rowCount() - pageOffset();
    return remaining <= 0 ? 0 : std::min(m_pageSize, remaining);
}

int PagingProxy::columnCount(const QModelIndex&) const
{
    return sourceModel() ? sourceModel()->columnCount() : 1;
}

QModelIndex PagingProxy::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex PagingProxy::parent(const QModelIndex&) const
{
    return {};
}

QModelIndex PagingProxy::mapToSource(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid() || !sourceModel()) return {};
    return sourceModel()->index(proxyIndex.row() + pageOffset(), proxyIndex.column());
}

QModelIndex PagingProxy::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid()) return {};
    const int row = sourceIndex.row() - pageOffset();
    if (row < 0 || row >= rowCount()) return {};
    return createIndex(row, sourceIndex.column());
}

QHash<int, QByteArray> PagingProxy::roleNames() const
{
    return sourceModel() ? sourceModel()->roleNames() : QAbstractProxyModel::roleNames();
}

int PagingProxy::totalCount() const
{
    return sourceModel() ? sourceModel()->rowCount() : 0;
}

int PagingProxy::pageCount() const
{
    return std::max(1, (totalCount() + m_pageSize - 1) / m_pageSize);
}

void PagingProxy::setPageSize(int size)
{
    const int v = std::max(1, size);
    if (v == m_pageSize) return;
    beginResetModel();
    m_pageSize = v;
    const bool pageChanged = m_currentPage > pageCount();
    if (pageChanged) m_currentPage = pageCount();
    endResetModel();
    if (pageChanged) emit currentPageChanged(m_currentPage);
}

void PagingProxy::setCurrentPage(int page)
{
    const int v = std::max(1, page);
    if (v == m_currentPage) return;
    beginResetModel();
    m_currentPage = v;
    endResetModel();
    emit currentPageChanged(m_currentPage);
}

}  // namespace logos::installer
