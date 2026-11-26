// What a row is against disk, and what its action does.

#include "check.h"

#include <logos/installer/RowState.h>

using namespace logos::installer;

TEST(versions_order_by_semver) {
    CHECK_EQ(versionCompare("1.0.0-rc.1", "1.0.0"), -1);
    CHECK_EQ(versionCompare("1.0.0-rc.2", "1.0.0-rc.10"), -1);
    CHECK_EQ(versionCompare("1.10.0", "1.9.0"), 1);
    CHECK_EQ(versionCompare("1.2.0", "1.2.0"), 0);
}

TEST(status_against_the_release) {
    CHECK_EQ(installStatusOf(false, {}, {}, "1.0.0", "h"), InstallStatus::NotInstalled);
    CHECK_EQ(installStatusOf(true, "0.9.0", "h", "1.0.0", "h"), InstallStatus::UpgradeAvailable);
    CHECK_EQ(installStatusOf(true, "1.1.0", "h", "1.0.0", "h"), InstallStatus::DowngradeAvailable);
    CHECK_EQ(installStatusOf(true, "1.0.0", "a", "1.0.0", "b"), InstallStatus::DifferentHash);
    CHECK_EQ(installStatusOf(true, "1.0.0", "", "1.0.0", "b"), InstallStatus::Installed);
    CHECK_EQ(installStatusOf(true, "1.0.0", "h", "1.0.0", "h"), InstallStatus::Installed);
}

// Nothing to compare: installed is what it is.
TEST(an_installed_package_without_versions_is_installed) {
    CHECK_EQ(installStatusOf(true, {}, {}, "1.0.0", {}), InstallStatus::Installed);
    CHECK_EQ(installStatusOf(true, "1.0.0", {}, {}, {}), InstallStatus::Installed);
}

TEST(actions_for_a_package_not_installed) {
    CHECK_EQ(resolveRowAction(false, true, InstallStatus::NotInstalled, {}, {}, "1.0.0", "h"),
             RowAction::Install);
    CHECK_EQ(resolveRowAction(false, false, InstallStatus::NotInstalled, {}, {}, "1.0.0", "h"),
             RowAction::NotAvailable);
}

TEST(actions_for_an_installed_package) {
    const auto action = [](const char* installed, const char* ih, const char* sel, const char* sh) {
        return resolveRowAction(true, true, InstallStatus::Installed, installed, ih, sel, sh);
    };
    CHECK_EQ(action("1.0.0", "h", "1.1.0", "x"), RowAction::Upgrade);
    CHECK_EQ(action("1.1.0", "h", "1.0.0", "x"), RowAction::Downgrade);
    CHECK_EQ(action("1.0.0", "a", "1.0.0", "b"), RowAction::Reinstall);
    CHECK_EQ(action("1.0.0", "a", "1.0.0", "a"), RowAction::NoOp);
    CHECK_EQ(action("1.0.0", "", "1.0.0", "b"), RowAction::NoOp);
    CHECK_EQ(action("", "", "1.0.0", "b"), RowAction::NoOp);
}

// The catalog dropped this host's variant after the install: nothing to
// download, so no version change, and not "Not available" either.
TEST(an_installed_package_without_a_variant_offers_nothing) {
    CHECK_EQ(resolveRowAction(true, false, InstallStatus::Installed, "1.0.0", "h", "1.1.0", "x"),
             RowAction::NoOp);
}

TEST(transient_states_come_first) {
    CHECK_EQ(resolveRowAction(false, false, InstallStatus::Failed, {}, {}, {}, {}), RowAction::Retry);
    CHECK_EQ(resolveRowAction(false, true, InstallStatus::Installing, {}, {}, "1", {}), RowAction::NoOp);
}

TEST(an_update_is_a_strictly_newer_version) {
    CHECK(hasUpdateAvailable(true, "1.0.0", "1.0.1"));
    CHECK(!hasUpdateAvailable(true, "1.0.1", "1.0.1"));
    CHECK(!hasUpdateAvailable(false, "1.0.0", "1.0.1"));
    CHECK(!hasUpdateAvailable(true, {}, "1.0.1"));
}

TEST(the_worse_status_wins) {
    CHECK_EQ(worseOf(InstallStatus::Installed, InstallStatus::UpgradeAvailable),
             InstallStatus::UpgradeAvailable);
    CHECK_EQ(worseOf(InstallStatus::UpgradeAvailable, InstallStatus::DowngradeAvailable),
             InstallStatus::DowngradeAvailable);
    CHECK_EQ(worseOf(InstallStatus::DifferentHash, InstallStatus::DowngradeAvailable),
             InstallStatus::DifferentHash);
    CHECK_EQ(worseOf(InstallStatus::UpgradeAvailable, InstallStatus::Installed),
             InstallStatus::UpgradeAvailable);
}

TEST(only_user_installs_on_disk_can_be_uninstalled) {
    CHECK(canUninstall("user", InstallStatus::Installed));
    CHECK(canUninstall("user", InstallStatus::DifferentHash));
    CHECK(!canUninstall("embedded", InstallStatus::Installed));
    CHECK(!canUninstall("user", InstallStatus::NotInstalled));
    CHECK(!canUninstall("user", InstallStatus::Installing));
}

CHECK_MAIN()
