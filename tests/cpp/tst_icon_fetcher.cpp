// The catalog's icons, fetched and checked by the backend, shown by the view
// as its own copy.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/IconFetcher.h>
#include <logos/installer/PackageModel.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace logos::installer;
using namespace fixtures;

namespace {

// Enough of a PNG for the checks: the signature and an IHDR chunk.
QByteArray png(quint32 width = 256, quint32 height = 256, int padding = 0)
{
    QByteArray d("\x89PNG\r\n\x1a\n", 8);
    d.append(QByteArray("\x00\x00\x00\x0d", 4)).append("IHDR");
    for (quint32 v : {width, height}) {
        d.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
    }
    d.append(QByteArray("\x08\x06\x00\x00\x00\x00\x00\x00\x00", 9));
    d.append(QByteArray(padding, 'x'));
    return d;
}

QString sha(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QVariantMap withIcon(QVariantMap row, const QString& url, const QString& sha256,
                     const QString& manifestVersion = {})
{
    row.insert("icon", url);
    row.insert("manifestVersion", manifestVersion);
    QVariantList versions = row.value("versions").toList();
    QVariantMap newest = versions.first().toMap();
    newest.insert("icon", QVariantMap{{"path", "icons/x.png"}, {"sha256", sha256}});
    versions[0] = newest;
    row.insert("versions", versions);
    return row;
}

// Serves fixed bytes per URL, at once or when told.
struct FakeServer {
    QHash<QString, QByteArray> files;
    QStringList asked;
    bool hold = false;
    QList<std::function<void()>> held;

    IconFetcher::Fetch fetch()
    {
        return [this](const QUrl& url, IconFetcher::Done done) {
            asked << url.toString();
            auto reply = [this, url, done] {
                const auto it = files.constFind(url.toString());
                if (it == files.cend()) done({}, "404");
                else done(*it, {});
            };
            if (hold) held.append(reply);
            else reply();
        };
    }
    void flush() { while (!held.isEmpty()) held.takeFirst()(); }
};

QString iconOf(const PackageModel& model, const QString& name)
{
    return roleOf(model, name, "iconUrl").toString();
}

const QString kUrl = QStringLiteral("https://repo.example/icons/wallet.png");

}  // namespace

TEST(a_catalog_icon_becomes_the_rows_own_copy) {
    const QByteArray icon = png();
    FakeServer server;
    server.files.insert(kUrl, icon);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(icon), "0.4.0")});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(iconOf(model, "wallet_ui"),
             QStringLiteral("data:image/png;base64,") + QString::fromLatin1(icon.toBase64()));
    CHECK(roleOf(model, "wallet_ui", "supportsFullBleedIcon").toBool());
    CHECK_EQ(server.asked, QStringList{kUrl});
}

TEST(an_icon_not_matching_its_hash_is_refused) {
    FakeServer server;
    server.files.insert(kUrl, png());
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha("other"))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    QStringList refused;
    QObject::connect(&fetcher, &IconFetcher::iconRefused, &fetcher,
                     [&](const QString& name, const QString&) { refused << name; });
    SPIN(20);
    CHECK_EQ(iconOf(model, "wallet_ui"), QString());
    CHECK_EQ(refused, QStringList{"wallet_ui"});
}

TEST(only_a_png_within_limits_is_an_icon) {
    CHECK_EQ(IconFetcher::refusal(png(), {}), QString());
    CHECK_EQ(IconFetcher::refusal(png(), sha(png())), QString());
    CHECK(!IconFetcher::refusal("<svg/>", {}).isEmpty());
    CHECK(!IconFetcher::refusal(png().left(20), {}).isEmpty());
    CHECK(!IconFetcher::refusal(png(5000, 10), {}).isEmpty());
    CHECK(!IconFetcher::refusal(png(0, 10), {}).isEmpty());
    CHECK(!IconFetcher::refusal(png(10, 10, int(IconFetcher::kMaxBytes)), {}).isEmpty());
}

// No hash in the catalog: a PNG within the limits is enough.
TEST(an_icon_the_catalog_names_no_hash_for_is_still_checked) {
    FakeServer server;
    server.files.insert(kUrl, png());
    server.files.insert("https://repo.example/bad.png", QByteArray("GIF89a"));
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "a", "1.0", "H"), kUrl, {}),
                      withIcon(catalogRow("r", "b", "1.0", "H"), "https://repo.example/bad.png", {})});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK(!iconOf(model, "a").isEmpty());
    CHECK(iconOf(model, "b").isEmpty());
}

