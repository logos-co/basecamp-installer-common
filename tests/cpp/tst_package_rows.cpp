// The catalog row and Local row transforms, from logos-package-manager-ui's
// tests/test_package_rows.cpp.

#include "check.h"

#include <logos/installer/PackageRows.h>

#include <QVariantList>
#include <QVariantMap>

using namespace logos::installer;

namespace {

// The shape package_manager puts on the wire per installed package
// (PackageManagerImpl::toLogosMap): `dependencies` is a flat name list, and
// `dependencyConstraints` repeats ONLY the entries that declared a version
// range or a signer.
QVariantMap installedRecord(const QVariantList& dependencies,
                            const QVariantList& constraints = {})
{
    QVariantMap inst;
    inst["name"]        = QStringLiteral("token_list_ui");
    inst["displayName"] = QStringLiteral("Token Lists");
    inst["version"]     = QStringLiteral("0.1.0");
    inst["type"]        = QStringLiteral("ui_qml");
    inst["category"]    = QStringLiteral("wallet");
    inst["installType"] = QStringLiteral("user");
    QVariantMap hashes;
    hashes["root"] = QStringLiteral("6550574b0e76");
    inst["hashes"] = hashes;
    inst["dependencies"] = dependencies;
    if (!constraints.isEmpty()) inst["dependencyConstraints"] = constraints;
    return inst;
}

QVariantMap constraint(const QString& name, const QString& version)
{
    QVariantMap c;
    c["name"]    = name;
    c["version"] = version;
    return c;
}

}  // namespace

// The regression. A package no catalog publishes has the Local row as its
// ONLY surface, and this used to be hardcoded to an empty list — so the
// details panel said "Dependencies: None" for every one of them.
TEST(local_row_carries_the_installed_dependencies) {
    const QVariantMap row = buildLocalPackageRow(
        installedRecord({QStringLiteral("token_list_module")}));

    const QStringList deps = row.value("dependencies").toStringList();
    CHECK_EQ(deps.size(), 1);
    CHECK_EQ(deps.at(0), QStringLiteral("token_list_module"));
}

// The version range survives the split wire shape: package_manager sends the
// name in `dependencies` and the range alongside in `dependencyConstraints`,
// and the row has to put them back together.
TEST(local_row_rejoins_a_constrained_dependency) {
    const QVariantMap row = buildLocalPackageRow(
        installedRecord({QStringLiteral("token_list_module")},
                        {constraint(QStringLiteral("token_list_module"),
                                    QStringLiteral("~0.1.0"))}));

    const QStringList deps = row.value("dependencies").toStringList();
    CHECK_EQ(deps.size(), 1);
    CHECK_EQ(deps.at(0), QStringLiteral("token_list_module ~0.1.0"));
}

// `dependencies` is the edge list; a constraint naming something absent from
// it must not invent an edge, and the declared order is preserved.
TEST(local_row_takes_its_edges_from_dependencies_alone) {
    const QVariantMap row = buildLocalPackageRow(
        installedRecord({QStringLiteral("eth_rpc_module"),
                         QStringLiteral("keystore_module")},
                        {constraint(QStringLiteral("keystore_module"),
                                    QStringLiteral("~0.1.0")),
                         constraint(QStringLiteral("never_declared"),
                                    QStringLiteral("~9.9.9"))}));

    const QStringList deps = row.value("dependencies").toStringList();
    CHECK_EQ(deps.size(), 2);
    CHECK_EQ(deps.at(0), QStringLiteral("eth_rpc_module"));
    CHECK_EQ(deps.at(1), QStringLiteral("keystore_module ~0.1.0"));
}

TEST(local_row_with_no_declared_dependencies_stays_empty) {
    const QVariantMap row = buildLocalPackageRow(installedRecord({}));
    CHECK(row.value("dependencies").toStringList().isEmpty());
}

