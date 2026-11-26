// One page of a long list.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/PackageModel.h>
#include <logos/installer/PagingProxy.h>

using namespace logos::installer;
using namespace fixtures;

namespace {

QVariantList rows(int n)
{
    QVariantList out;
    for (int i = 0; i < n; ++i)
        out.append(catalogRow("r", QStringLiteral("p%1").arg(i, 2, 10, QLatin1Char('0')), "1.0", "H"));
    return out;
}

}  // namespace

TEST(pages_slice_the_source) {
    PackageModel model;
    model.setCatalog(rows(5));
    PagingProxy paging;
    paging.setPageSize(2);
    paging.setSourceModel(&model);
    CHECK_EQ(paging.totalCount(), 5);
    CHECK_EQ(paging.pageCount(), 3);
    CHECK_EQ(names(paging), (QStringList{"p00", "p01"}));
    paging.setCurrentPage(3);
    CHECK_EQ(names(paging), QStringList{"p04"});
    paging.setCurrentPage(4);
    CHECK_EQ(paging.rowCount(), 0);
}

// Forwarding a structural change could hand the view rows outside the page.
TEST(a_structural_change_goes_back_to_the_first_page) {
    PackageModel model;
    model.setCatalog(rows(5));
    PagingProxy paging;
    paging.setPageSize(2);
    paging.setSourceModel(&model);
    paging.setCurrentPage(2);
    int resets = 0, pages = 0, totals = 0;
    QObject::connect(&paging, &QAbstractItemModel::modelReset, &paging, [&] { ++resets; });
    QObject::connect(&paging, &PagingProxy::currentPageChanged, &paging, [&] { ++pages; });
    QObject::connect(&paging, &PagingProxy::totalCountChanged, &paging, [&] { ++totals; });
    model.setCatalog(rows(6));
    CHECK(resets >= 1);
    CHECK_EQ(pages, 1);
    CHECK(totals >= 1);
    CHECK_EQ(paging.currentPage(), 1);
    CHECK_EQ(paging.totalCount(), 6);
}

TEST(a_change_on_the_page_is_forwarded_at_its_place_on_it) {
    PackageModel model;
    model.setCatalog(rows(5));
    PagingProxy paging;
    paging.setPageSize(2);
    paging.setSourceModel(&model);
    paging.setCurrentPage(2);
    QList<int> changed;
    QObject::connect(&paging, &QAbstractItemModel::dataChanged, &paging,
                     [&](const QModelIndex& from, const QModelIndex&) { changed.append(from.row()); });
    model.selectVersion(0, 0);
    model.setSelected(3, true);
    model.setSelected(4, true);
    CHECK_EQ(changed, QList<int>{1});
    CHECK(at(paging, 1, "isSelected").toBool());
}

TEST(a_smaller_page_count_keeps_the_page_in_range) {
    PackageModel model;
    model.setCatalog(rows(5));
    PagingProxy paging;
    paging.setPageSize(1);
    paging.setSourceModel(&model);
    paging.setCurrentPage(5);
    paging.setPageSize(2);
    CHECK_EQ(paging.currentPage(), 3);
    paging.setPageSize(0);
    CHECK_EQ(paging.pageSize(), 1);
}

TEST(the_roles_are_the_sources) {
    PackageModel model;
    PagingProxy paging;
    paging.setSourceModel(&model);
    CHECK_EQ(paging.roleNames(), model.roleNames());
}

CHECK_MAIN()
