// Keeping the package list filled from the two package modules.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/CatalogClient.h>
#include <logos/installer/PackageModel.h>
#include <logos/installer/PackageModules.h>

using namespace logos::installer;
using namespace fixtures;

namespace {

// Answers at once, or holds every answer until flush().
struct FakeModules : PackageModules {
    bool hold = false;
    QStringList calls;
    QList<std::function<void()>> held;

    QVariantList repositories{QVariantMap{{"url", "https://default"}, {"displayName", "Logos"},
                                          {"isDefault", true}},
                              QVariantMap{{"url", "https://mine"}, {"displayName", "Mine"}}};
    QVariantList catalog{catalogRow("https://default", "wallet_ui", "1.0", "H", {}, {"linux-x86_64"})};
    QVariantList installedPackages{installed("wallet_ui", "1.0", "H")};
    QStringList variants{"linux-x86_64"};
    QString downloadSource = "any";
    QList<QVariantMap> storage{QVariantMap{{"state", "running"}}};
    QString refreshError;
    QStringList failing;   // reads whose call fails
    int startFails = 0;    // the next starts that fail

    void answer(std::function<void()> f)
    {
        if (hold) held.append(std::move(f));
        else f();
    }
    void flush()
    {
        while (!held.isEmpty()) held.takeFirst()();
    }

    void startDownloader(std::function<void(bool)> done) override
    {
        calls << "start";
        const bool started = startFails == 0;
        if (!started) --startFails;
        answer([done, started] { done(started); });
    }
    void listRepositories(std::function<void(const QVariantList&, bool)> done) override
    {
        calls << "listRepositories";
        const bool read = !failing.contains("listRepositories");
        answer([done, r = repositories, read] { done(read ? r : QVariantList{}, read); });
    }
    void getCatalog(std::function<void(const QVariantList&, bool)> done) override
    {
        calls << "getCatalog";
        const bool read = !failing.contains("getCatalog");
        answer([done, c = catalog, read] { done(read ? c : QVariantList{}, read); });
    }
    void refreshCatalog(std::function<void(const QString&)> done) override
    {
        calls << "refreshCatalog";
        answer([done, e = refreshError] { done(e); });
    }
    void getDownloadSource(std::function<void(const QString&)> done) override
    {
        calls << "getDownloadSource";
        answer([done, s = downloadSource] { done(s); });
    }
    void getStorageState(std::function<void(const QVariantMap&)> done) override
    {
        calls << "getStorageState";
        const QVariantMap s = storage.size() > 1 ? storage.takeFirst() : storage.value(0);
        answer([done, s] { done(s); });
    }
    void getInstalledPackages(std::function<void(const QVariantList&, bool)> done) override
    {
        calls << "getInstalledPackages";
        const bool read = !failing.contains("getInstalledPackages");
        answer([done, i = installedPackages, read] { done(read ? i : QVariantList{}, read); });
    }
    void getValidVariants(std::function<void(const QStringList&, bool)> done) override
    {
        calls << "getValidVariants";
        const bool read = !failing.contains("getValidVariants");
        answer([done, v = variants, read] { done(read ? v : QStringList{}, read); });
    }
};

int count(const QStringList& calls, const QString& call)
{
    return int(calls.count(call));
}

}  // namespace

TEST(a_load_starts_the_downloader_then_reads_everything_in_order) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    int loaded = 0;
    QObject::connect(&client, &CatalogClient::loaded, &client, [&] { ++loaded; });
    client.load();

    CHECK_EQ(modules.calls.value(0), QStringLiteral("start"));
    const QStringList chain{"listRepositories", "getCatalog", "getInstalledPackages", "getValidVariants"};
    QStringList seen;
    for (const QString& c : modules.calls) if (chain.contains(c)) seen << c;
    CHECK_EQ(seen, chain);
    CHECK(modules.calls.contains("getDownloadSource"));
    CHECK(modules.calls.contains("getStorageState"));

    CHECK_EQ(loaded, 1);
    CHECK(client.hasLoaded());
    CHECK(!client.isLoading());
    CHECK_EQ(model.rowCount(), 1);
    CHECK_EQ(roleOf(model, "wallet_ui", "installStatus").toInt(), toInt(InstallStatus::Installed));
    CHECK_EQ(model.hostVariant(), QStringLiteral("linux-x86_64"));
    CHECK_EQ(client.downloadSource(), QStringLiteral("any"));
    CHECK_EQ(client.storageState(), QStringLiteral("running"));
}

