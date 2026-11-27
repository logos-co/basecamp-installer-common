// The installs in flight, and how a row and an app's tile show them. From
// logos-basecamp's tests/download_progress_test.cpp and install_flow_test.cpp.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/InstallProgress.h>
#include <logos/installer/PackageModel.h>

#include <algorithm>

using namespace logos::installer;
using namespace fixtures;

namespace {

void beginDownloading(InstallProgress& p, const QString& name)
{
    p.begin(name, {}, {}, name);
    p.setStage(name, InstallStage::Downloading);
}

void record(InstallProgress& p, void (InstallProgress::*signal)(const QString&),
            QStringList* into)
{
    QObject::connect(&p, signal, &p, [into](const QString& name) { into->append(name); });
}

quint64 u64(const PackageModel& model, const QString& name, const QByteArray& role)
{
    return roleOf(model, name, role).toULongLong();
}

}  // namespace

// ── Bookkeeping ──────────────────────────────────────────────────────────

TEST(progress_is_zero_before_any_sample_arrives) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(0));
    CHECK_EQ(p.downloadTotal("wallet_ui"), quint64(0));
}

TEST(progress_is_recorded_while_downloading) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 1024, 4096);
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(1024));
    CHECK_EQ(p.downloadTotal("wallet_ui"), quint64(4096));
}

// "The transport didn't say": blanking the total would drop the bar back to
// indeterminate mid-download.
TEST(an_unknown_total_keeps_the_previous_total) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 1024, 4096);
    p.setDownloadProgress("wallet_ui", 2048, 0);
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(2048));
    CHECK_EQ(p.downloadTotal("wallet_ui"), quint64(4096));
}

TEST(progress_for_an_untracked_package_is_ignored) {
    InstallProgress p;
    p.setDownloadProgress("never_started", 512, 1024);
    CHECK_EQ(p.downloadReceived("never_started"), quint64(0));
    CHECK(!p.has("never_started"));
    CHECK(p.activeNames().isEmpty());
}

// A sample crosses a process boundary and can land after the stage moved on.
TEST(late_progress_after_the_download_is_ignored) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 1024, 4096);
    p.setStage("wallet_ui", InstallStage::Installing);
    p.setDownloadProgress("wallet_ui", 4096, 4096);
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(1024));
    CHECK_EQ(p.stage("wallet_ui"), InstallStage::Installing);
}

// The counters record a finished transfer, which the tile sums.
TEST(leaving_the_download_keeps_the_counters) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 4096, 4096);
    p.setStage("wallet_ui", InstallStage::Installing);
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(4096));
    CHECK_EQ(p.downloadTotal("wallet_ui"), quint64(4096));
}

TEST(progress_has_its_own_signal) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    QStringList progressed, staged;
    record(p, &InstallProgress::downloadProgressChanged, &progressed);
    record(p, &InstallProgress::stageChanged, &staged);
    p.setDownloadProgress("wallet_ui", 1024, 4096);
    CHECK_EQ(progressed, QStringList{"wallet_ui"});
    CHECK(staged.isEmpty());
}

TEST(a_cleared_entry_reports_no_progress) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 1024, 4096);
    p.clear("wallet_ui");
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(0));
    CHECK_EQ(p.downloadTotal("wallet_ui"), quint64(0));
}

// ── A plan: the install an app starts ────────────────────────────────────

TEST(a_plan_sums_every_package) {
    InstallProgress p;
    p.begin("wallet_ui", {}, {}, "wallet_ui");
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    CHECK_EQ(p.planDownloadTotal("wallet_ui"), quint64(5000));
    CHECK_EQ(p.planDownloadReceived("wallet_ui"), quint64(0));
    p.setDownloadProgress("wallet_module", 2000, 4000);
    p.setDownloadProgress("wallet_ui", 500, 1000);
    CHECK_EQ(p.planDownloadReceived("wallet_ui"), quint64(2500));
}

// The total is right before a byte moves, so the tile's never grows.
TEST(a_plans_total_comes_from_the_catalog_first) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"dep_a", "1.0", "H_a", 2000},
                              {"dep_b", "1.0", "H_b", 3000}});
    CHECK_EQ(p.planDownloadTotal("wallet_ui"), quint64(6000));
}