// A catalog row keeps the manifest's object form rendering identically — the
// two paths share one renderer precisely so they can't drift apart again.
TEST(catalog_row_renders_an_object_form_dependency_the_same_way) {
    QVariantMap dep;
    dep["name"]    = QStringLiteral("token_list_module");
    dep["version"] = QStringLiteral("~0.1.0");

    QVariantMap manifest;
    manifest["name"]         = QStringLiteral("token_list_ui");
    manifest["version"]      = QStringLiteral("0.1.0");
    manifest["type"]         = QStringLiteral("ui_qml");
    manifest["dependencies"] = QVariantList{dep};

    QVariantMap version;
    version["manifest"] = manifest;
    version["rootHash"] = QStringLiteral("6550574b0e76");

    QVariantMap catalogRow;
    catalogRow["name"]     = QStringLiteral("token_list_ui");
    catalogRow["versions"] = QVariantList{version};

    const QVariantMap row = buildPackageRow(catalogRow, {}, {});

    const QStringList deps = row.value("dependencies").toStringList();
    CHECK_EQ(deps.size(), 1);
    CHECK_EQ(deps.at(0), QStringLiteral("token_list_module ~0.1.0"));
}

// ─── Provenance ───────────────────────────────────────────────────────────────
//
// A catalog can draw packages from other catalogs. Such a row is deliberately
// stamped with the CONFIGURED repository's url — the list groups on that, and
// an aggregate catalog would otherwise render as no section at all — so
// `repository*` alone cannot say whose bytes a row carries. `origin*` is what
// answers that, and it has to survive the transform to reach the details panel.

namespace {

// A catalog row as `package_downloader.getCatalog()` emits it, with the
// repository / origin pair the caller wants to exercise.
QVariantMap provenanceRow(const QVariantList& versions,
                          const QString& originName = {},
                          const QString& originDisplay = {})
{
    QVariantMap row;
    row["name"]                  = QStringLiteral("chat_module");
    row["versions"]              = versions;
    row["repositoryUrl"]         = QStringLiteral("https://distro.example/logos-repo.json");
    row["repositoryName"]        = QStringLiteral("my-distro");
    row["repositoryDisplayName"] = QStringLiteral("My Distro");
    if (!originName.isEmpty()) {
        row["originRepositoryUrl"]         = QStringLiteral("https://b.example/logos-repo.json");
        row["originRepositoryName"]        = originName;
        row["originRepositoryDisplayName"] = originDisplay;
    }
    return row;
}

QVariantMap catalogVersion(const QString& version, const QString& hash,
                           const QString& originName = {})
{
    QVariantMap manifest;
    manifest["name"]    = QStringLiteral("chat_module");
    manifest["version"] = version;
    manifest["type"]    = QStringLiteral("core");
    QVariantMap v;
    v["manifest"] = manifest;
    v["rootHash"] = hash;
    if (!originName.isEmpty()) {
        v["originRepositoryName"] = originName;
        v["originRepositoryUrl"]  =
            QStringLiteral("https://%1.example/logos-repo.json").arg(originName);
    }
    return v;
}

}  // namespace

TEST(a_drawn_in_row_keeps_the_configured_repository_and_names_its_origin) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_b"))},
                      QStringLiteral("team-b"), QStringLiteral("Team B")),
        {}, {});

    CHECK_EQ(row.value("repositoryName").toString(), QStringLiteral("my-distro"));
    CHECK_EQ(row.value("originRepositoryName").toString(), QStringLiteral("team-b"));
    CHECK_EQ(row.value("originRepositoryDisplayName").toString(),
                    QStringLiteral("Team B"));
    CHECK_EQ(row.value("originRepositoryUrl").toString(),
                    QStringLiteral("https://b.example/logos-repo.json"));
}

