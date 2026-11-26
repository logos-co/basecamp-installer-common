// The package list both installer apps show. The status cases come from
// logos-basecamp's tests/apps_model_test.cpp, the progress and queue cases
// from logos-package-manager-ui's tests/test_package_list_model.cpp.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/InstallProgress.h>
#include <logos/installer/PackageModel.h>

using namespace logos::installer;
using namespace fixtures;

namespace {

InstallStatus statusOf(const PackageModel& model, const QString& name, const QString& repo)
{
    return static_cast<InstallStatus>(roleOf(model, name, "installStatus", repo).toInt());
}

RowAction actionOf(const PackageModel& model, const QString& name, const QString& repo = {})
{
    return static_cast<RowAction>(roleOf(model, name, "rowAction", repo).toInt());
}

// App Manager's list: an app's status counts its dependencies.
struct Apps : PackageModel {
    Apps() { setStatusIncludesDependencies(true); }
};

// A parent and its dependency, both in repo1.
void radicle(PackageModel& model, const QString& moduleVersion, const QString& moduleHash,
             const QString& appVersion = "0.2.3", const QString& appHash = "H_radicle_023")
{
    model.setCatalog({catalogRow("repo1", "radicle", "0.2.3", "H_radicle_023", {dep("radicle_module")}),
                      catalogRow("repo1", "radicle_module", "0.2.3", "H_mod_023")});
    model.setInstalled({installed("radicle", appVersion, appHash),
                        installed("radicle_module", moduleVersion, moduleHash)});
}

struct Recorder {
    explicit Recorder(QAbstractItemModel& m)
    {
        QObject::connect(&m, &QAbstractItemModel::modelReset, &m, [this] { ++resets; });
        QObject::connect(&m, &QAbstractItemModel::rowsInserted, &m, [this] { ++inserts; });
        QObject::connect(&m, &QAbstractItemModel::rowsRemoved, &m, [this] { ++removals; });
        QObject::connect(&m, &QAbstractItemModel::dataChanged, &m,
                         [this](const QModelIndex& from, const QModelIndex&, const QList<int>& r) {
                             changedRows.append(from.row());
                             changedRoles.append(r);
                         });
    }
    int resets = 0, inserts = 0, removals = 0;
    QList<int> changedRows;
    QList<QList<int>> changedRoles;
};

}  // namespace

// ── Status against disk ──────────────────────────────────────────────────

TEST(not_installed_by_default) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::NotInstalled);
    CHECK(!roleOf(model, "wallet_ui", "isInstalled").toBool());
}

TEST(installed_upgrade_downgrade_and_different_hash) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "a", "1.0", "H"), catalogRow("repo1", "b", "2.0", "H2"),
                      catalogRow("repo1", "c", "1.0", "H1"), catalogRow("repo1", "d", "1.0", "H_repo")});
    model.setInstalled({installed("a", "1.0", "H"), installed("b", "1.0", "H1"),
                        installed("c", "2.0", "H2"), installed("d", "1.0", "H_other")});
    CHECK_EQ(statusOf(model, "a", "repo1"), InstallStatus::Installed);
    CHECK_EQ(statusOf(model, "b", "repo1"), InstallStatus::UpgradeAvailable);
    CHECK_EQ(statusOf(model, "c", "repo1"), InstallStatus::DowngradeAvailable);
    CHECK_EQ(statusOf(model, "d", "repo1"), InstallStatus::DifferentHash);
    CHECK(roleOf(model, "a", "isInstalled").toBool());
    CHECK(roleOf(model, "b", "updateAvailable").toBool());
    CHECK(!roleOf(model, "c", "updateAvailable").toBool());
}

TEST(two_repositories_with_the_same_build_are_both_installed) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_shared"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H_shared")});
    model.setInstalled({installed("wallet_ui", "1.0", "H_shared")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Installed);
    CHECK_EQ(statusOf(model, "wallet_ui", "repo2"), InstallStatus::Installed);
}

TEST(the_other_repositorys_build_is_a_different_hash) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H1"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H2")});
    model.setInstalled({installed("wallet_ui", "1.0", "H1")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Installed);
    CHECK_EQ(statusOf(model, "wallet_ui", "repo2"), InstallStatus::DifferentHash);
}

TEST(uninstalling_returns_to_not_installed) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H_ui")});
    model.setInstalled({installed("wallet_ui", "1.0", "H_ui")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Installed);
    model.setInstalled({});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::NotInstalled);
    CHECK_EQ(statusOf(model, "wallet_ui", "repo2"), InstallStatus::NotInstalled);
}

// ── An app counts its dependencies (App Manager) ─────────────────────────

TEST(an_outdated_dependency_makes_its_app_upgradeable) {
    Apps model;
    radicle(model, "0.2.2", "H_mod_022");
    CHECK_EQ(statusOf(model, "radicle_module", "repo1"), InstallStatus::UpgradeAvailable);
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::UpgradeAvailable);
    CHECK_EQ(actionOf(model, "radicle"), RowAction::Upgrade);
}

TEST(a_newer_dependency_makes_its_app_downgradeable) {
    Apps model;
    radicle(model, "0.3.0", "H_mod_030");
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::DowngradeAvailable);
    CHECK_EQ(actionOf(model, "radicle"), RowAction::Downgrade);
}

TEST(a_damaged_dependency_makes_its_app_reinstallable) {
    Apps model;
    radicle(model, "0.2.3", "H_mod_TAMPERED");
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::DifferentHash);
    CHECK_EQ(actionOf(model, "radicle"), RowAction::Reinstall);
}

TEST(current_dependencies_leave_the_app_installed) {
    Apps model;
    radicle(model, "0.2.3", "H_mod_023");
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::Installed);
    CHECK_EQ(actionOf(model, "radicle"), RowAction::NoOp);
}

