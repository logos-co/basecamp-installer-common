// Filtering and sorting the package list. From logos-basecamp's
// tests/apps_filter_proxy_test.cpp and logos-package-manager-ui's
// PackagesFilterProxy, with the rules agreed for both apps: search the name,
// display name and description; installed means on disk; text ignores case.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/InstallProgress.h>
#include <logos/installer/PackageFilter.h>
#include <logos/installer/PackageModel.h>

#include <memory>

using namespace logos::installer;
using namespace fixtures;

namespace {

struct Fixture {
    Fixture() { filter.setSourceModel(&model); }
    PackageModel model;
    PackageFilter filter;
};

const QStringList kHost{QStringLiteral("linux-x86_64")};

}  // namespace

// ── Filters ──────────────────────────────────────────────────────────────

TEST(the_type_filter_ignores_case) {
    Fixture f;
    f.model.setCatalog({catalogRow("r", "a", "1.0", "H", {}, {}, "ui_qml"),
                        catalogRow("r", "b", "1.0", "H", {}, {}, "core")});
    f.filter.setTypeFilter("CORE");
    CHECK_EQ(names(f.filter), QStringList{"b"});
}

TEST(the_category_filter_ignores_case) {
    Fixture f;
    f.model.setCatalog({withCategory(catalogRow("r", "a", "1.0", "H"), "wallet"),
                        withCategory(catalogRow("r", "b", "1.0", "H"), "Chat"),
                        withCategory(catalogRow("r", "c", "1.0", "H"), "WALLET")});
    f.filter.setCategoryFilter("Wallet");
    CHECK_EQ(names(f.filter), (QStringList{"a", "c"}));
}

// People type what they see: "Token Lists" is token_list_ui's display name.
TEST(search_finds_the_display_name_the_name_and_the_description) {
    Fixture f;
    f.model.setCatalog({withDisplayName(catalogRow("r", "token_list_ui", "1.0", "H"), "Token Lists",
                                        "Device-wide token catalogue"),
                        withDisplayName(catalogRow("r", "lez_explorer_ui", "1.0", "H"), "LEZ Explorer",
                                        "Logos Execution Zone Block Explorer"),
                        catalogRow("r", "chat", "1.0", "H")});
    f.filter.setSearchText("token lists");
    CHECK_EQ(names(f.filter), QStringList{"token_list_ui"});
    f.filter.setSearchText("LEZ_EXPLORER");
    CHECK_EQ(names(f.filter), QStringList{"lez_explorer_ui"});
    f.filter.setSearchText("block explorer");
    CHECK_EQ(names(f.filter), QStringList{"lez_explorer_ui"});
    f.filter.setSearchText("");
    CHECK_EQ(f.filter.count(), 3);
}

TEST(installed_means_on_disk) {
    Fixture f;
    f.model.setCatalog({catalogRow("r", "a", "1.0", "H"), catalogRow("r", "b", "1.0", "H")});
    f.model.setInstalled({installed("a", "1.0", "H")});
    f.filter.setInstallStateFilter(PackageFilter::Installed);
    CHECK_EQ(names(f.filter), QStringList{"a"});
    f.filter.setInstallStateFilter(PackageFilter::NotInstalled);
    CHECK_EQ(names(f.filter), QStringList{"b"});
}

// A fresh install stays where it was until it is on disk, so its progress
// shows there; it doesn't vanish mid-install.
TEST(a_fresh_install_stays_under_not_installed_until_it_is_on_disk) {
    Fixture f;
    InstallProgress p;
    f.model.setProgress(&p);
    f.model.setCatalog({catalogRow("r", "a", "1.0", "H")});
    f.filter.setInstallStateFilter(PackageFilter::NotInstalled);
    p.begin("a", "1.0", "H", "a", "r");
    p.setDownloadProgress("a", 10, 100);
    CHECK_EQ(names(f.filter), QStringList{"a"});
    CHECK_EQ(roleOf(f.filter, "a", "installStatus").toInt(), toInt(InstallStatus::Installing));
    f.model.setInstalled({installed("a", "1.0", "H")});
    CHECK(names(f.filter).isEmpty());
}