// A catalog that draws from nobody has origin == repository by definition, and
// so does every row from a downloader predating the field. One shape for every
// row means the details panel needs no special case for the absence.
TEST(a_row_with_no_origin_falls_back_to_its_repository) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_a"))}),
        {}, {});

    CHECK_EQ(row.value("originRepositoryName").toString(),
                    row.value("repositoryName").toString());
    CHECK_EQ(row.value("originRepositoryDisplayName").toString(),
                    row.value("repositoryDisplayName").toString());
    CHECK_EQ(row.value("originRepositoryUrl").toString(),
                    row.value("repositoryUrl").toString());
}

// Present-but-empty is the shape a source repository that never resolved
// produces. The fallback is still the better answer than a blank label.
TEST(an_empty_origin_is_treated_as_absent) {
    QVariantMap raw = provenanceRow(
        QVariantList{catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"))});
    raw["originRepositoryName"]        = QString();
    raw["originRepositoryDisplayName"] = QString();

    const QVariantMap row = buildPackageRow(raw, {}, {});
    CHECK_EQ(row.value("originRepositoryName").toString(),
                    QStringLiteral("my-distro"));
}

// After a merge one package's versions can come from several catalogs, so the
// dropdown can offer 1.2.0 from one and 1.0.0 from another.
TEST(each_version_carries_the_catalog_that_published_it) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{
                          catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_b"),
                                         QStringLiteral("team-b")),
                          catalogVersion(QStringLiteral("1.0.0"), QStringLiteral("h_root"),
                                         QStringLiteral("my-distro")),
                      },
                      QStringLiteral("team-b"), QStringLiteral("Team B")),
        {}, {});

    const QVariantList avail = row.value("availableVersions").toList();
    CHECK_EQ(avail.size(), 2);
    CHECK_EQ(avail.at(0).toMap().value("originRepositoryName").toString(),
                    QStringLiteral("team-b"));
    CHECK_EQ(avail.at(1).toMap().value("originRepositoryName").toString(),
                    QStringLiteral("my-distro"));
}

// A version entry with no origin of its own belongs to whoever published the
// package — not to nobody.
TEST(a_version_without_its_own_origin_inherits_the_packages) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_b"))},
                      QStringLiteral("team-b"), QStringLiteral("Team B")),
        {}, {});

    const QVariantList avail = row.value("availableVersions").toList();
    CHECK_EQ(avail.size(), 1);
    CHECK_EQ(avail.at(0).toMap().value("originRepositoryName").toString(),
                    QStringLiteral("team-b"));
}

// Nothing published a Local row anywhere, so the "is this drawn from
// elsewhere" comparison must simply be false rather than undefined.
TEST(a_local_row_reports_itself_as_its_own_origin) {
    const QVariantMap row = buildLocalPackageRow(installedRecord({}));
    CHECK_EQ(row.value("originRepositoryName").toString(),
                    row.value("repositoryName").toString());
    CHECK_EQ(row.value("originRepositoryDisplayName").toString(),
                    QStringLiteral("local"));
}

TEST(a_local_row_downloaded_from_storage_has_the_storage_source) {
    QVariantMap inst = installedRecord({});
    inst["source"] = QStringLiteral("logos:zDvZRwzm");

    const QVariantMap row = buildLocalPackageRow(inst);
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::Storage));
}

TEST(a_catalog_row_downloaded_from_storage_has_the_storage_source) {
    QVariantMap inst;
    inst["name"]        = QStringLiteral("chat_module");
    inst["version"]     = QStringLiteral("1.2.0");
    inst["installType"] = QStringLiteral("user");
    inst["source"]      = QStringLiteral("logos:zDvZRwzm");
    QHash<QString, QVariantMap> installedByName;
    installedByName.insert(QStringLiteral("chat_module"), inst);

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_a"))}),
        installedByName, {});
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::Storage));
}

TEST(a_row_downloaded_from_a_url_has_the_github_source) {
    QVariantMap inst = installedRecord({});
    inst["source"] = QStringLiteral("https://github.com/logos-co/chat/releases/chat.lgx");

    const QVariantMap row = buildLocalPackageRow(inst);
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::GitHub));
}