// No version change repairs a bad payload.
TEST(the_most_urgent_dependency_wins) {
    Apps model;
    model.setCatalog({catalogRow("repo1", "radicle", "0.2.3", "H_r", {dep("mod_old"), dep("mod_broken")}),
                      catalogRow("repo1", "mod_old", "0.2.3", "H_old_023"),
                      catalogRow("repo1", "mod_broken", "0.2.3", "H_broken_023")});
    model.setInstalled({installed("radicle", "0.2.3", "H_r"),
                        installed("mod_old", "0.2.2", "H_old_022"),
                        installed("mod_broken", "0.2.3", "H_broken_TAMPERED")});
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::DifferentHash);
}

TEST(the_apps_own_state_comes_first) {
    Apps model;
    radicle(model, "0.2.3", "H_mod_TAMPERED", "0.2.2", "H_radicle_022");
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::UpgradeAvailable);
}

// Each repository's app is judged by its own repository's dependency.
TEST(a_dependency_from_another_repository_counts_for_its_app_only) {
    Apps model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui", {dep("wallet_modules")}),
                      catalogRow("repo2", "wallet_ui", "1.0", "H_ui", {dep("wallet_modules")}),
                      catalogRow("repo1", "wallet_modules", "1.0", "H_mods_repo1"),
                      catalogRow("repo2", "wallet_modules", "1.0", "H_mods_repo2")});
    model.setInstalled({installed("wallet_ui", "1.0", "H_ui"),
                        installed("wallet_modules", "1.0", "H_mods_repo1")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Installed);
    CHECK_EQ(statusOf(model, "wallet_ui", "repo2"), InstallStatus::DifferentHash);

    model.setInstalled({installed("wallet_ui", "1.0", "H_ui"),
                        installed("wallet_modules", "1.0", "H_mods_repo2")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::DifferentHash);
    CHECK_EQ(statusOf(model, "wallet_ui", "repo2"), InstallStatus::Installed);
}

// repo2 doesn't publish the dependency: installing its app would bring
// repo1's, so that is what it is judged against.
TEST(a_dependency_the_apps_repository_doesnt_publish_is_judged_as_installing_would_bring_it) {
    Apps model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui", {dep("wallet_modules")}),
                      catalogRow("repo2", "wallet_ui", "1.0", "H_ui", {dep("wallet_modules")}),
                      catalogRow("repo1", "wallet_modules", "1.1", "H_mods11")});
    model.setInstalled({installed("wallet_ui", "1.0", "H_ui"),
                        installed("wallet_modules", "1.0", "H_mods10")});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::UpgradeAvailable);
    CHECK_EQ(statusOf(model, "wallet_ui", "repo2"), InstallStatus::UpgradeAvailable);
}

// Basecamp's old roll-up compared a dependency with its newest release,
// whatever range the app asked for: an app holding one back said Upgrade
// for good, though installing it changed nothing.
TEST(a_dependency_its_range_holds_back_leaves_the_app_installed) {
    Apps model;
    model.setCatalog({catalogRow("repo1", "token_ui", "1.0", "H_ui", {dep("lez_core", "0.4.3")}),
                      withOlderVersion(catalogRow("repo1", "lez_core", "0.5.1", "H_051", {}, {}, "core"),
                                       "0.4.3", "H_043")});
    model.setInstalled({installed("token_ui", "1.0", "H_ui"), installed("lez_core", "0.4.3", "H_043")});
    CHECK_EQ(statusOf(model, "token_ui", "repo1"), InstallStatus::Installed);
    CHECK_EQ(actionOf(model, "token_ui"), RowAction::NoOp);
    // Its newest within the range is newer: that is an upgrade.
    model.setCatalog({catalogRow("repo1", "token_ui", "1.0", "H_ui", {dep("lez_core", "~0.4.0")}),
                      withOlderVersion(withOlderVersion(catalogRow("repo1", "lez_core", "0.5.1", "H_051", {}, {},
                                                                   "core"), "0.4.4", "H_044"), "0.4.3", "H_043")});
    CHECK_EQ(statusOf(model, "token_ui", "repo1"), InstallStatus::UpgradeAvailable);
}

TEST(an_app_missing_a_dependency_is_not_installed) {
    Apps model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui", {dep("wallet_modules")})});
    model.setInstalled({installed("wallet_ui", "1.0", "H_ui")});
    model.setMissingDependencies("wallet_ui", {"wallet_modules"});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::NotInstalled);
    CHECK(!roleOf(model, "wallet_ui", "isInstalled").toBool());
    CHECK_EQ(actionOf(model, "wallet_ui"), RowAction::Install);
    CHECK_EQ(roleOf(model, "wallet_ui", "missingDependencies").toStringList(),
             QStringList{"wallet_modules"});

    model.setMissingDependencies("wallet_ui", {});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Installed);
}

// Package Manager lists the modules: each row is only itself.
TEST(without_counting_dependencies_each_row_is_itself) {
    PackageModel model;
    radicle(model, "0.2.2", "H_mod_022");
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::Installed);
    CHECK_EQ(statusOf(model, "radicle_module", "repo1"), InstallStatus::UpgradeAvailable);
    model.setMissingDependencies("radicle", {"radicle_module"});
    CHECK_EQ(statusOf(model, "radicle", "repo1"), InstallStatus::Installed);
    CHECK(roleOf(model, "radicle", "isInstalled").toBool());
}

// ── Actions ──────────────────────────────────────────────────────────────

TEST(the_action_is_for_the_selected_version) {
    PackageModel model;
    model.setCatalog({withOlderVersion(catalogRow("repo1", "chat", "1.2.0", "H12"), "1.1.0", "H11")});
    model.setInstalled({installed("chat", "1.1.0", "H11")});
    CHECK_EQ(actionOf(model, "chat"), RowAction::Upgrade);
    model.selectVersion(0, 1);
    CHECK_EQ(actionOf(model, "chat"), RowAction::NoOp);
    CHECK_EQ(roleOf(model, "chat", "version").toString(), QStringLiteral("1.1.0"));
    CHECK_EQ(statusOf(model, "chat", "repo1"), InstallStatus::UpgradeAvailable);
}