// One fetch per icon: packages and repositories share it by its hash.
TEST(a_shared_icon_is_fetched_once) {
    const QByteArray icon = png();
    FakeServer server;
    server.files.insert(kUrl, icon);
    server.files.insert("https://mirror.example/same.png", icon);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r1", "wallet_ui", "1.0", "H"), kUrl, sha(icon)),
                      withIcon(catalogRow("r2", "wallet_ui", "1.0", "H"), kUrl, sha(icon)),
                      withIcon(catalogRow("r1", "wallet_lite", "1.0", "H"),
                               "https://mirror.example/same.png", sha(icon))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(server.asked.size(), 1);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
    CHECK_EQ(iconOf(model, "wallet_lite"), iconOf(model, "wallet_ui"));
}

TEST(a_refresh_doesnt_fetch_again) {
    const QByteArray icon = png();
    FakeServer server;
    server.files.insert(kUrl, icon);
    PackageModel model;
    const QVariantMap row = withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(icon));
    model.setCatalog({row});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    model.setCatalog({row, catalogRow("r", "chat", "1.0", "H")});
    SPIN(20);
    CHECK_EQ(server.asked.size(), 1);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
}

TEST(icons_are_kept_on_disk_by_hash) {
    QTemporaryDir dir;
    const QByteArray icon = png();
    FakeServer server;
    server.files.insert(kUrl, icon);
    const QVariantMap row = withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(icon));
    {
        PackageModel model;
        model.setCatalog({row});
        IconFetcher fetcher(&model);
        fetcher.setCacheDirectory(dir.path());
        fetcher.setFetch(server.fetch());
        SPIN(20);
    }
    CHECK(QFile::exists(QDir(dir.path()).filePath(sha(icon) + ".png")));

    PackageModel model;
    model.setCatalog({row});
    IconFetcher fetcher(&model);
    fetcher.setCacheDirectory(dir.path());
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(server.asked.size(), 1);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
}

// What is on disk is checked like what arrives: a damaged copy is fetched again.
TEST(a_damaged_copy_on_disk_is_fetched_again) {
    QTemporaryDir dir;
    const QByteArray icon = png();
    QFile damaged(QDir(dir.path()).filePath(sha(icon) + ".png"));
    CHECK(damaged.open(QIODevice::WriteOnly));
    damaged.write("not the icon");
    damaged.close();
    FakeServer server;
    server.files.insert(kUrl, icon);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(icon))});
    IconFetcher fetcher(&model);
    fetcher.setCacheDirectory(dir.path());
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(server.asked.size(), 1);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
}

// The hash names the file and comes from the catalog.
TEST(a_hash_that_isnt_one_never_names_a_file) {
    QTemporaryDir dir;
    FakeServer server;
    server.files.insert(kUrl, png());
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, "../../escape")});
    IconFetcher fetcher(&model);
    fetcher.setCacheDirectory(dir.path() + "/icons");
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK(iconOf(model, "wallet_ui").isEmpty());
    CHECK(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    CHECK(QDir(dir.path() + "/icons").entryList(QDir::Files).isEmpty());
}

TEST(a_refused_icon_waits_for_a_retry) {
    const QByteArray icon = png();
    FakeServer server;
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(icon))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(server.asked.size(), 1);
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.1", "H2"), kUrl, sha(icon))});
    SPIN(20);
    CHECK_EQ(server.asked.size(), 1);
    server.files.insert(kUrl, icon);
    fetcher.retry();
    SPIN(20);
    CHECK_EQ(server.asked.size(), 2);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
}

TEST(no_more_than_four_fetches_at_once) {
    FakeServer server;
    server.hold = true;
    QVariantList catalog;
    for (int i = 0; i < 6; ++i) {
        const QByteArray icon = png(16 + i, 16);
        const QString url = QStringLiteral("https://repo.example/%1.png").arg(i);
        server.files.insert(url, icon);
        catalog.append(withIcon(catalogRow("r", QStringLiteral("p%1").arg(i), "1.0", "H"), url, sha(icon)));
    }
    PackageModel model;
    model.setCatalog(catalog);
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(server.asked.size(), IconFetcher::kParallel);
    server.flush();
    SPIN(20);
    server.flush();
    CHECK_EQ(server.asked.size(), 6);
    for (int i = 0; i < 6; ++i) CHECK(!iconOf(model, QStringLiteral("p%1").arg(i)).isEmpty());
}