// Installed before sources were recorded.
TEST(a_row_downloaded_without_a_recorded_source_has_the_github_source) {
    const QVariantMap row = buildLocalPackageRow(installedRecord({}));
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::GitHub));
}

TEST(a_row_installed_from_a_local_file_has_the_local_file_source) {
    QVariantMap inst = installedRecord({});
    inst["source"] = QStringLiteral("file:///home/user/chat.lgx");

    const QVariantMap row = buildLocalPackageRow(inst);
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::LocalFile));
}

TEST(an_embedded_row_has_the_builtin_source) {
    QVariantMap inst = installedRecord({});
    inst["installType"] = QStringLiteral("embedded");

    const QVariantMap row = buildLocalPackageRow(inst);
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::Builtin));
}

TEST(a_catalog_row_not_installed_has_no_source) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_a"))}),
        {}, {});
    CHECK_EQ(row.value("downloadSource").toInt(),
                    toInt(DownloadSource::NoSource));
}

TEST(a_version_usable_over_storage_and_http_offers_both_sources) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["usableSources"] = QVariantList{QStringLiteral("http"), QStringLiteral("logos")};

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, {});
    const QVariantList sources =
        row.value("availableVersions").toList().at(0).toMap().value("sources").toList();
    CHECK(sources == (QVariantList{toInt(DownloadSource::Storage),
                                               toInt(DownloadSource::GitHub)}));
}

TEST(a_version_usable_over_storage_alone_offers_only_storage) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["usableSources"] = QVariantList{QStringLiteral("logos")};

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, {});
    const QVariantList sources =
        row.value("availableVersions").toList().at(0).toMap().value("sources").toList();
    CHECK(sources == (QVariantList{toInt(DownloadSource::Storage)}));
}

TEST(a_version_usable_over_http_alone_offers_only_github) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["usableSources"] = QVariantList{QStringLiteral("http")};

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, {});
    const QVariantList sources =
        row.value("availableVersions").toList().at(0).toMap().value("sources").toList();
    CHECK(sources == (QVariantList{toInt(DownloadSource::GitHub)}));
}

// ── Download source ──────────────────────────────────────────────────────

namespace {

// A version this platform can install, so only the download source decides.
QVariantMap installableVersion(const QString& version, const QString& hash)
{
    QVariantMap v = catalogVersion(version, hash);
    QVariantMap manifest = v["manifest"].toMap();
    manifest["main"] = QVariantMap{{QStringLiteral("linux-x86_64"), QStringLiteral("lib/chat.so")}};
    v["manifest"] = manifest;
    return v;
}

const QStringList kValidVariants{QStringLiteral("linux-x86_64")};

}  // namespace

TEST(a_version_the_download_source_cannot_serve_is_not_available) {
    QVariantMap v = installableVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["sourceAvailable"] = false;
    v["requiredSource"] = QStringLiteral("logos");

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, kValidVariants);

    CHECK(row.value("isVariantAvailable").toBool());
    CHECK(!(row.value("isSourceAvailable").toBool()));
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::NotAvailable));
    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NotOnLogosStorage));
}

TEST(http_only_reports_its_own_reason) {
    QVariantMap v = installableVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["sourceAvailable"] = false;
    v["requiredSource"] = QStringLiteral("http");

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NotOverHttp));
}

// No download source setting can fix a platform mismatch, so that reason wins.
TEST(the_variant_reason_wins_over_the_download_source) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["sourceAvailable"] = false;
    v["requiredSource"] = QStringLiteral("logos");

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NoVariantsPublished));
}

// A downloader that predates the setting sends none of the fields.
TEST(a_version_without_source_fields_stays_installable) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{installableVersion(QStringLiteral("1.2.0"),
                                                      QStringLiteral("h_a"))}),
        {}, kValidVariants);

    CHECK(row.value("isSourceAvailable").toBool());
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::Install));
    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::Available));
}

