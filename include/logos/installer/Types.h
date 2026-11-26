#pragma once

#include <QString>

// The vocabularies both installer apps and Basecamp share. Each app's `.rep`
// mirrors these values for its view, so they are fixed: append, never
// renumber.
namespace logos::installer {

// A catalog row against what is on disk. Installing and Failed are the
// transient states a row's own install puts it in.
enum class InstallStatus : int {
    NotInstalled       = 0,
    Installing         = 1,
    Installed          = 2,
    Failed             = 3,
    UpgradeAvailable   = 4,
    DowngradeAvailable = 5,
    DifferentHash      = 6,
};

// Where an install of one package is.
enum class InstallStage : int {
    None        = 0,
    Downloading = 1,
    Queued      = 2,
    Installing  = 3,
    Installed   = 4,
    Failed      = 5,
    Downloaded  = 6,
};

// What a row's primary action does for the version it has selected.
// Uninstall is never the primary action.
enum class RowAction : int {
    Install      = 0,
    Upgrade      = 1,
    Downgrade    = 2,
    Reinstall    = 3,
    Retry        = 4,
    NotAvailable = 5,
    NoOp         = 6,
};

enum class NotAvailableReason : int {
    Available           = 0,
    NoVariantsPublished = 1,
    BuildFlavorMismatch = 2,
    PlatformMismatch    = 3,
    // The download source setting excludes every transport the version is
    // published on.
    NotOnLogosStorage   = 4,
    NotOverHttp         = 5,
};

// Where an installed package's bytes came from.
enum class DownloadSource : int {
    NoSource  = 0,
    Builtin   = 1,
    GitHub    = 2,
    Storage   = 3,
    LocalFile = 4,
};

constexpr int toInt(InstallStatus v) { return static_cast<int>(v); }
constexpr int toInt(InstallStage v) { return static_cast<int>(v); }
constexpr int toInt(RowAction v) { return static_cast<int>(v); }
constexpr int toInt(NotAvailableReason v) { return static_cast<int>(v); }
constexpr int toInt(DownloadSource v) { return static_cast<int>(v); }

// The stage's name on the wire ("downloading", …); empty for None.
QString stageName(InstallStage stage);
InstallStage stageFromName(const QString& name);

}  // namespace logos::installer