// The app is another row: it must hear about its dependency's bytes.
TEST(a_dependencys_progress_notifies_its_app) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    QStringList progressed;
    record(p, &InstallProgress::downloadProgressChanged, &progressed);
    p.setDownloadProgress("wallet_module", 2000, 4000);
    CHECK(progressed.contains("wallet_module"));
    CHECK(progressed.contains("wallet_ui"));
}

TEST(a_package_that_started_no_plan_reports_its_own_bytes) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    p.setDownloadProgress("wallet_module", 2000, 4000);
    CHECK_EQ(p.planDownloadReceived("wallet_module"), quint64(2000));
    CHECK_EQ(p.planDownloadTotal("wallet_module"), quint64(4000));
}

// Downloads run one at a time: a plan starts with every package queued.
TEST(planned_packages_start_queued) {
    InstallProgress p;
    p.begin("wallet_ui", {}, {}, "wallet_ui");
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    CHECK_EQ(p.stage("wallet_module"), InstallStage::Queued);
}

TEST(first_bytes_start_a_queued_package) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"dep_a", "1.0", "H_a", 2000}, {"dep_b", "1.0", "H_b", 3000}});
    QStringList staged;
    record(p, &InstallProgress::stageChanged, &staged);
    p.setDownloadProgress("dep_a", 500, 2000);
    CHECK_EQ(p.stage("dep_a"), InstallStage::Downloading);
    CHECK_EQ(p.downloadReceived("dep_a"), quint64(500));
    CHECK(staged.contains("dep_a"));
    CHECK_EQ(p.stage("dep_b"), InstallStage::Queued);
}

TEST(queued_packages_count_toward_the_plans_total) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"dep_a", "1.0", "H_a", 2000}, {"dep_b", "1.0", "H_b", 3000}});
    p.setDownloadProgress("dep_a", 2000, 2000);
    CHECK_EQ(p.planDownloadTotal("wallet_ui"), quint64(5000));
    CHECK_EQ(p.planDownloadReceived("wallet_ui"), quint64(2000));
}

TEST(two_installs_sum_separately) {
    InstallProgress p;
    p.beginPlan("app_a", {{"app_a", "1.0", "H_a", 1000}, {"dep_a", "1.0", "H_da", 2000}});
    p.beginPlan("app_b", {{"app_b", "1.0", "H_b", 5000}});
    CHECK_EQ(p.planDownloadTotal("app_a"), quint64(3000));
    CHECK_EQ(p.planDownloadTotal("app_b"), quint64(5000));
    p.setDownloadProgress("dep_a", 1000, 2000);
    CHECK_EQ(p.planDownloadReceived("app_a"), quint64(1000));
    CHECK_EQ(p.planDownloadReceived("app_b"), quint64(0));
}

// Counted for one install only, the other's bar would stop short.
TEST(a_shared_dependency_counts_toward_both_installs) {
    InstallProgress p;
    p.beginPlan("app_a", {{"app_a", "1.0", "H_a", 1000}, {"shared", "1.0", "H_s", 4000}});
    p.beginPlan("app_b", {{"app_b", "1.0", "H_b", 2000}, {"shared", "1.0", "H_s", 4000}});
    CHECK_EQ(p.planDownloadTotal("app_a"), quint64(5000));
    CHECK_EQ(p.planDownloadTotal("app_b"), quint64(6000));
    p.setDownloadProgress("shared", 4000, 4000);
    CHECK_EQ(p.planDownloadReceived("app_a"), quint64(4000));
    CHECK_EQ(p.planDownloadReceived("app_b"), quint64(4000));
}

TEST(joining_a_shared_dependency_keeps_its_progress) {
    InstallProgress p;
    p.beginPlan("app_a", {{"shared", "1.0", "H_s", 4000}});
    p.setDownloadProgress("shared", 3000, 4000);
    p.beginPlan("app_b", {{"shared", "1.0", "H_s", 4000}});
    CHECK_EQ(p.downloadReceived("shared"), quint64(3000));
    CHECK_EQ(p.planDownloadReceived("app_b"), quint64(3000));
}

TEST(clearing_one_install_keeps_what_the_other_still_needs) {
    InstallProgress p;
    p.beginPlan("app_a", {{"app_a", "1.0", "H_a", 1000}, {"shared", "1.0", "H_s", 4000}});
    p.beginPlan("app_b", {{"app_b", "1.0", "H_b", 2000}, {"shared", "1.0", "H_s", 4000}});
    p.clearByTopLevel("app_a");
    CHECK(p.has("shared"));
    CHECK(!p.has("app_a"));
    CHECK_EQ(p.planDownloadTotal("app_b"), quint64(6000));
}