TEST(the_downloader_is_started_once) {
    FakeModules modules;
    modules.hold = true;
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.load();
    client.load();
    modules.flush();
    client.refresh();
    modules.flush();
    CHECK_EQ(count(modules.calls, "start"), 1);
}

// A start that failed is asked again by the next load: package_downloader
// refuses every call until it has started, and a failed start taken for
// one never recovered.
TEST(a_downloader_that_didnt_start_is_started_again) {
    FakeModules modules;
    modules.startFails = 1;
    modules.failing << "listRepositories";
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.load();
    CHECK(!client.hasLoaded());
    CHECK_EQ(client.loadError(), QStringLiteral("the repositories"));
    modules.failing.clear();
    client.refresh();
    CHECK_EQ(count(modules.calls, "start"), 2);
    CHECK(client.hasLoaded());
    client.refresh();
    CHECK_EQ(count(modules.calls, "start"), 2);
}

TEST(the_loading_flag_covers_the_whole_load) {
    FakeModules modules;
    modules.hold = true;
    PackageModel model;
    CatalogClient client(&modules, &model);
    QList<bool> states;
    QObject::connect(&client, &CatalogClient::loadingChanged, &client,
                     [&] { states.append(client.isLoading()); });
    client.load();
    modules.flush();
    CHECK_EQ(states, (QList<bool>{true, false}));
}

// The downloader serves each index from a cache; only refreshCatalog fetches.
TEST(a_refresh_fetches_again_before_loading) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.load();
    modules.calls.clear();
    modules.refreshError = "one repository failed";
    client.refresh();
    CHECK_EQ(modules.calls.value(0), QStringLiteral("refreshCatalog"));
    CHECK(modules.calls.contains("getCatalog"));
    CHECK(!client.isLoading());
}

// An answer a newer load overtook is dropped.
TEST(an_overtaken_load_is_dropped) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.load();
    modules.hold = true;
    modules.calls.clear();
    int loaded = 0;
    QObject::connect(&client, &CatalogClient::loaded, &client, [&] { ++loaded; });
    client.refresh();
    client.refresh();
    modules.flush();
    CHECK_EQ(count(modules.calls, "listRepositories"), 2);
    CHECK_EQ(count(modules.calls, "getCatalog"), 1);
    CHECK_EQ(loaded, 1);
}

TEST(a_burst_of_package_events_loads_once) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.setDebounceInterval(10);
    client.load();
    modules.calls.clear();
    modules.installedPackages = {};
    client.onPackagesChanged();
    client.onPackagesChanged();
    client.onPackagesChanged();
    CHECK(modules.calls.isEmpty());
    SPIN(60);
    CHECK_EQ(count(modules.calls, "listRepositories"), 1);
    CHECK_EQ(roleOf(model, "wallet_ui", "installStatus").toInt(), toInt(InstallStatus::NotInstalled));
}

// A new download source is announced as a catalog change.
TEST(a_catalog_change_loads_and_reads_the_download_source) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.setDebounceInterval(10);
    client.load();
    int sourceChanges = 0;
    QObject::connect(&client, &CatalogClient::downloadSourceChanged, &client, [&] { ++sourceChanges; });
    modules.calls.clear();
    modules.downloadSource = "logos";
    client.onCatalogChanged();
    CHECK_EQ(client.downloadSource(), QStringLiteral("logos"));
    CHECK_EQ(sourceChanges, 1);
    SPIN(60);
    CHECK_EQ(count(modules.calls, "getCatalog"), 1);
}