TEST(an_icon_the_catalog_drops_goes) {
    const QByteArray icon = png();
    FakeServer server;
    server.files.insert(kUrl, icon);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(icon))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
    model.setCatalog({catalogRow("r", "wallet_ui", "1.1", "H")});
    SPIN(20);
    CHECK(iconOf(model, "wallet_ui").isEmpty());
}

// Without a fetch of its own, only http(s) is fetched.
TEST(only_http_urls_are_fetched) {
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), "file:///etc/passwd", {})});
    IconFetcher fetcher(&model);
    QString reason;
    QObject::connect(&fetcher, &IconFetcher::iconRefused, &fetcher,
                     [&](const QString&, const QString& why) { reason = why; });
    SPIN(20);
    CHECK_EQ(reason, QStringLiteral("not an http(s) URL"));
    CHECK(iconOf(model, "wallet_ui").isEmpty());
}

QString iconIn(const PackageModel& model, const QString& repositoryUrl, const QString& name)
{
    return model.index(model.rowOf(name, repositoryUrl)).data(PackageModel::IconUrlRole).toString();
}

// Two repositories, one name: each row shows its own publisher's icon.
TEST(the_same_name_from_two_repositories_shows_each_its_own_icon) {
    const QString otherUrl = QStringLiteral("https://other.example/icons/wallet.png");
    const QByteArray mine = png(), theirs = png(128, 128);
    FakeServer server;
    server.files.insert(kUrl, mine);
    server.files.insert(otherUrl, theirs);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r1", "wallet_ui", "1.0", "H"), kUrl, sha(mine)),
                      withIcon(catalogRow("r2", "wallet_ui", "1.0", "H2"), otherUrl, sha(theirs))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(20);
    CHECK_EQ(iconIn(model, "r1", "wallet_ui"), "data:image/png;base64," + QString::fromLatin1(mine.toBase64()));
    CHECK_EQ(iconIn(model, "r2", "wallet_ui"), "data:image/png;base64," + QString::fromLatin1(theirs.toBase64()));
}

// An icon that comes after the catalog named another is not shown.
TEST(an_icon_the_catalog_replaced_while_it_came_is_not_shown) {
    const QString newUrl = QStringLiteral("https://repo.example/icons/wallet-2.png");
    const QByteArray before = png(), after = png(128, 128);
    FakeServer server;
    server.hold = true;
    server.files.insert(kUrl, before);
    server.files.insert(newUrl, after);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(before))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(10);
    CHECK_EQ(server.held.size(), 1);
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.1", "H2"), newUrl, sha(after))});
    SPIN(10);
    // The new icon lands first, then the old one.
    server.held.takeLast()();
    server.flush();
    CHECK_EQ(iconOf(model, "wallet_ui"), "data:image/png;base64," + QString::fromLatin1(after.toBase64()));
}

// A new icon that is refused: the old one doesn't stay in its place.
TEST(an_icon_replaced_by_one_that_is_refused_goes) {
    const QString newUrl = QStringLiteral("https://repo.example/icons/wallet-2.png");
    const QByteArray before = png();
    FakeServer server;
    server.files.insert(kUrl, before);
    server.files.insert(newUrl, "not a png");
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(before))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(10);
    CHECK(!iconOf(model, "wallet_ui").isEmpty());
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.1", "H2"), newUrl, sha("not a png"))});
    SPIN(10);
    CHECK_EQ(iconOf(model, "wallet_ui"), QString());
}

// A reply that lands before the next scan is checked against the model.
TEST(an_old_icon_arriving_before_the_next_scan_is_not_shown) {
    const QString newUrl = QStringLiteral("https://repo.example/icons/wallet-2.png");
    const QByteArray before = png(), after = png(128, 128);
    FakeServer server;
    server.hold = true;
    server.files.insert(kUrl, before);
    server.files.insert(newUrl, after);
    PackageModel model;
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.0", "H"), kUrl, sha(before))});
    IconFetcher fetcher(&model);
    fetcher.setFetch(server.fetch());
    SPIN(10);
    model.setCatalog({withIcon(catalogRow("r", "wallet_ui", "1.1", "H2"), newUrl, sha(after))});
    server.flush();
    CHECK_EQ(iconOf(model, "wallet_ui"), QString());
}

CHECK_MAIN()