TEST(clearing_the_last_install_removes_the_entry) {
    InstallProgress p;
    p.beginPlan("app_a", {{"shared", "1.0", "H_s", 4000}});
    p.beginPlan("app_b", {{"shared", "1.0", "H_s", 4000}});
    p.clearByTopLevel("app_a");
    CHECK(p.has("shared"));
    p.clearByTopLevel("app_b");
    CHECK(!p.has("shared"));
}

// The tiles involved repaint, or they keep showing the bytes.
TEST(clearing_a_dependency_repaints_its_app) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    QStringList progressed, planned;
    record(p, &InstallProgress::downloadProgressChanged, &progressed);
    record(p, &InstallProgress::planStageChanged, &planned);
    p.clear("wallet_module");
    CHECK(progressed.contains("wallet_module"));
    CHECK(progressed.contains("wallet_ui"));
    CHECK(planned.contains("wallet_ui"));
    CHECK_EQ(p.planDownloadTotal("wallet_ui"), quint64(1000));
}

TEST(leaving_a_shared_dependency_repaints_the_install_that_left) {
    InstallProgress p;
    p.beginPlan("app_a", {{"shared", "1.0", "H_s", 4000}});
    p.beginPlan("app_b", {{"shared", "1.0", "H_s", 4000}});
    QStringList progressed;
    record(p, &InstallProgress::downloadProgressChanged, &progressed);
    p.clearByTopLevel("app_a");
    CHECK(progressed.contains("app_a"));
    CHECK_EQ(p.planDownloadTotal("app_a"), quint64(0));
}

TEST(a_failed_dependency_fails_its_apps_plan) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    QStringList planned, errored;
    record(p, &InstallProgress::planStageChanged, &planned);
    record(p, &InstallProgress::errorChanged, &errored);
    p.fail("wallet_module", "checksum mismatch");
    CHECK(planned.contains("wallet_ui"));
    CHECK_EQ(errored, QStringList{"wallet_module"});
    CHECK_EQ(p.planStage("wallet_ui"), InstallStage::Failed);
    CHECK_EQ(p.error("wallet_module"), QStringLiteral("checksum mismatch"));
}

// ── A new attempt ────────────────────────────────────────────────────────

// What a failed attempt left is no part of the next: it starts Queued, with
// no error, and its first bytes count.
TEST(a_plan_after_a_failed_one_starts_afresh) {
    InstallProgress p;
    p.markFailed("wallet_ui", "the download stopped");
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000}});
    CHECK_EQ(p.stage("wallet_ui"), InstallStage::Queued);
    CHECK_EQ(p.error("wallet_ui"), QString());
    p.setDownloadProgress("wallet_ui", 400, 1000);
    CHECK_EQ(p.stage("wallet_ui"), InstallStage::Downloading);
    CHECK_EQ(p.downloadReceived("wallet_ui"), quint64(400));
    CHECK_EQ(p.planStage("wallet_ui"), InstallStage::Downloading);
}

// Failed from one repository, then installed from another: the new install
// shows on the other's row.
TEST(a_plan_from_another_repository_after_a_failure_shows_on_its_row) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H1"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H2")});
    p.markFailed("wallet_ui", "the download stopped", "repo1");
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H2", 400, "repo2"}});
    p.setDownloadProgress("wallet_ui", 100, 400);
    CHECK(p.belongsTo("wallet_ui", "repo2"));
    CHECK(!p.belongsTo("wallet_ui", "repo1"));
    CHECK_EQ(roleOf(model, "wallet_ui", "downloadReceived", "repo2").toULongLong(), quint64(100));
    CHECK_EQ(roleOf(model, "wallet_ui", "installStatus", "repo1").toInt(),
             toInt(InstallStatus::NotInstalled));
}

// An app on disk whose retry fetches only a dependency: its own row's
// failure was the attempt before's.
TEST(a_plan_that_brings_only_a_dependency_leaves_no_failure_behind) {
    InstallProgress p;
    p.markFailed("chat", "chat_module: no variant for this platform");
    p.beginPlan("chat", {{"chat_module", "1.0", "H_mod", 4000}});
    CHECK(!p.has("chat"));
    CHECK_EQ(p.planStage("chat"), InstallStage::Downloading);
}