TEST(excluded_names_are_left_out) {
    Fixture f;
    f.model.setCatalog({catalogRow("r", "main_ui", "1.0", "H"), catalogRow("r", "chat", "1.0", "H")});
    f.filter.setExcludedNames({"main_ui"});
    CHECK_EQ(names(f.filter), QStringList{"chat"});
}

TEST(the_repository_filter_keeps_one_repository) {
    Fixture f;
    f.model.setCatalog({catalogRow("repo1", "a", "1.0", "H"), catalogRow("repo2", "a", "1.0", "H"),
                        catalogRow("repo2", "b", "1.0", "H")});
    f.filter.setRepositoryUrlFilter("repo2");
    CHECK_EQ(names(f.filter), (QStringList{"a", "b"}));
    CHECK_EQ(at(f.filter, 0, "repositoryUrl").toString(), QStringLiteral("repo2"));
}

TEST(local_only_keeps_what_no_catalog_publishes_whatever_the_repository_filter) {
    Fixture f;
    f.model.setCatalog({catalogRow("repo1", "a", "1.0", "H")});
    f.model.setInstalled({installed("orphan", "1.0", "H")});
    f.filter.setRepositoryUrlFilter("repo1");
    f.filter.setLocalOnly(true);
    CHECK_EQ(names(f.filter), QStringList{"orphan"});
}

TEST(filters_combine) {
    Fixture f;
    f.model.setCatalog({withCategory(catalogRow("r", "wallet_a", "1.0", "H"), "wallet"),
                        withCategory(catalogRow("r", "wallet_b", "1.0", "H", {}, {}, "core"), "wallet"),
                        withCategory(catalogRow("r", "chat_a", "1.0", "H"), "chat")});
    f.filter.setCategoryFilter("wallet");
    f.filter.setTypeFilter("ui_qml");
    f.filter.setSearchText("wallet");
    CHECK_EQ(names(f.filter), QStringList{"wallet_a"});
}

TEST(the_count_follows_the_rows_shown) {
    Fixture f;
    f.model.setCatalog({catalogRow("r", "a", "1.0", "H"), catalogRow("r", "b", "1.0", "H")});
    int changes = 0;
    QObject::connect(&f.filter, &PackageFilter::countChanged, &f.filter, [&] { ++changes; });
    f.filter.setSearchText("a");
    CHECK(changes >= 1);
    CHECK_EQ(f.filter.count(), 1);
    f.model.setCatalog({catalogRow("r", "a", "1.0", "H"), catalogRow("r", "ab", "1.0", "H")});
    CHECK_EQ(f.filter.count(), 2);
}

// ── What this host can't install ─────────────────────────────────────────

TEST(packages_for_other_hosts_are_hidden_by_default) {
    Fixture f;
    f.model.setValidVariants(kHost);
    f.model.setCatalog({catalogRow("r", "mine", "1.0", "H", {}, kHost),
                        catalogRow("r", "foreign", "1.0", "H", {}, {"windows-x86_64"})});
    CHECK_EQ(names(f.filter), QStringList{"mine"});
    f.filter.setShowUnavailable(true);
    CHECK_EQ(f.filter.count(), 2);
}

// A catalog that drops this host's build must not take the uninstall with it.
TEST(an_installed_package_is_never_hidden_as_unavailable) {
    Fixture f;
    f.model.setValidVariants(kHost);
    f.model.setCatalog({catalogRow("r", "gone", "1.0", "H", {}, {"windows-x86_64"})});
    CHECK_EQ(f.filter.count(), 0);
    f.model.setInstalled({installed("gone", "1.0", "H")});
    CHECK_EQ(f.filter.count(), 1);
}

TEST(showing_unavailable_combines_with_the_install_state) {
    Fixture f;
    f.model.setValidVariants(kHost);
    f.model.setCatalog({catalogRow("r", "mine", "1.0", "H", {}, kHost),
                        catalogRow("r", "foreign", "1.0", "H", {}, {"windows-x86_64"})});
    f.filter.setShowUnavailable(true);
    f.filter.setInstallStateFilter(PackageFilter::NotInstalled);
    CHECK_EQ(f.filter.count(), 2);
}

