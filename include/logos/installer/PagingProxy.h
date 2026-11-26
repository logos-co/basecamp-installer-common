#pragma once

#include <QAbstractProxyModel>
#include <QList>

namespace logos::installer {

// One page of its source, for a list too long to show whole. Any structural
// change in the source resets the page to the first, which also keeps the
// view from ever seeing rows outside the page.
class PagingProxy : public QAbstractProxyModel {
    Q_OBJECT

public:
    explicit PagingProxy(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* sourceModel) override;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    QHash<int, QByteArray> roleNames() const override;

    int pageSize() const { return m_pageSize; }
    // At least 1; keeps the current page within range.
    void setPageSize(int size);
    // From 1.
    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int page);
    int pageCount() const;
    // The rows of every page.
    int totalCount() const;

signals:
    void totalCountChanged();
    void currentPageChanged(int page);

private:
    void onSourceReset();
    void onSourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight,
                             const QList<int>& roles);
    int pageOffset() const { return (m_currentPage - 1) * m_pageSize; }

    int m_pageSize = 20;
    int m_currentPage = 1;
    QList<QMetaObject::Connection> m_sourceConnections;
};

}  // namespace logos::installer