// A dependency that failed last time is on its way with the retry's plan.
TEST(a_plan_after_a_dependency_failed_starts_it_afresh) {
    InstallProgress p;
    p.markFailed("chat", "chat_module: no variant for this platform");
    p.markFailed("chat_module", "no variant for this platform");
    p.beginPlan("chat", {{"chat_module", "1.0", "H_mod", 4000}, {"chat", "1.0", "H", 1000}});
    CHECK_EQ(p.stage("chat_module"), InstallStage::Queued);
    CHECK_EQ(p.error("chat_module"), QString());
    CHECK_EQ(p.planStage("chat"), InstallStage::Downloading);
    p.setDownloadProgress("chat_module", 1000, 4000);
    CHECK_EQ(p.planDownloadReceived("chat"), quint64(1000));
}

TEST(a_retry_waiting_its_turn_is_queued) {
    InstallProgress p;
    p.markFailed("wallet_ui", "the download stopped");
    p.beginWaiting("wallet_ui", "1.0", {});
    CHECK_EQ(p.stage("wallet_ui"), InstallStage::Queued);
    CHECK_EQ(p.planStage("wallet_ui"), InstallStage::Queued);
    CHECK_EQ(p.error("wallet_ui"), QString());
}

// One in flight is joined, not restarted.
TEST(a_waiting_request_for_a_package_on_its_way_changes_nothing) {
    InstallProgress p;
    p.beginPlan("app_a", {{"shared", "1.0", "H_s", 4000}});
    p.setDownloadProgress("shared", 3000, 4000);
    p.beginWaiting("shared", "1.0", {});
    CHECK_EQ(p.stage("shared"), InstallStage::Downloading);
    CHECK_EQ(p.downloadReceived("shared"), quint64(3000));
}

// ── Where the bytes come from ────────────────────────────────────────────

TEST(the_transport_is_recorded_with_the_bytes) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 0, 4096, "logos");
    CHECK_EQ(p.downloadTransport("wallet_ui"), QStringLiteral("logos"));
    CHECK(!p.downloadFellBack("wallet_ui"));
}

TEST(http_after_logos_is_a_fallback) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 0, 4096, "logos");
    p.setDownloadProgress("wallet_ui", 0, 4096, "http");
    CHECK_EQ(p.downloadTransport("wallet_ui"), QStringLiteral("http"));
    CHECK(p.downloadFellBack("wallet_ui"));
}

TEST(http_from_the_start_is_no_fallback) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 0, 4096, "http");
    p.setDownloadProgress("wallet_ui", 2048, 4096, "http");
    CHECK(!p.downloadFellBack("wallet_ui"));
}

TEST(a_sample_without_a_transport_keeps_the_last_one) {
    InstallProgress p;
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 0, 4096, "logos");
    p.setDownloadProgress("wallet_ui", 1024, 4096);
    CHECK_EQ(p.downloadTransport("wallet_ui"), QStringLiteral("logos"));
}

TEST(an_apps_row_speaks_for_the_package_on_the_wire) {
    InstallProgress p;
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    p.setDownloadProgress("wallet_module", 0, 4000, "logos");
    CHECK_EQ(p.planDownloadTransport("wallet_ui"), QStringLiteral("logos"));
    p.setDownloadProgress("wallet_module", 0, 4000, "http");
    CHECK_EQ(p.planDownloadTransport("wallet_ui"), QStringLiteral("http"));
    CHECK(p.planDownloadFellBack("wallet_ui"));
}

// ── Through the package list ─────────────────────────────────────────────

TEST(the_list_reports_nothing_without_progress) {
    PackageModel model;
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui")});
    CHECK_EQ(u64(model, "wallet_ui", "downloadReceived"), quint64(0));
    CHECK_EQ(roleOf(model, "wallet_ui", "installStage").toInt(), toInt(InstallStage::None));
}

TEST(the_list_shows_a_rows_own_bytes) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui")});
    beginDownloading(p, "wallet_ui");
    p.setDownloadProgress("wallet_ui", 1024, 4096, "logos");
    CHECK_EQ(u64(model, "wallet_ui", "downloadReceived"), quint64(1024));
    CHECK_EQ(u64(model, "wallet_ui", "downloadTotal"), quint64(4096));
    CHECK_EQ(roleOf(model, "wallet_ui", "downloadTransport").toString(), QStringLiteral("logos"));
}