// A sideloaded app publishes no variants; it must stay listed.
TEST(a_local_package_stays_listed) {
    Fixture f;
    f.model.setValidVariants(kHost);
    f.model.setInstalled({installed("sideloaded", "1.0", "H")});
    CHECK_EQ(names(f.filter), QStringList{"sideloaded"});
}

// App Manager chains a section filter behind its search: a chained filter
// left at the default hides again what the first one showed.
TEST(a_chained_filter_must_let_the_first_decide) {
    Fixture f;
    f.model.setValidVariants(kHost);
    f.model.setCatalog({catalogRow("r", "foreign", "1.0", "H", {}, {"windows-x86_64"})});
    f.filter.setShowUnavailable(true);
    PackageFilter section;
    section.setSourceModel(&f.filter);
    CHECK_EQ(section.count(), 0);
    section.setShowUnavailable(true);
    CHECK_EQ(section.count(), 1);
    section.setRepositoryUrlFilter("r");
    CHECK_EQ(names(section), QStringList{"foreign"});
}

TEST(the_filter_follows_variants_that_arrive_late) {
    Fixture f;
    f.model.setCatalog({catalogRow("r", "foreign", "1.0", "H", {}, {"windows-x86_64"})});
    CHECK_EQ(f.filter.count(), 1);
    f.model.setValidVariants(kHost);
    CHECK_EQ(f.filter.count(), 0);
}

// ── The category and type lists ──────────────────────────────────────────

TEST(categories_are_listed_once_whatever_their_case) {
    Fixture f;
    f.model.setCatalog({withCategory(catalogRow("r", "a", "1.0", "H"), "DeFi"),
                        withCategory(catalogRow("r", "b", "1.0", "H"), "defi"),
                        withCategory(catalogRow("r", "c", "1.0", "H"), "chat"),
                        catalogRow("r", "d", "1.0", "H")});
    CHECK_EQ(f.filter.categories(), (QStringList{"Chat", "DeFi"}));
    f.filter.setCategoryFilter("DeFi");
    CHECK_EQ(names(f.filter), (QStringList{"a", "b"}));
}

// The lists follow the list's scope, not its filters: picking a type or
// typing doesn't take categories away.
TEST(categories_follow_the_scope_not_the_filters) {
    Fixture f;
    f.model.setCatalog({withCategory(catalogRow("r", "app", "1.0", "H", {}, {}, "ui_qml"), "wallet"),
                        withCategory(catalogRow("r", "mod", "1.0", "H", {}, {}, "core"), "network")});
    CHECK_EQ(f.filter.categories(), (QStringList{"Network", "Wallet"}));
    CHECK_EQ(f.filter.types(), (QStringList{"core", "ui_qml"}));
    f.filter.setTypeFilter("core");
    f.filter.setSearchText("mod");
    CHECK_EQ(f.filter.categories(), (QStringList{"Network", "Wallet"}));

    int changes = 0;
    QObject::connect(&f.filter, &PackageFilter::categoriesChanged, &f.filter, [&] { ++changes; });
    f.filter.setTypeScope({"ui_qml"});
    CHECK_EQ(changes, 1);
    CHECK_EQ(f.filter.categories(), QStringList{"Wallet"});
    CHECK_EQ(f.filter.types(), QStringList{"ui_qml"});
}

// A category with nothing installable here isn't offered, unless unavailable
// packages are shown.
TEST(categories_follow_show_unavailable) {
    Fixture f;
    f.model.setValidVariants(kHost);
    f.model.setCatalog({withCategory(catalogRow("r", "mine", "1.0", "H", {}, kHost), "chat"),
                        withCategory(catalogRow("r", "foreign", "1.0", "H", {}, {"windows-x86_64"}), "games")});
    CHECK_EQ(f.filter.categories(), QStringList{"Chat"});
    f.filter.setShowUnavailable(true);
    CHECK_EQ(f.filter.categories(), (QStringList{"Chat", "Games"}));
}