TEST(repositories_get_labels_an_order_and_a_default) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    int changes = 0;
    QObject::connect(&client, &CatalogClient::repositoriesChanged, &client, [&] { ++changes; });
    client.load();
    CHECK_EQ(client.repositoryCount(), 2);
    CHECK_EQ(client.defaultRepositoryUrl(), QStringLiteral("https://default"));
    CHECK_EQ(client.sourceOrder(), (QStringList{"https://default", "https://mine", "local"}));
    CHECK_EQ(client.repositoryLabels().value("https://mine").toString(), QStringLiteral("Mine"));
    CHECK_EQ(client.repositoryLabels().value("local").toString(), QStringLiteral("local"));
    CHECK_EQ(changes, 1);
    client.refresh();
    CHECK_EQ(changes, 1);
}

// A node on its way up is asked again until it settles, in case an event is
// lost; once.
TEST(storage_on_its_way_up_is_asked_again_until_it_settles) {
    FakeModules modules;
    modules.storage = {QVariantMap{{"state", "starting"}}, QVariantMap{{"state", "starting"}},
                       QVariantMap{{"state", "running"}}};
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.setStoragePollInterval(10);
    client.load();
    client.onStorageStateChanged("starting");
    CHECK_EQ(client.storageState(), QStringLiteral("starting"));
    SPIN(100);
    CHECK_EQ(client.storageState(), QStringLiteral("running"));
    CHECK_EQ(count(modules.calls, "getStorageState"), 3);
}

TEST(storage_events_and_an_armed_subscription_update_the_state) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.load();
    int changes = 0;
    QObject::connect(&client, &CatalogClient::storageStateChanged, &client, [&] { ++changes; });
    client.onStorageStateChanged("failed", "no peers");
    CHECK_EQ(client.storageState(), QStringLiteral("failed"));
    CHECK_EQ(client.storageError(), QStringLiteral("no peers"));
    modules.storage = {QVariantMap{{"state", "off"}}};
    client.onSubscriptionArmed();
    CHECK_EQ(client.storageState(), QStringLiteral("off"));
    CHECK_EQ(changes, 2);
}

// A downloader without getStorageState answers nothing: nothing changes.
TEST(an_empty_storage_answer_changes_nothing) {
    FakeModules modules;
    modules.storage = {QVariantMap{}};
    PackageModel model;
    CatalogClient client(&modules, &model);
    client.load();
    CHECK_EQ(client.storageState(), QString());
}

// A module that didn't answer is not an empty list: what was shown stays.
TEST(a_load_with_a_failed_read_keeps_the_last_list) {
    for (const QString& call : {"listRepositories", "getCatalog", "getInstalledPackages", "getValidVariants"}) {
        FakeModules modules;
        PackageModel model;
        CatalogClient client(&modules, &model);
        client.load();
        CHECK_EQ(model.rowCount(), 1);
        CHECK(model.index(0).data(PackageModel::IsInstalledRole).toBool());
        int loaded = 0, failed = 0;
        QObject::connect(&client, &CatalogClient::loaded, &client, [&] { ++loaded; });
        QObject::connect(&client, &CatalogClient::loadFailed, &client, [&] { ++failed; });
        modules.failing << call;
        client.refresh();
        CHECK_EQ(loaded, 0);
        CHECK_EQ(failed, 1);
        CHECK(!client.loadError().isEmpty());
        CHECK(!client.isLoading());
        CHECK_EQ(model.rowCount(), 1);
        CHECK(model.index(0).data(PackageModel::IsInstalledRole).toBool());
        CHECK_EQ(client.repositoryCount(), 2);
    }
}

// A first load that fails says so; the next good one clears it.
TEST(a_first_load_that_fails_says_so) {
    FakeModules modules;
    PackageModel model;
    CatalogClient client(&modules, &model);
    modules.failing << "getCatalog";
    client.load();
    CHECK(!client.hasLoaded());
    CHECK(!client.isLoading());
    CHECK_EQ(client.loadError(), QStringLiteral("the catalog"));
    modules.failing.clear();
    client.refresh();
    CHECK(client.hasLoaded());
    CHECK_EQ(client.loadError(), QString());
}

CHECK_MAIN()
