#pragma once

#include "Types.h"

#include <QString>

// What a package row is against what is on disk, and what its primary action
// does. Both installer apps decide these the same way.
namespace logos::installer {

// -1 / 0 / +1, with logos-package's semver (what lgx, lgpm and lgpd use), so
// a pre-release orders below its release.
int versionCompare(const QString& a, const QString& b);

// Installed against the catalog's release. An installed package with no
// version on either side to compare counts as Installed.
InstallStatus installStatusOf(bool isInstalled,
                              const QString& installedVersion,
                              const QString& installedHash,
                              const QString& releaseVersion,
                              const QString& releaseHash);

// The primary action for the version a row has selected. A failed row offers
// Retry; a row mid-install offers nothing (NoOp). An installed package with no
// variant for this host can't change version, so NoOp; Uninstall stays in the
// row's menu.
RowAction resolveRowAction(bool isInstalled,
                           bool variantAvailable,
                           InstallStatus currentStatus,
                           const QString& installedVersion,
                           const QString& installedHash,
                           const QString& selectedVersion,
                           const QString& selectedHash);

// A version strictly newer than the installed one exists, whatever the row
// has selected.
bool hasUpdateAvailable(bool isInstalled,
                        const QString& installedVersion,
                        const QString& newestVersion);

// The status that needs more attention: DifferentHash, then
// DowngradeAvailable, then UpgradeAvailable, then the rest. For an app whose
// dependencies need an update or a repair.
InstallStatus worseOf(InstallStatus a, InstallStatus b);

// The user installed it and it is on disk; built-in packages can't be
// uninstalled.
bool canUninstall(const QString& installType, InstallStatus status);

}  // namespace logos::installer