TEST(an_app_with_nothing_for_this_host_offers_nothing_to_change) {
    Apps model;
    model.setValidVariants({"linux-x86_64"});
    model.setCatalog({catalogRow("repo1", "a", "2.0", "H2", {}, {"windows-x86_64"}),
                      catalogRow("repo1", "b", "1.0", "H", {}, {"windows-x86_64"})});
    model.setInstalled({installed("a", "1.0", "H1")});
    CHECK_EQ(statusOf(model, "a", "repo1"), InstallStatus::UpgradeAvailable);
    CHECK_EQ(actionOf(model, "a"), RowAction::NoOp);
    CHECK_EQ(actionOf(model, "b"), RowAction::NotAvailable);
}

// ── What a row is doing now ──────────────────────────────────────────────

TEST(a_row_installing_offers_nothing_and_a_failed_one_retry) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H")});
    CHECK_EQ(actionOf(model, "wallet_ui"), RowAction::Install);

    p.begin("wallet_ui", "1.0", "H", "wallet_ui", "repo1");
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Installing);
    CHECK_EQ(actionOf(model, "wallet_ui"), RowAction::NoOp);

    p.fail("wallet_ui", "signature changed");
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Failed);
    CHECK_EQ(actionOf(model, "wallet_ui"), RowAction::Retry);
    CHECK_EQ(roleOf(model, "wallet_ui", "errorMessage").toString(), QStringLiteral("signature changed"));

    p.clear("wallet_ui");
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::NotInstalled);
    CHECK_EQ(roleOf(model, "wallet_ui", "errorMessage").toString(), QString());
}

// A failure stays until the install side clears it, whatever refreshes.
TEST(a_failure_survives_a_refresh) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H")});
    p.begin("wallet_ui", "1.0", "H", "wallet_ui", "repo1");
    p.fail("wallet_ui", "no peers");
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H")});
    model.setInstalled({});
    CHECK_EQ(statusOf(model, "wallet_ui", "repo1"), InstallStatus::Failed);
}

// Waiting behind another install: "Queued" until its own starts.
TEST(a_queued_row_is_installing_and_queued) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "blockchain_ui", "1.0", "H"),
                      catalogRow("repo1", "chat", "1.0", "H")});
    p.beginPlan("blockchain_ui", {{"blockchain_ui", "1.0", "H", 100, "repo1"}});
    CHECK_EQ(statusOf(model, "blockchain_ui", "repo1"), InstallStatus::Installing);
    CHECK(roleOf(model, "blockchain_ui", "queued").toBool());
    CHECK(!roleOf(model, "chat", "queued").toBool());

    p.setDownloadProgress("blockchain_ui", 10, 100, "http");
    CHECK(!roleOf(model, "blockchain_ui", "queued").toBool());
}

// The stage repaints the roles that follow it.
TEST(a_stage_change_repaints_status_action_and_stage) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H")});
    Recorder r(model);
    p.begin("wallet_ui", "1.0", "H", "wallet_ui", "repo1");
    bool repainted = false;
    for (const QList<int>& roles : r.changedRoles) {
        repainted = repainted || (roles.contains(PackageModel::InstallStatusRole)
                                  && roles.contains(PackageModel::RowActionRole)
                                  && roles.contains(PackageModel::InstallStageRole));
    }
    CHECK(repainted);
}

// ── Packages no catalog publishes ────────────────────────────────────────

TEST(a_user_installed_package_no_catalog_publishes_is_local) {
    PackageModel model;
    model.setInstalled({installed("side_loaded_module", "1.2.3", "H_side", "user", "utilities")});
    CHECK_EQ(model.rowCount(), 1);
    CHECK_EQ(at(model, 0, "repositoryUrl").toString(), QString());
    CHECK_EQ(at(model, 0, "sourceKey").toString(), QStringLiteral("local"));
    CHECK_EQ(at(model, 0, "category").toString(), QStringLiteral("utilities"));
    CHECK_EQ(at(model, 0, "installedVersion").toString(), QStringLiteral("1.2.3"));
    CHECK_EQ(at(model, 0, "installStatus").toInt(), toInt(InstallStatus::Installed));
    CHECK(at(model, 0, "isInstalled").toBool());
}

TEST(a_package_a_catalog_publishes_gets_no_local_row) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_repo")});
    model.setInstalled({installed("wallet_ui", "1.0", "H_repo"), installed("orphan_mod", "1.0", "H")});
    CHECK_EQ(model.rowCount(), 2);
    CHECK(rowOf(model, "wallet_ui", "repo1") >= 0);
    CHECK_EQ(roleOf(model, "orphan_mod", "repositoryUrl").toString(), QString());
}

TEST(embedded_packages_get_no_local_row) {
    PackageModel model;
    model.setInstalled({installed("embedded_mod", "1.0", "H", "embedded"),
                        installed("user_mod", "2.0", "H", "user")});
    CHECK_EQ(names(model), QStringList{"user_mod"});
}

TEST(an_uninstalled_local_package_goes) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui")});
    model.setInstalled({installed("orphan_mod", "1.0", "H")});
    CHECK_EQ(model.rowCount(), 2);
    model.setInstalled({});
    CHECK_EQ(names(model), QStringList{"wallet_ui"});
}

TEST(a_catalog_adopting_a_local_package_replaces_its_row) {
    PackageModel model;
    model.setInstalled({installed("wallet_ui", "1.0", "H_installed")});
    CHECK_EQ(roleOf(model, "wallet_ui", "repositoryUrl").toString(), QString());
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_installed")});
    CHECK_EQ(model.rowCount(), 1);
    CHECK_EQ(roleOf(model, "wallet_ui", "repositoryUrl").toString(), QStringLiteral("repo1"));
}