// The plan pair is the tile's; the package pair stays the app's own bytes,
// which a dialog lists beside its dependency's.
TEST(the_list_shows_an_apps_whole_install_apart_from_its_own_bytes) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui")});
    p.beginPlan("wallet_ui", {{"wallet_ui", "1.0", "H_ui", 1000},
                              {"wallet_module", "1.0", "H_mod", 4000}});
    p.setDownloadProgress("wallet_module", 4000, 4000);
    CHECK_EQ(u64(model, "wallet_ui", "planDownloadTotal"), quint64(5000));
    CHECK_EQ(u64(model, "wallet_ui", "planDownloadReceived"), quint64(4000));
    CHECK_EQ(u64(model, "wallet_ui", "downloadTotal"), quint64(1000));
    CHECK_EQ(u64(model, "wallet_ui", "downloadReceived"), quint64(0));
}

// Progress fires several times a second: only its own roles repaint.
TEST(progress_repaints_only_the_progress_roles) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_ui")});
    beginDownloading(p, "wallet_ui");

    QList<QList<int>> changes;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                     [&](const QModelIndex&, const QModelIndex&, const QList<int>& r) {
                         changes.append(r);
                     });
    p.setDownloadProgress("wallet_ui", 1024, 4096);

    QList<int> roles;
    for (const QList<int>& r : changes) {
        if (r.contains(PackageModel::DownloadReceivedRole)) roles = r;
    }
    CHECK_EQ(roles.size(), 9);
    CHECK(roles.contains(PackageModel::DownloadTransportRole));
    CHECK(roles.contains(PackageModel::PlanDownloadReceivedRole));
    CHECK(roles.contains(PackageModel::PlanInstallStageRole));
    CHECK(!roles.contains(PackageModel::RowActionRole));
    CHECK(!roles.contains(PackageModel::InstallStatusRole));
}

// Two repositories publish one package, and the install names neither.
TEST(progress_reaches_every_row_of_the_name) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H_shared"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H_shared")});
    beginDownloading(p, "wallet_ui");
    int count = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model, [&] { ++count; });
    p.setDownloadProgress("wallet_ui", 2048, 4096);
    CHECK_EQ(count, 2);
}

// An install from one repository animates that repository's row only.
TEST(an_install_from_one_repository_shows_on_its_row_only) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    model.setCatalog({catalogRow("repo1", "wallet_ui", "1.0", "H1"),
                      catalogRow("repo2", "wallet_ui", "1.0", "H2")});
    p.begin("wallet_ui", "1.0", "H2", "wallet_ui", "repo2");
    p.setDownloadProgress("wallet_ui", 100, 400);
    CHECK_EQ(roleOf(model, "wallet_ui", "downloadReceived", "repo2").toULongLong(), quint64(100));
    CHECK_EQ(roleOf(model, "wallet_ui", "downloadReceived", "repo1").toULongLong(), quint64(0));
    CHECK_EQ(roleOf(model, "wallet_ui", "installStatus", "repo2").toInt(),
             toInt(InstallStatus::Installing));
    CHECK_EQ(roleOf(model, "wallet_ui", "installStatus", "repo1").toInt(),
             toInt(InstallStatus::NotInstalled));
}

// ── A whole install, replayed ────────────────────────────────────────────
//
// What happens on the wire: the plan in the resolver's order (dependencies
// first), each package downloaded to completion in turn, then installed in
// turn. The invariants hold after every single step.

