#include "logos/installer/RowState.h"

#include <logos/semver.hpp>

namespace logos::installer {

int versionCompare(const QString& a, const QString& b)
{
    return logos::semver::compare(a.toStdString(), b.toStdString());
}

InstallStatus installStatusOf(bool isInstalled,
                              const QString& installedVersion,
                              const QString& installedHash,
                              const QString& releaseVersion,
                              const QString& releaseHash)
{
    if (!isInstalled) return InstallStatus::NotInstalled;
    if (installedVersion.isEmpty() || releaseVersion.isEmpty())
        return InstallStatus::Installed;
    const int cmp = versionCompare(installedVersion, releaseVersion);
    if (cmp < 0) return InstallStatus::UpgradeAvailable;
    if (cmp > 0) return InstallStatus::DowngradeAvailable;
    if (!releaseHash.isEmpty() && !installedHash.isEmpty() && releaseHash != installedHash)
        return InstallStatus::DifferentHash;
    return InstallStatus::Installed;
}

RowAction resolveRowAction(bool isInstalled,
                           bool variantAvailable,
                           InstallStatus currentStatus,
                           const QString& installedVersion,
                           const QString& installedHash,
                           const QString& selectedVersion,
                           const QString& selectedHash)
{
    if (currentStatus == InstallStatus::Failed) return RowAction::Retry;
    if (currentStatus == InstallStatus::Installing) return RowAction::NoOp;

    // Installed first: a package on disk is shown as what it is even when the
    // catalog dropped this host's variant since.
    if (isInstalled) {
        if (!variantAvailable) return RowAction::NoOp;
        if (installedVersion.isEmpty() || selectedVersion.isEmpty()) return RowAction::NoOp;
        const int cmp = versionCompare(installedVersion, selectedVersion);
        if (cmp < 0) return RowAction::Upgrade;
        if (cmp > 0) return RowAction::Downgrade;
        if (!selectedHash.isEmpty() && !installedHash.isEmpty() && selectedHash != installedHash)
            return RowAction::Reinstall;
        return RowAction::NoOp;
    }
    return variantAvailable ? RowAction::Install : RowAction::NotAvailable;
}

bool hasUpdateAvailable(bool isInstalled,
                        const QString& installedVersion,
                        const QString& newestVersion)
{
    if (!isInstalled || installedVersion.isEmpty() || newestVersion.isEmpty()) return false;
    return versionCompare(installedVersion, newestVersion) < 0;
}

InstallStatus worseOf(InstallStatus a, InstallStatus b)
{
    const auto rank = [](InstallStatus v) {
        switch (v) {
        case InstallStatus::DifferentHash:      return 3;
        case InstallStatus::DowngradeAvailable: return 2;
        case InstallStatus::UpgradeAvailable:   return 1;
        default:                                return 0;
        }
    };
    return rank(b) > rank(a) ? b : a;
}

bool canUninstall(const QString& installType, InstallStatus status)
{
    if (installType != QLatin1String("user")) return false;
    switch (status) {
    case InstallStatus::Installed:
    case InstallStatus::UpgradeAvailable:
    case InstallStatus::DowngradeAvailable:
    case InstallStatus::DifferentHash:
        return true;
    default:
        return false;
    }
}

}  // namespace logos::installer