// The Version picker mirrors the pick through this helper (setRowVersion).
TEST(picking_another_version_follows_its_download_source) {
    QVariantMap older = installableVersion(QStringLiteral("1.1.0"), QStringLiteral("h_b"));
    older["sourceAvailable"] = false;
    older["requiredSource"] = QStringLiteral("http");
    QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{installableVersion(QStringLiteral("1.2.0"),
                                                      QStringLiteral("h_a")),
                                   older}),
        {}, kValidVariants);

    selectVersion(row, 1);
    CHECK(!(row.value("isSourceAvailable").toBool()));
    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NotOverHttp));

    selectVersion(row, 0);
    CHECK(row.value("isSourceAvailable").toBool());
    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::Available));
}

// ── Platform availability: which rows the list drops ──────────────────────
//
// A catalog is published per OS/arch, so on a platform with few variants most
// of it is packages the user can never install. Those rows are dropped
// outright (PackagesFilterProxy) and left out of the Categories / Types
// lists — but only the two reasons nothing the user does can fix.

TEST(a_package_with_no_variant_for_this_platform_is_dropped) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    QVariantMap manifest = v["manifest"].toMap();
    manifest["main"] = QVariantMap{
        {QStringLiteral("windows-x86_64"), QStringLiteral("lib/chat.dll")}};
    v["manifest"] = manifest;

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::PlatformMismatch));
    CHECK(isUnavailableOnThisPlatform(row));
}

TEST(a_package_publishing_no_variants_at_all_is_dropped) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{catalogVersion(QStringLiteral("1.2.0"),
                                                  QStringLiteral("h_a"))}),
        {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NoVariantsPublished));
    CHECK(isUnavailableOnThisPlatform(row));
}

TEST(an_installable_package_is_kept) {
    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{installableVersion(QStringLiteral("1.2.0"),
                                                      QStringLiteral("h_a"))}),
        {}, kValidVariants);

    CHECK(!(isUnavailableOnThisPlatform(row)));
}

// A dev-flavor host accepts ONLY "-dev" variants -- platformVariantsToTry
// replaces the bare spelling rather than adding to it -- so a release-only
// package is as uninstallable there as a foreign OS is. This is the case that
// dominates a developer's list, which is why it is dropped too.
TEST(a_build_flavor_mismatch_is_dropped) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    QVariantMap manifest = v["manifest"].toMap();
    manifest["main"] = QVariantMap{
        {QStringLiteral("linux-x86_64-dev"), QStringLiteral("lib/chat.so")}};
    v["manifest"] = manifest;

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::BuildFlavorMismatch));
    CHECK(isUnavailableOnThisPlatform(row));
}

// Every version refused by the download source: nothing to install, so the
// row goes. Recoverable in Settings, but so is a build flavor.
TEST(a_package_no_version_of_which_the_source_will_serve_is_dropped) {
    QVariantMap v = installableVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    v["sourceAvailable"] = false;
    v["requiredSource"] = QStringLiteral("http");

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}), {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NotOverHttp));
    CHECK(isUnavailableOnThisPlatform(row));
}

// ONE servable version is enough to keep the row, even when the newest is
// refused and so the row currently reads "Not available". Filtering on the
// selected version instead would both hide this package -- whose 1.1.0 is
// installable -- and make a row disappear as the user worked its dropdown.
TEST(a_package_with_one_servable_version_is_kept) {
    QVariantMap newest = installableVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    newest["sourceAvailable"] = false;
    newest["requiredSource"] = QStringLiteral("http");

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{newest,
                                   installableVersion(QStringLiteral("1.1.0"),
                                                      QStringLiteral("h_b"))}),
        {}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::NotOverHttp));
    CHECK(!(isUnavailableOnThisPlatform(row)));
}