namespace {

struct Pkg { QString name; quint64 size; };

const QList<Pkg> kPlan{{"delivery_module", 3 * 1024 * 1024},
                       {"chat_module", 8 * 1024 * 1024},
                       {"chat", 1 * 1024 * 1024}};
const QString kApp = QStringLiteral("chat");

quint64 planTotal()
{
    quint64 t = 0;
    for (const Pkg& pkg : kPlan) t += pkg.size;
    return t;
}

QList<InstallProgress::PlannedPackage> planned()
{
    QList<InstallProgress::PlannedPackage> out;
    for (const Pkg& pkg : kPlan) out.append({pkg.name, "1.0", "H_" + pkg.name, pkg.size});
    return out;
}

void listCatalog(PackageModel& model)
{
    QVariantList catalog;
    for (const Pkg& pkg : kPlan) catalog.append(catalogRow("repo1", pkg.name, "1.0", "H_" + pkg.name));
    model.setCatalog(catalog);
}

void checkInvariants(InstallProgress& p, PackageModel& model, quint64& highWater)
{
    // Downloads are sequential: at most one package reads Downloading.
    int downloading = 0;
    for (const Pkg& pkg : kPlan) {
        if (pkg.name != kApp && p.stage(pkg.name) == InstallStage::Downloading) ++downloading;
    }
    CHECK(downloading <= 1);

    // The tile never goes backwards, nor past the plan.
    const quint64 sum = u64(model, kApp, "planDownloadReceived");
    CHECK(sum >= highWater);
    highWater = sum;
    CHECK(sum <= planTotal());

    // It is the sum of the rows' own bytes, each within its package's size.
    quint64 own = 0;
    for (const Pkg& pkg : kPlan) {
        const quint64 bytes = u64(model, pkg.name, "downloadReceived");
        CHECK(bytes <= pkg.size);
        own += bytes;
    }
    CHECK_EQ(sum, own);
    CHECK_EQ(u64(model, kApp, "planDownloadTotal"), planTotal());
}

InstallStage planStageOf(const PackageModel& model)
{
    return static_cast<InstallStage>(roleOf(model, kApp, "planInstallStage").toInt());
}

}  // namespace

TEST(a_whole_install_holds_every_invariant) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    listCatalog(model);
    quint64 highWater = 0;

    p.beginPlan(kApp, planned());
    checkInvariants(p, model, highWater);
    for (const Pkg& pkg : kPlan) CHECK_EQ(p.stage(pkg.name), InstallStage::Queued);

    for (const Pkg& pkg : kPlan) {
        for (quint64 got : {quint64(0), pkg.size / 4, pkg.size / 2, (pkg.size * 3) / 4, pkg.size}) {
            p.setDownloadProgress(pkg.name, got, pkg.size);
            checkInvariants(p, model, highWater);
        }
        // Finished moves forward to Downloaded, never back to Queued.
        CHECK_EQ(p.stage(pkg.name), InstallStage::Downloaded);
    }
    CHECK_EQ(u64(model, kApp, "planDownloadReceived"), planTotal());

    for (const Pkg& pkg : kPlan) {
        p.setStage(pkg.name, InstallStage::Installing);
        checkInvariants(p, model, highWater);
        p.setStage(pkg.name, InstallStage::Installed);
        checkInvariants(p, model, highWater);
    }
    CHECK_EQ(u64(model, kApp, "planDownloadReceived"), planTotal());

    p.clearByTopLevel(kApp);
    for (const Pkg& pkg : kPlan) CHECK(!p.has(pkg.name));
    CHECK_EQ(u64(model, kApp, "planDownloadReceived"), quint64(0));
}

// Between two packages nothing is on the wire; the tile must not blink.
TEST(an_install_is_one_download_phase_then_installing) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    listCatalog(model);
    p.beginPlan(kApp, planned());

    QList<InstallStage> phases;
    const auto sample = [&] {
        const InstallStage s = planStageOf(model);
        if (phases.isEmpty() || phases.last() != s) phases.append(s);
    };
    sample();
    for (const Pkg& pkg : kPlan) {
        p.setDownloadProgress(pkg.name, 0, pkg.size);         sample();
        p.setDownloadProgress(pkg.name, pkg.size / 2, pkg.size); sample();
        p.setDownloadProgress(pkg.name, pkg.size, pkg.size);  sample();
        sample();
    }
    for (const Pkg& pkg : kPlan) {
        p.setStage(pkg.name, InstallStage::Installing); sample();
        p.setStage(pkg.name, InstallStage::Installed);  sample();
    }
    CHECK_EQ(phases.size(), 3);
    CHECK_EQ(phases.value(0), InstallStage::Downloading);
    CHECK_EQ(phases.value(1), InstallStage::Installing);
    CHECK_EQ(phases.value(2), InstallStage::Installed);
}

// A request behind another: Basecamp hasn't been asked about it, and nothing
// of it is on its way. Its plan read Downloading, so its tile and details
// said so.
TEST(a_request_waiting_its_turn_is_queued_its_plan_too) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    listCatalog(model);
    p.beginWaiting(kApp, "1.0.0", {});
    CHECK_EQ(p.stage(kApp), InstallStage::Queued);
    CHECK_EQ(planStageOf(model), InstallStage::Queued);
    // Its turn, approved: its plan is on its way from the start.
    p.clear(kApp);
    p.beginPlan(kApp, planned());
    CHECK_EQ(planStageOf(model), InstallStage::Downloading);
}

