#pragma once

#include "Types.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Whether a catalog package can be installed on this host. One rule for both
// installer apps and Basecamp: when two copies drift, the apps list different
// packages for one catalog.
namespace logos::installer {

struct Availability {
    // The newest version's manifest offers one of this host's variants.
    bool variantAvailable = false;
    // Why not, when it doesn't; Available when it does.
    NotAvailableReason variantReason = NotAvailableReason::NoVariantsPublished;
    // variantAvailable, and the download source serves at least one version.
    bool hasInstallableVersion = false;
};

// "<os>-<arch>[-<flavor>]" into (base, flavor), flavor being "dev" or
// "portable". A dev host accepts only "-dev" variants, so the flavor must be
// separable to tell a wrong flavor from a wrong machine.
QPair<QString, QString> splitVariant(const QString& variant);

// The variants a manifest offers: the keys of its `main` map.
QStringList offeredVariants(const QVariantMap& manifest);

// `versions` newest first, each carrying its `manifest` and the downloader's
// per-version `sourceAvailable` (absent counts as servable). The variant is
// decided by the newest version; the source per version, so one servable
// version keeps the package. Empty `validVariants` means this host's aren't
// known yet: every variant passes, rather than nothing.
Availability availabilityOf(const QVariantList& versions, const QStringList& validVariants);

// Whether the download source setting serves this version.
bool sourceAvailable(const QVariantMap& version);

// Why the download source refuses this version: its `requiredSource`.
NotAvailableReason sourceUnavailableReason(const QVariantMap& version);

// Why `version` can't be installed here, or Available. The variant first:
// no source setting fixes a platform mismatch.
NotAvailableReason notAvailableReason(const Availability& availability,
                                      const QVariantMap& version);

// Hidden by "show unavailable" off. An installed package never is: it is on
// disk, so it was installable once.
bool isUnavailableOnThisPlatform(bool hasInstallableVersion, bool isInstalled);

}  // namespace logos::installer
