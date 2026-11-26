// One availability rule for both installer apps.

#include "check.h"

#include <logos/installer/Availability.h>

using namespace logos::installer;

namespace {

QVariantMap version(const QStringList& variants, const QString& type = QStringLiteral("core"))
{
    QVariantMap main;
    for (const QString& v : variants) main.insert(v, QStringLiteral("lib/x.so"));
    return {{"manifest", QVariantMap{{"main", main}, {"type", type}}}};
}

QVariantMap refused(QVariantMap v, const QString& requiredSource)
{
    v["sourceAvailable"] = false;
    v["requiredSource"] = requiredSource;
    return v;
}

const QStringList kHost{QStringLiteral("linux-x86_64")};
const QStringList kDevHost{QStringLiteral("linux-x86_64-dev")};

}  // namespace

TEST(variants_split_off_a_known_flavor_only) {
    CHECK_EQ(splitVariant("linux-x86_64-dev").first, QStringLiteral("linux-x86_64"));
    CHECK_EQ(splitVariant("linux-x86_64-dev").second, QStringLiteral("dev"));
    CHECK_EQ(splitVariant("linux-x86_64-portable").second, QStringLiteral("portable"));
    CHECK_EQ(splitVariant("linux-x86_64").first, QStringLiteral("linux-x86_64"));
    CHECK_EQ(splitVariant("linux-x86_64").second, QString());
    CHECK_EQ(splitVariant("darwin").first, QStringLiteral("darwin"));
}

TEST(a_host_variant_makes_it_available) {
    const Availability a = availabilityOf({version({"linux-x86_64", "darwin-arm64"})}, kHost);
    CHECK(a.variantAvailable);
    CHECK_EQ(a.variantReason, NotAvailableReason::Available);
    CHECK(a.hasInstallableVersion);
}

TEST(no_versions_means_nothing_published) {
    const Availability a = availabilityOf({}, kHost);
    CHECK(!a.variantAvailable);
    CHECK_EQ(a.variantReason, NotAvailableReason::NoVariantsPublished);
    CHECK(!a.hasInstallableVersion);
}

TEST(an_empty_main_is_nothing_published_unless_qml_only) {
    CHECK_EQ(availabilityOf({version({})}, kHost).variantReason,
             NotAvailableReason::NoVariantsPublished);
    CHECK(availabilityOf({version({}, "ui_qml")}, kHost).variantAvailable);
}

TEST(this_machine_in_another_flavor_is_a_flavor_mismatch) {
    CHECK_EQ(availabilityOf({version({"linux-x86_64"})}, kDevHost).variantReason,
             NotAvailableReason::BuildFlavorMismatch);
    CHECK_EQ(availabilityOf({version({"linux-x86_64-dev"})}, kHost).variantReason,
             NotAvailableReason::BuildFlavorMismatch);
}

TEST(another_machine_is_a_platform_mismatch) {
    CHECK_EQ(availabilityOf({version({"windows-x86_64"})}, kHost).variantReason,
             NotAvailableReason::PlatformMismatch);
}

// The variant comes from the newest version only.
TEST(the_newest_version_decides_the_variant) {
    const Availability a = availabilityOf(
        {version({"windows-x86_64"}), version({"linux-x86_64"})}, kHost);
    CHECK(!a.variantAvailable);
}

TEST(one_servable_version_keeps_the_package) {
    const Availability a = availabilityOf(
        {refused(version({"linux-x86_64"}), "http"), version({"linux-x86_64"})}, kHost);
    CHECK(a.hasInstallableVersion);
    CHECK_EQ(notAvailableReason(a, refused(version({"linux-x86_64"}), "http")),
             NotAvailableReason::NotOverHttp);
}

TEST(no_servable_version_is_not_installable) {
    const Availability a = availabilityOf({refused(version({"linux-x86_64"}), "logos")}, kHost);
    CHECK(a.variantAvailable);
    CHECK(!a.hasInstallableVersion);
}

TEST(the_variant_reason_comes_before_the_source) {
    const Availability a = availabilityOf({version({"windows-x86_64"})}, kHost);
    CHECK_EQ(notAvailableReason(a, refused(version({"windows-x86_64"}), "logos")),
             NotAvailableReason::PlatformMismatch);
}

// package_manager hasn't said yet: show everything rather than nothing.
TEST(unknown_host_variants_make_everything_available) {
    const Availability a = availabilityOf({version({"windows-x86_64"})}, {});
    CHECK(a.variantAvailable);
    CHECK_EQ(a.variantReason, NotAvailableReason::Available);
    CHECK(a.hasInstallableVersion);
    CHECK(!availabilityOf({refused(version({"windows-x86_64"}), "http")}, {}).hasInstallableVersion);
}

TEST(a_downloader_without_source_fields_serves_everything) {
    CHECK(sourceAvailable({}));
    CHECK_EQ(sourceUnavailableReason({}), NotAvailableReason::NotOnLogosStorage);
}

TEST(an_installed_package_is_never_hidden) {
    CHECK(isUnavailableOnThisPlatform(false, false));
    CHECK(!isUnavailableOnThisPlatform(false, true));
    CHECK(!isUnavailableOnThisPlatform(true, false));
}

CHECK_MAIN()