// The catalog dropped our variant AFTER the package was installed. Hiding it
// would take its Uninstall button with it — the row is the only surface the
// on-disk copy has.
TEST(an_installed_package_survives_losing_its_variant) {
    QVariantMap v = catalogVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    QVariantMap manifest = v["manifest"].toMap();
    manifest["main"] = QVariantMap{
        {QStringLiteral("windows-x86_64"), QStringLiteral("lib/chat.dll")}};
    v["manifest"] = manifest;

    QVariantMap installed;
    installed["name"]        = QStringLiteral("chat_module");
    installed["version"]     = QStringLiteral("1.2.0");
    installed["installType"] = QStringLiteral("user");

    const QVariantMap row = buildPackageRow(
        provenanceRow(QVariantList{v}),
        {{QStringLiteral("chat_module"), installed}}, kValidVariants);

    CHECK_EQ(row.value("notAvailableReason").toInt(),
                    toInt(NotAvailableReason::PlatformMismatch));
    CHECK(!(isUnavailableOnThisPlatform(row)));
}

// No catalog publishes it, so there is no variant list to check at all.
TEST(a_local_row_is_never_dropped) {
    const QVariantMap row = buildLocalPackageRow(
        installedRecord({QStringLiteral("token_list_module")}));

    CHECK(!(isUnavailableOnThisPlatform(row)));
}

// ── From Basecamp's App Manager rows ─────────────────────────────────────

namespace {

QVariantMap appCatalogRow(const QVariantList& provides, const QString& icon,
                          const QString& manifestVersion)
{
    QVariantMap v = installableVersion(QStringLiteral("1.2.0"), QStringLiteral("h_a"));
    QVariantMap manifest = v["manifest"].toMap();
    manifest["provides"] = provides;
    v["manifest"] = manifest;
    QVariantMap row = provenanceRow(QVariantList{v});
    row["icon"] = icon;
    row["manifestVersion"] = manifestVersion;
    return row;
}

}  // namespace

TEST(a_row_lists_the_intents_it_provides_and_which_it_publishes_to_links) {
    const QVariantMap row = buildPackageRow(
        appCatalogRow({QStringLiteral("chat.open"),
                       QVariantMap{{"intent", "chat.send"}, {"web", true}},
                       QVariantMap{{"intent", "chat.share"}, {"web", "true"}}},
                      {}, {}),
        {}, kValidVariants);
    CHECK_EQ(row.value("provides").toStringList(),
             (QStringList{"chat.open", "chat.send", "chat.share"}));
    // "true" as a string is not a boolean: never published.
    CHECK_EQ(row.value("webProvides").toStringList(), QStringList{"chat.send"});
}

TEST(a_catalog_icon_is_kept_only_as_a_url) {
    CHECK_EQ(buildPackageRow(appCatalogRow({}, "icon.png", {}), {}, {})
                 .value("catalogIconUrl").toString(),
             QString());
    CHECK_EQ(buildPackageRow(appCatalogRow({}, "https://r.example/icon.png", {}), {}, {})
                 .value("catalogIconUrl").toString(),
             QStringLiteral("https://r.example/icon.png"));
}

TEST(a_row_is_listed_under_its_repository_url_then_name_then_local) {
    CHECK_EQ(sourceKeyOf({{"repositoryUrl", "https://r"}, {"repositoryName", "r"}}),
             QStringLiteral("https://r"));
    CHECK_EQ(sourceKeyOf({{"repositoryName", "r"}}), QStringLiteral("r"));
    CHECK_EQ(sourceKeyOf({}), QStringLiteral("local"));
    CHECK_EQ(buildLocalPackageRow(installedRecord({})).value("sourceKey").toString(),
             QStringLiteral("local"));
}