// ── This host ────────────────────────────────────────────────────────────

// package_manager answers after the catalog: until then, nothing is blanked.
TEST(until_the_host_is_known_everything_is_available) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "foreign", "1.0", "H", {}, {"windows-x86_64"})});
    CHECK(roleOf(model, "foreign", "hasInstallableVersion").toBool());
    CHECK_EQ(actionOf(model, "foreign"), RowAction::Install);

    int hostChanged = 0;
    QObject::connect(&model, &PackageModel::hostVariantChanged, &model, [&] { ++hostChanged; });
    model.setValidVariants({"linux-x86_64"});
    CHECK_EQ(hostChanged, 1);
    CHECK_EQ(model.hostVariant(), QStringLiteral("linux-x86_64"));
    CHECK(!roleOf(model, "foreign", "hasInstallableVersion").toBool());
    CHECK_EQ(roleOf(model, "foreign", "notAvailableReason").toInt(),
             toInt(NotAvailableReason::PlatformMismatch));
    CHECK_EQ(actionOf(model, "foreign"), RowAction::NotAvailable);
}

// ── Rebuilt in place ─────────────────────────────────────────────────────

TEST(a_refresh_moves_and_resets_nothing_that_stays) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "a", "1.0", "H"), catalogRow("repo1", "b", "1.0", "H"),
                      catalogRow("repo1", "c", "1.0", "H")});
    Recorder r(model);
    model.setCatalog({catalogRow("repo1", "a", "1.0", "H"), catalogRow("repo1", "c", "1.1", "H2"),
                      catalogRow("repo1", "d", "1.0", "H")});
    CHECK_EQ(r.resets, 0);
    CHECK_EQ(r.removals, 1);
    CHECK_EQ(r.inserts, 1);
    CHECK_EQ(names(model), (QStringList{"a", "c", "d"}));
    CHECK_EQ(r.changedRows, QList<int>{1});
    CHECK_EQ(roleOf(model, "c", "version").toString(), QStringLiteral("1.1"));
}

TEST(an_unchanged_refresh_changes_nothing) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "a", "1.0", "H")});
    model.setInstalled({installed("a", "1.0", "H")});
    Recorder r(model);
    model.setCatalog({catalogRow("repo1", "a", "1.0", "H")});
    model.setInstalled({installed("a", "1.0", "H")});
    CHECK_EQ(r.changedRows.size() + r.inserts + r.removals + r.resets, 0);
}

TEST(a_selected_version_survives_a_refresh) {
    PackageModel model;
    const QVariantMap chat = withOlderVersion(catalogRow("repo1", "chat", "1.2.0", "H12"), "1.1.0", "H11");
    model.setCatalog({chat});
    model.selectVersion(0, 1);
    model.setCatalog({chat, catalogRow("repo1", "other", "1.0", "H")});
    CHECK_EQ(roleOf(model, "chat", "selectedVersionIndex").toInt(), 1);
    CHECK_EQ(roleOf(model, "chat", "version").toString(), QStringLiteral("1.1.0"));
}

// The older version left the catalog: the newest is selected again.
TEST(a_selected_version_that_left_falls_back_to_the_newest) {
    PackageModel model;
    model.setCatalog({withOlderVersion(catalogRow("repo1", "chat", "1.2.0", "H12"), "1.1.0", "H11")});
    model.selectVersion(0, 1);
    model.setCatalog({catalogRow("repo1", "chat", "1.2.0", "H12")});
    CHECK_EQ(roleOf(model, "chat", "selectedVersionIndex").toInt(), 0);
    CHECK_EQ(roleOf(model, "chat", "version").toString(), QStringLiteral("1.2.0"));
}

TEST(a_selection_survives_a_refresh_while_installable) {
    PackageModel model;
    model.setValidVariants({"linux-x86_64"});
    model.setCatalog({catalogRow("repo1", "a", "1.0", "H", {}, {"linux-x86_64"}),
                      catalogRow("repo1", "b", "1.0", "H", {}, {"linux-x86_64"})});
    int selectionChanges = 0;
    QObject::connect(&model, &PackageModel::selectionChanged, &model, [&] { ++selectionChanges; });
    model.setSelected(0, true);
    model.setSelected(1, true);
    CHECK_EQ(selectionChanges, 2);
    CHECK_EQ(model.selectedRows(), (QList<int>{0, 1}));

    model.setCatalog({catalogRow("repo1", "a", "1.0", "H", {}, {"linux-x86_64"}),
                      catalogRow("repo1", "b", "1.0", "H", {}, {"windows-x86_64"})});
    CHECK_EQ(model.selectedRows(), QList<int>{0});
    CHECK_EQ(selectionChanges, 3);

    model.clearSelection();
    CHECK(model.selectedRows().isEmpty());
}

// ── Icons ────────────────────────────────────────────────────────────────

TEST(a_row_shows_the_icon_it_was_given_and_keeps_it) {
    PackageModel model;
    QVariantMap row = catalogRow("repo1", "wallet_ui", "1.0", "H");
    row.insert("icon", "https://r.example/wallet.png");
    model.setCatalog({row, catalogRow("repo1", "chat", "1.0", "H")});
    CHECK_EQ(roleOf(model, "wallet_ui", "iconUrl").toString(), QString());
    CHECK_EQ(roleOf(model, "wallet_ui", "catalogIconUrl").toString(),
             QStringLiteral("https://r.example/wallet.png"));

    Recorder r(model);
    model.setIcon("wallet_ui", "data:image/png;base64,AAAA", true);
    CHECK_EQ(r.changedRows, QList<int>{rowOf(model, "wallet_ui")});
    CHECK_EQ(roleOf(model, "wallet_ui", "iconUrl").toString(), QStringLiteral("data:image/png;base64,AAAA"));
    CHECK(roleOf(model, "wallet_ui", "supportsFullBleedIcon").toBool());

    model.setCatalog({row, catalogRow("repo1", "chat", "1.1", "H")});
    CHECK_EQ(roleOf(model, "wallet_ui", "iconUrl").toString(), QStringLiteral("data:image/png;base64,AAAA"));
}