TEST(categories_change_with_the_catalog_only_when_they_differ) {
    Fixture f;
    f.model.setCatalog({withCategory(catalogRow("r", "a", "1.0", "H"), "chat")});
    int changes = 0;
    QObject::connect(&f.filter, &PackageFilter::categoriesChanged, &f.filter, [&] { ++changes; });
    f.model.setCatalog({withCategory(catalogRow("r", "a", "1.1", "H2"), "chat")});
    CHECK_EQ(changes, 0);
    f.model.setCatalog({withCategory(catalogRow("r", "a", "1.1", "H2"), "chat"),
                        withCategory(catalogRow("r", "b", "1.0", "H"), "wallet")});
    CHECK_EQ(changes, 1);
}

// ── Sorting ──────────────────────────────────────────────────────────────

TEST(sections_follow_the_source_order_and_keep_the_models_order_inside) {
    Fixture f;
    f.model.setCatalog({catalogRow("user", "z", "1.0", "H"), catalogRow("default", "y", "1.0", "H"),
                        catalogRow("user", "a", "1.0", "H"), catalogRow("default", "b", "1.0", "H")});
    f.model.setInstalled({installed("orphan", "1.0", "H")});
    f.filter.setSourceOrder({"default", "user", "local"});
    CHECK_EQ(names(f.filter), (QStringList{"y", "b", "z", "a", "orphan"}));
}

TEST(within_a_section_rows_sort_by_the_role_ignoring_case) {
    Fixture f;
    f.model.setCatalog({withDisplayName(catalogRow("r1", "a", "1.0", "H"), "beta"),
                        withDisplayName(catalogRow("r1", "b", "1.0", "H"), "Alpha"),
                        withDisplayName(catalogRow("r0", "c", "1.0", "H"), "zulu")});
    f.filter.setSourceOrder({"r0", "r1"});
    f.filter.setSortRoleByName("displayName");
    CHECK_EQ(names(f.filter), (QStringList{"c", "b", "a"}));
    // Descending reverses inside the sections, not the sections.
    f.filter.setSortOrderInt(Qt::DescendingOrder);
    CHECK_EQ(names(f.filter), (QStringList{"c", "a", "b"}));
}

// 0.10.0 after 0.2.0, and no version lowest.
TEST(versions_sort_by_semver) {
    Fixture f;
    f.model.setCatalog({catalogRow("r", "a", "0.10.0", "H"), catalogRow("r", "b", "0.2.0", "H"),
                        catalogRow("r", "c", "0.2.0-rc.1", "H"), catalogRow("r", "d", "1.0", "H")});
    f.model.setInstalled({installed("a", "0.10.0", "H"), installed("b", "0.2.0", "H"),
                          installed("c", "0.2.0-rc.1", "H")});
    f.filter.setSortRoleByName("installedVersion");
    CHECK_EQ(names(f.filter), (QStringList{"d", "c", "b", "a"}));
}

TEST(ties_order_by_name) {
    Fixture f;
    f.model.setCatalog({withCategory(catalogRow("r", "b", "1.0", "H"), "chat"),
                        withCategory(catalogRow("r", "a", "1.0", "H"), "chat")});
    f.filter.setSortRoleByName("category");
    CHECK_EQ(names(f.filter), (QStringList{"a", "b"}));
}

TEST(without_an_order_or_a_role_the_models_order_stays) {
    Fixture f;
    f.model.setCatalog({catalogRow("r2", "b", "1.0", "H"), catalogRow("r1", "a", "1.0", "H")});
    CHECK_EQ(names(f.filter), (QStringList{"b", "a"}));
    f.filter.setSortRoleByName("name");
    CHECK_EQ(names(f.filter), (QStringList{"a", "b"}));
    f.filter.setSortRoleByName("");
    CHECK_EQ(names(f.filter), (QStringList{"b", "a"}));
}

CHECK_MAIN()