TEST(a_full_bleed_icon_needs_a_0_4_manifest) {
    CHECK(!supportsFullBleedIcon(QString()));
    CHECK(!supportsFullBleedIcon(QStringLiteral("0.3.9")));
    CHECK(supportsFullBleedIcon(QStringLiteral("0.4.0")));
    CHECK(supportsFullBleedIcon(QStringLiteral("1.0")));
    CHECK(!supportsFullBleedIcon(QStringLiteral("x.4")));
    CHECK(buildPackageRow(appCatalogRow({}, {}, "0.4.0"), {}, {}).value("supportsFullBleedIcon").toBool());
    CHECK(!buildPackageRow(appCatalogRow({}, {}, {}), {}, {}).value("supportsFullBleedIcon").toBool());
}

TEST(dependency_entries_take_plain_and_object_forms) {
    const QVariantList entries = dependencyEntries({
        QStringLiteral("a"),
        QVariantMap{{"name", "b"}, {"version", ">=1"}, {"signer", "did:key:z"}},
        QVariantMap{{"version", "1"}},
    });
    CHECK_EQ(entries.size(), 2);
    CHECK_EQ(entries.at(0).toMap().value("name").toString(), QStringLiteral("a"));
    CHECK_EQ(entries.at(0).toMap().value("version").toString(), QString());
    CHECK_EQ(entries.at(1).toMap().value("signer").toString(), QStringLiteral("did:key:z"));
    CHECK_EQ(renderDependencies({QVariantMap{{"name", "b"}, {"version", ">=1"}, {"signer", "did:key:z"}}}),
             QStringList{"b >=1 [signer=did:key:z]"});
}

// ── Selecting a version ──────────────────────────────────────────────────

namespace {

QVariantMap installedRowAt(const QString& installedVersion)
{
    QVariantMap installed;
    installed["name"]        = QStringLiteral("chat_module");
    installed["version"]     = installedVersion;
    installed["installType"] = QStringLiteral("user");
    installed["hashes"]      = QVariantMap{{"root", "h_b"}};
    return buildPackageRow(
        provenanceRow(QVariantList{installableVersion("1.2.0", "h_a"),
                                   installableVersion("1.1.0", "h_b")}),
        {{QStringLiteral("chat_module"), installed}}, kValidVariants);
}

}  // namespace

TEST(the_action_follows_the_selected_version) {
    QVariantMap row = installedRowAt(QStringLiteral("1.1.0"));
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::Upgrade));
    CHECK(row.value("updateAvailable").toBool());

    selectVersion(row, 1);
    CHECK_EQ(row.value("version").toString(), QStringLiteral("1.1.0"));
    CHECK_EQ(row.value("hash").toString(), QStringLiteral("h_b"));
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::NoOp));
    // The status is the newest against disk, whatever is selected.
    CHECK_EQ(row.value("installStatus").toInt(), toInt(InstallStatus::UpgradeAvailable));
    CHECK(row.value("updateAvailable").toBool());
}

TEST(selecting_an_older_version_offers_a_downgrade) {
    QVariantMap row = installedRowAt(QStringLiteral("1.2.0"));
    selectVersion(row, 1);
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::Downgrade));
}

TEST(an_out_of_range_selection_selects_the_newest) {
    QVariantMap row = installedRowAt(QStringLiteral("1.1.0"));
    selectVersion(row, 7);
    CHECK_EQ(row.value("selectedVersionIndex").toInt(), 0);
    CHECK_EQ(row.value("version").toString(), QStringLiteral("1.2.0"));
}

TEST(a_failed_row_offers_retry_and_one_in_flight_offers_nothing) {
    QVariantMap row = installedRowAt(QStringLiteral("1.1.0"));
    row["installStatus"] = toInt(InstallStatus::Failed);
    selectVersion(row, 0);
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::Retry));
    row["installStatus"] = toInt(InstallStatus::Installing);
    selectVersion(row, 0);
    CHECK_EQ(row.value("rowAction").toInt(), toInt(RowAction::NoOp));
}

CHECK_MAIN()