// ── What an app brings ───────────────────────────────────────────────────

namespace {

QStringList namesIn(const QVariantList& packages)
{
    QStringList out;
    for (const QVariant& v : packages) out << v.toMap().value("name").toString();
    return out;
}

QVariantMap entryIn(const QVariantList& packages, const QString& name)
{
    for (const QVariant& v : packages)
        if (v.toMap().value("name").toString() == name) return v.toMap();
    return {};
}

// `row`'s newest version, with its manifest's `key` set to `value`.
QVariantMap withManifest(QVariantMap row, const char* key, const QVariant& value, int version = 0)
{
    QVariantList versions = row.value("versions").toList();
    QVariantMap v = versions.value(version).toMap();
    QVariantMap manifest = v.value("manifest").toMap();
    manifest.insert(QLatin1String(key), value);
    v.insert("manifest", manifest);
    versions[version] = v;
    row.insert("versions", versions);
    return row;
}

}  // namespace

TEST(an_app_brings_what_it_needs_and_what_that_needs_nearest_first_once_each) {
    Apps model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "W", {dep("signer"), dep("keys")}),
                      catalogRow("repo1", "signer", "2.0", "S2", {dep("keys"), dep("crypto")}, {}, "core"),
                      catalogRow("repo1", "keys", "1.1", "K11", {}, {}, "core"),
                      // Needs what needed it: listed once.
                      catalogRow("repo1", "crypto", "0.3", "C3", {dep("signer")}, {}, "core")});
    model.setInstalled({installed("keys", "1.0", "K10")});
    const QVariantList brought = model.packagesBroughtBy(model.rowOf("wallet_ui", "repo1"));
    // The app itself first, its own action, not counting what it needs.
    CHECK_EQ(namesIn(brought), (QStringList{"wallet_ui", "signer", "keys", "crypto"}));
    CHECK(brought.first().toMap().value("subject").toBool());
    CHECK_EQ(brought.first().toMap().value("rowAction").toInt(), toInt(RowAction::Install));
    CHECK(!entryIn(brought, "signer").value("subject").toBool());
    CHECK_EQ(entryIn(brought, "signer").value("rowAction").toInt(), toInt(RowAction::Install));
    CHECK_EQ(entryIn(brought, "keys").value("rowAction").toInt(), toInt(RowAction::Upgrade));
    CHECK_EQ(entryIn(brought, "keys").value("installedVersion").toString(), QStringLiteral("1.0"));
    CHECK_EQ(entryIn(brought, "keys").value("version").toString(), QStringLiteral("1.1"));
    CHECK(!entryIn(brought, "crypto").value("optional").toBool());
    CHECK(entryIn(brought, "crypto").value("found").toBool());
    CHECK(model.packagesBroughtBy(-1).isEmpty());
}

TEST(an_apps_optional_packages_come_after_what_it_needs_ticked) {
    PackageModel model;
    model.setCatalog({withManifest(catalogRow("repo1", "chat_ui", "1.0", "C", {dep("delivery")}),
                                   "optional_dependencies",
                                   QVariantList{"libp2p", QVariantMap{{"name", "rln"}, {"version", "~1.0"}},
                                                "delivery"}),
                      catalogRow("repo1", "delivery", "1.0", "D", {}, {}, "core"),
                      catalogRow("repo1", "rln", "1.0", "R", {}, {}, "core"),
                      catalogRow("repo1", "libp2p", "1.0", "L", {}, {}, "core")});
    const QVariantList brought = model.packagesBroughtBy(model.rowOf("chat_ui"));
    // One it needs anyway isn't optional.
    CHECK_EQ(namesIn(brought), (QStringList{"chat_ui", "delivery", "libp2p", "rln"}));
    CHECK(!entryIn(brought, "delivery").value("optional").toBool());
    CHECK(!entryIn(brought, "delivery").contains("selected"));
    const QVariantMap rln = entryIn(brought, "rln");
    CHECK(rln.value("optional").toBool());
    CHECK(rln.value("selected").toBool());
    CHECK_EQ(rln.value("offeredBy").toString(), QStringLiteral("chat_ui"));
    CHECK(!rln.contains("requiredFor"));
}

// As Basecamp's install dialog listed them: any package the app brings may
// offer one.
TEST(optional_packages_come_from_whatever_the_app_brings) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "chat_ui", "1.0", "C", {dep("chat")}),
                      catalogRow("repo1", "chat", "1.0", "CH", {dep("delivery")}, {}, "core"),
                      withManifest(catalogRow("repo1", "delivery", "1.0", "D", {}, {}, "core"),
                                   "optional_dependencies", QVariantList{"libp2p", "chat_ui"}),
                      catalogRow("repo1", "libp2p", "1.0", "L", {}, {}, "core")});
    const QVariantList brought = model.packagesBroughtBy(model.rowOf("chat_ui"));
    CHECK_EQ(namesIn(brought), (QStringList{"chat_ui", "chat", "delivery", "libp2p"}));
    CHECK(entryIn(brought, "libp2p").value("optional").toBool());
    CHECK_EQ(entryIn(brought, "libp2p").value("offeredBy").toString(), QStringLiteral("delivery"));
}