// Brought meanwhile by another install, it is on its way with it.
TEST(a_waiting_request_another_install_brings_is_on_its_way) {
    InstallProgress p;
    p.beginWaiting("delivery_module", "1.0.0", {});
    p.beginPlan(kApp, planned());
    CHECK_EQ(p.planStage(kApp), InstallStage::Downloading);
    CHECK_EQ(p.planStage("delivery_module"), InstallStage::Downloading);
}

TEST(no_row_ever_moves_backwards) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    listCatalog(model);
    p.beginPlan(kApp, planned());

    CHECK_EQ(u64(model, kApp, "downloadReceived"), quint64(0));
    CHECK_EQ(planStageOf(model), InstallStage::Downloading);

    const auto rank = [](InstallStage s) {
        switch (s) {
        case InstallStage::Queued:      return 0;
        case InstallStage::Downloading: return 1;
        case InstallStage::Downloaded:  return 2;
        case InstallStage::Installing:  return 3;
        case InstallStage::Installed:   return 4;
        default:                        return -1;
        }
    };
    QHash<QString, int> furthest;
    const auto forward = [&] {
        for (const Pkg& pkg : kPlan) {
            const int r = rank(p.stage(pkg.name));
            CHECK(r >= furthest.value(pkg.name, 0));
            furthest[pkg.name] = r;
        }
    };
    for (const Pkg& pkg : kPlan) {
        p.setDownloadProgress(pkg.name, 0, pkg.size);
        CHECK_EQ(p.stage(pkg.name), InstallStage::Downloading);
        CHECK_EQ(planStageOf(model), InstallStage::Downloading);
        forward();
        p.setDownloadProgress(pkg.name, pkg.size, pkg.size);
        CHECK_EQ(p.stage(pkg.name), InstallStage::Downloaded);
        forward();
    }
    CHECK_EQ(planStageOf(model), InstallStage::Downloading);
    for (const Pkg& pkg : kPlan) {
        p.setStage(pkg.name, InstallStage::Installing);
        forward();
        p.setStage(pkg.name, InstallStage::Installed);
        forward();
    }
    CHECK_EQ(planStageOf(model), InstallStage::Installed);
}

TEST(only_one_package_ever_reads_downloading) {
    InstallProgress p;
    p.beginPlan(kApp, planned());
    int peak = 0;
    for (const Pkg& pkg : kPlan) {
        for (quint64 got : {quint64(0), pkg.size / 2, pkg.size}) {
            p.setDownloadProgress(pkg.name, got, pkg.size);
            int n = 0;
            for (const Pkg& q : kPlan) {
                if (q.name != kApp && p.stage(q.name) == InstallStage::Downloading) ++n;
            }
            peak = std::max(peak, n);
        }
    }
    CHECK_EQ(peak, 1);
}

TEST(a_finished_download_stays_finished) {
    InstallProgress p;
    p.beginPlan(kApp, {{"delivery_module", "1.0", "H_d", 1000}});
    p.setDownloadProgress("delivery_module", 1000, 1000);
    CHECK_EQ(p.stage("delivery_module"), InstallStage::Downloaded);
    p.setDownloadProgress("delivery_module", 1000, 1000);
    CHECK_EQ(p.stage("delivery_module"), InstallStage::Downloaded);
    CHECK_EQ(p.downloadReceived("delivery_module"), quint64(1000));
}

TEST(a_package_row_and_a_tile_count_different_things) {
    InstallProgress p;
    PackageModel model;
    model.setProgress(&p);
    listCatalog(model);
    p.beginPlan(kApp, planned());
    p.setDownloadProgress("delivery_module", 3 * 1024 * 1024, 3 * 1024 * 1024);
    p.setDownloadProgress("chat_module", 4 * 1024 * 1024, 8 * 1024 * 1024);
    CHECK_EQ(u64(model, kApp, "downloadReceived"), quint64(0));
    CHECK_EQ(u64(model, kApp, "downloadTotal"), quint64(1 * 1024 * 1024));
    CHECK_EQ(u64(model, kApp, "planDownloadReceived"), quint64(7 * 1024 * 1024));
    CHECK_EQ(u64(model, kApp, "planDownloadTotal"), planTotal());
}

CHECK_MAIN()