TEST(a_ticked_optional_package_brings_what_it_needs_right_under_it) {
    PackageModel model;
    model.setCatalog({withManifest(catalogRow("repo1", "chat_ui", "1.0", "C", {dep("delivery")}),
                                   "optional_dependencies", QVariantList{"rln", "libp2p"}),
                      // What delivery needs too comes with it, not with rln.
                      catalogRow("repo1", "delivery", "1.0", "D", {dep("crypto")}, {}, "core"),
                      catalogRow("repo1", "crypto", "1.0", "CR", {}, {}, "core"),
                      withManifest(catalogRow("repo1", "rln", "1.0", "R", {dep("zk"), dep("crypto")}, {}, "core"),
                                   "optional_dependencies", QVariantList{"prover"}),
                      catalogRow("repo1", "zk", "1.0", "Z", {dep("field")}, {}, "core"),
                      catalogRow("repo1", "field", "1.0", "F", {}, {}, "core"),
                      catalogRow("repo1", "prover", "1.0", "P", {}, {}, "core"),
                      catalogRow("repo1", "libp2p", "1.0", "L", {}, {}, "core")});
    const int app = model.rowOf("chat_ui");
    QVariantList brought = model.packagesBroughtBy(app);
    CHECK_EQ(namesIn(brought),
             (QStringList{"chat_ui", "delivery", "crypto", "rln", "zk", "field", "libp2p", "prover"}));
    for (const char* name : {"zk", "field"}) {
        const QVariantMap entry = entryIn(brought, QLatin1String(name));
        CHECK_EQ(entry.value("requiredFor").toString(), QStringLiteral("rln"));
        CHECK(entry.value("optional").toBool());
        CHECK(!entry.contains("selected"));
    }
    CHECK(!entryIn(brought, "crypto").contains("requiredFor"));
    // What a ticked one offers is offered too.
    CHECK_EQ(entryIn(brought, "prover").value("offeredBy").toString(), QStringLiteral("rln"));
    CHECK(!entryIn(brought, "prover").contains("requiredFor"));

    // Unticked: nothing comes with it.
    brought = model.packagesBroughtBy(app, {{"rln", false}});
    CHECK_EQ(namesIn(brought), (QStringList{"chat_ui", "delivery", "crypto", "rln", "libp2p"}));
    CHECK(!entryIn(brought, "rln").value("selected").toBool());
    CHECK(entryIn(brought, "libp2p").value("selected").toBool());
}

TEST(an_optional_package_that_cannot_be_installed_is_not_ticked) {
    PackageModel model;
    model.setValidVariants({"linux-x86_64"});
    model.setCatalog({withManifest(catalogRow("repo1", "chat_ui", "1.0", "C", {}, {"linux-x86_64"}),
                                   "optional_dependencies", QVariantList{"rln", "ghost", "bundled"}),
                      catalogRow("repo1", "rln", "1.0", "R", {dep("zk")}, {"windows-x86_64"}, "core"),
                      catalogRow("repo1", "zk", "1.0", "Z", {}, {"linux-x86_64"}, "core")});
    model.setInstalled({installed("bundled", "0.9", "B", "embedded")});
    const QVariantList brought = model.packagesBroughtBy(model.rowOf("chat_ui"),
                                                         {{"rln", true}, {"ghost", true}, {"bundled", false}});
    CHECK_EQ(namesIn(brought), (QStringList{"chat_ui", "rln", "ghost", "bundled"}));
    CHECK_EQ(entryIn(brought, "rln").value("rowAction").toInt(), toInt(RowAction::NotAvailable));
    CHECK(!entryIn(brought, "rln").value("selected").toBool());
    CHECK(!entryIn(brought, "ghost").value("found").toBool());
    CHECK(!entryIn(brought, "ghost").value("selected").toBool());
    // On disk with nothing in a catalog: kept, as it is, whatever was asked.
    CHECK(entryIn(brought, "bundled").value("installedOnly").toBool());
    CHECK(entryIn(brought, "bundled").value("selected").toBool());
    CHECK(!entryIn(brought, "rln").value("installedOnly").toBool());
}

TEST(a_package_is_found_in_the_apps_repository_then_anywhere_then_on_disk) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "W",
                                 {dep("signer"), dep("capability_module"), dep("ghost")}),
                      catalogRow("repo2", "signer", "3.0", "S3", {}, {}, "core"),
                      catalogRow("repo1", "signer", "2.0", "S2", {}, {}, "core"),
                      catalogRow("repo2", "wallet_ui", "9.0", "W9", {dep("signer")})});
    model.setInstalled({installed("capability_module", "0.9", "CAP", "embedded")});
    const QVariantList brought = model.packagesBroughtBy(model.rowOf("wallet_ui", "repo1"));
    CHECK_EQ(entryIn(brought, "signer").value("version").toString(), QStringLiteral("2.0"));
    const QVariantMap bundled = entryIn(brought, "capability_module");
    CHECK(bundled.value("found").toBool());
    CHECK_EQ(bundled.value("installedVersion").toString(), QStringLiteral("0.9"));
    CHECK_EQ(bundled.value("rowAction").toInt(), toInt(RowAction::NoOp));
    const QVariantMap ghost = entryIn(brought, "ghost");
    CHECK(!ghost.value("found").toBool());
    CHECK_EQ(ghost.value("rowAction").toInt(), toInt(RowAction::NotAvailable));
    // Only another repository has it: from there.
    PackageModel other;
    other.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "W", {dep("signer")}),
                      catalogRow("repo2", "signer", "3.0", "S3", {}, {}, "core")});
    CHECK_EQ(entryIn(other.packagesBroughtBy(0), "signer").value("version").toString(), QStringLiteral("3.0"));
}

TEST(what_an_app_brings_follows_the_version_picked) {
    PackageModel model;
    model.setCatalog({withManifest(withOlderVersion(catalogRow("repo1", "wallet_ui", "2.0", "W2", {dep("signer")}),
                                                    "1.0", "W1"),
                                   "dependencies", QVariantList{QVariantMap{{"name", "old_signer"}}}, 1),
                      catalogRow("repo1", "signer", "2.0", "S2", {}, {}, "core"),
                      catalogRow("repo1", "old_signer", "1.0", "O1", {}, {}, "core")});
    const int row = model.rowOf("wallet_ui");
    CHECK_EQ(namesIn(model.packagesBroughtBy(row)), (QStringList{"wallet_ui", "signer"}));
    CHECK_EQ(model.packagesBroughtBy(row).first().toMap().value("versions").toStringList(),
             (QStringList{"2.0", "1.0"}));
    model.selectVersion(row, 1);
    CHECK_EQ(model.packagesBroughtBy(row).first().toMap().value("selectedVersionIndex").toInt(), 1);
    CHECK_EQ(namesIn(model.packagesBroughtBy(row)), (QStringList{"wallet_ui", "old_signer"}));
    CHECK_EQ(model.packagesBroughtBy(row).first().toMap().value("version").toString(), QStringLiteral("1.0"));
}

// A dependency picked at another version: that version, and what it needs.
TEST(a_dependency_picked_at_another_version_brings_what_that_version_needs) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "W", {dep("signer")}),
                      withManifest(withOlderVersion(catalogRow("repo1", "signer", "2.0", "S2", {dep("keys")},
                                                               {}, "core"), "1.0", "S1"),
                                   "dependencies", QVariantList{QVariantMap{{"name", "old_keys"}}}, 1),
                      catalogRow("repo1", "keys", "1.0", "K", {}, {}, "core"),
                      catalogRow("repo1", "old_keys", "1.0", "OK", {}, {}, "core")});
    const int app = model.rowOf("wallet_ui");
    CHECK_EQ(namesIn(model.packagesBroughtBy(app)), (QStringList{"wallet_ui", "signer", "keys"}));
    CHECK(!entryIn(model.packagesBroughtBy(app), "signer").contains("picked"));
    const QVariantList brought = model.packagesBroughtBy(app, {}, {{"signer", "1.0"}});
    CHECK_EQ(namesIn(brought), (QStringList{"wallet_ui", "signer", "old_keys"}));
    CHECK_EQ(entryIn(brought, "signer").value("version").toString(), QStringLiteral("1.0"));
    CHECK_EQ(entryIn(brought, "signer").value("selectedVersionIndex").toInt(), 1);
    CHECK_EQ(entryIn(brought, "signer").value("repositoryUrl").toString(), QStringLiteral("repo1"));
    CHECK(entryIn(brought, "signer").value("picked").toBool());
    // A pick is the details' own: the row's selection is not the details'.
    CHECK_EQ(model.rowAt(model.rowOf("signer")).value("version").toString(), QStringLiteral("2.0"));
}

// As Basecamp's resolver plans it: the newest version the range allows.
TEST(a_dependency_comes_at_the_newest_version_its_range_allows) {
    PackageModel model;
    const auto lez = withOlderVersion(withOlderVersion(catalogRow("repo1", "lez_core", "0.5.1", "L51", {}, {}, "core"),
                                                       "0.4.4", "L44"), "0.4.3", "L43");
    // "version", or "!" when it conflicts.
    const auto versionFor = [&](const QString& range) {
        model.setCatalog({catalogRow("repo1", "token_ui", "1.0", "T", {dep("lez_core", range)}), lez});
        const QVariantMap entry = entryIn(model.packagesBroughtBy(model.rowOf("token_ui")), "lez_core");
        return entry.value("conflict").toBool() ? QStringLiteral("!") : entry.value("version").toString();
    };
    CHECK_EQ(versionFor("0.4.3"), QStringLiteral("0.4.3"));
    CHECK_EQ(versionFor("~0.4.0"), QStringLiteral("0.4.4"));
    CHECK_EQ(versionFor(">=0.5.0"), QStringLiteral("0.5.1"));
    CHECK_EQ(versionFor(""), QStringLiteral("0.5.1"));
    model.setCatalog({catalogRow("repo1", "token_ui", "1.0", "T", {dep("lez_core", "0.4.3")}), lez});
    const QVariantMap entry = entryIn(model.packagesBroughtBy(model.rowOf("token_ui")), "lez_core");
    CHECK_EQ(entry.value("selectedVersionIndex").toInt(), 2);
    CHECK_EQ(entry.value("versions").toStringList(), (QStringList{"0.5.1", "0.4.4", "0.4.3"}));
}

TEST(a_package_two_others_need_meets_both_their_ranges) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "app_ui", "1.0", "A", {dep("a"), dep("b")}),
                      catalogRow("repo1", "a", "1.0", "A1", {dep("c", "^1.0.0")}, {}, "core"),
                      catalogRow("repo1", "b", "1.0", "B1", {dep("c", "<1.2.0")}, {}, "core"),
                      withOlderVersion(withOlderVersion(catalogRow("repo1", "c", "1.3.0", "C13", {}, {}, "core"),
                                                        "1.1.0", "C11"), "1.0.0", "C10")});
    const QVariantMap c = entryIn(model.packagesBroughtBy(model.rowOf("app_ui")), "c");
    CHECK_EQ(c.value("version").toString(), QStringLiteral("1.1.0"));
    CHECK(!c.value("conflict").toBool());
}

// What it brings at the version the range chose, not at the newest.
TEST(what_a_dependency_brings_is_what_the_version_its_range_allows_needs) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "app_ui", "1.0", "A", {dep("c", "~1.0.0")}),
                      withManifest(withOlderVersion(catalogRow("repo1", "c", "2.0.0", "C2", {dep("new_dep")}, {},
                                                               "core"), "1.0.0", "C1"),
                                   "dependencies", QVariantList{"old_dep"}, 1),
                      catalogRow("repo1", "new_dep", "1.0", "N", {}, {}, "core"),
                      catalogRow("repo1", "old_dep", "1.0", "O", {}, {}, "core")});
    CHECK_EQ(namesIn(model.packagesBroughtBy(model.rowOf("app_ui"))), (QStringList{"app_ui", "c", "old_dep"}));
}

TEST(a_range_no_version_meets_is_a_conflict) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "app_ui", "1.0", "A", {dep("c", "~2.0.0")}),
                      catalogRow("repo1", "c", "1.0.0", "C1", {}, {}, "core")});
    const QVariantMap c = entryIn(model.packagesBroughtBy(model.rowOf("app_ui")), "c");
    CHECK(c.value("conflict").toBool());
    CHECK_EQ(c.value("version").toString(), QStringLiteral("1.0.0"));
    CHECK(!entryIn(model.packagesBroughtBy(model.rowOf("app_ui")), "app_ui").value("conflict").toBool());
}

TEST(a_version_picked_outside_a_range_comes_and_says_it_conflicts) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "app_ui", "1.0", "A", {dep("c", "^1.0.0")}),
                      withOlderVersion(catalogRow("repo1", "c", "2.0.0", "C2", {}, {}, "core"), "1.0.0", "C1")});
    const int app = model.rowOf("app_ui");
    CHECK_EQ(entryIn(model.packagesBroughtBy(app), "c").value("version").toString(), QStringLiteral("1.0.0"));
    const QVariantMap c = entryIn(model.packagesBroughtBy(app, {}, {{"c", "2.0.0"}}), "c");
    CHECK_EQ(c.value("version").toString(), QStringLiteral("2.0.0"));
    CHECK(c.value("picked").toBool());
    CHECK(c.value("conflict").toBool());
    // A version no catalog has: as if nothing was picked.
    CHECK(!entryIn(model.packagesBroughtBy(app, {}, {{"c", "9.9.9"}}), "c").contains("picked"));
}

// The old dialog's pickers left out what the download source can't serve.
TEST(a_version_this_host_cannot_fetch_is_passed_over) {
    PackageModel model;
    QVariantMap c = withOlderVersion(catalogRow("repo1", "c", "1.1.0", "C11", {}, {}, "core"), "1.0.0", "C10");
    QVariantList versions = c.value("versions").toList();
    QVariantMap newest = versions.first().toMap();
    newest.insert("sourceAvailable", false);
    versions[0] = newest;
    c.insert("versions", versions);
    model.setCatalog({catalogRow("repo1", "app_ui", "1.0", "A", {dep("c")}), c});
    const QVariantMap entry = entryIn(model.packagesBroughtBy(model.rowOf("app_ui")), "c");
    CHECK_EQ(entry.value("version").toString(), QStringLiteral("1.0.0"));
    CHECK_EQ(entry.value("versionsServed").toList(), (QVariantList{false, true}));
}

// As the old dialog had it: opening an installed app doesn't turn its Open
// into an Install for optional packages it doesn't have; one on disk stays.
TEST(an_installed_apps_new_optional_packages_start_unticked) {
    PackageModel model;
    model.setCatalog({withManifest(catalogRow("repo1", "chat_ui", "1.0", "C"), "optional_dependencies",
                                   QVariantList{"rln", "libp2p"}),
                      catalogRow("repo1", "rln", "1.0", "R", {}, {}, "core"),
                      catalogRow("repo1", "libp2p", "1.0", "L", {}, {}, "core")});
    model.setInstalled({installed("chat_ui", "1.0", "C"), installed("libp2p", "1.0", "L")});
    const QVariantList brought = model.packagesBroughtBy(model.rowOf("chat_ui"));
    CHECK(!entryIn(brought, "rln").value("selected").toBool());
    CHECK(entryIn(brought, "libp2p").value("selected").toBool());
    CHECK(entryIn(model.packagesBroughtBy(model.rowOf("chat_ui"), {{"rln", true}}), "rln")
              .value("selected").toBool());
}

TEST(a_package_being_installed_says_so) {
    PackageModel model;
    InstallProgress progress;
    model.setProgress(&progress);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "W", {dep("signer")}),
                      catalogRow("repo1", "signer", "2.0", "S2", {}, {}, "core")});
    CHECK(!entryIn(model.packagesBroughtBy(0), "signer").value("installing").toBool());
    progress.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "W", 0, "repo1"}, {"signer", "2.0", "S2", 0, "repo1"}});
    CHECK(entryIn(model.packagesBroughtBy(0), "signer").value("installing").toBool());
    CHECK(entryIn(model.packagesBroughtBy(0), "signer").value("queued").toBool());
    progress.setDownloadProgress("signer", 10, 100, "logos");
    const QVariantMap signer = entryIn(model.packagesBroughtBy(0), "signer");
    CHECK_EQ(signer.value("downloadReceived").toULongLong(), 10ull);
    CHECK_EQ(signer.value("downloadTotal").toULongLong(), 100ull);
    CHECK_EQ(signer.value("downloadTransport").toString(), QStringLiteral("logos"));
    CHECK_EQ(signer.value("installStage").toInt(), toInt(InstallStage::Downloading));
}

// ── Reading rows ─────────────────────────────────────────────────────────

TEST(rows_are_found_by_name_and_repository) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H1"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H2")});
    CHECK_EQ(model.rowOf("wallet_ui", "repo2"), 1);
    CHECK_EQ(model.rowOf("wallet_ui"), 0);
    CHECK_EQ(model.rowOf("missing"), -1);
    const QVariantMap row = model.rowAt(1);
    CHECK_EQ(row.value("hash").toString(), QStringLiteral("H2"));
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::Install));
    CHECK_EQ(row.size(), model.roleNames().size());
}

TEST(every_role_has_a_distinct_name) {
    PackageModel model;
    QSet<QByteArray> seen;
    for (const QByteArray& name : model.roleNames()) {
        CHECK(!seen.contains(name));
        seen.insert(name);
    }
    CHECK_EQ(roleId(model, "planDownloadFellBack"), int(PackageModel::PlanDownloadFellBackRole));
}

CHECK_MAIN()
