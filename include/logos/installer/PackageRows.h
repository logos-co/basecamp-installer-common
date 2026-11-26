#pragma once

#include "Types.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Catalog rows (package_downloader's getCatalog) and installed packages
// (package_manager's getInstalledPackages) into the rows both installer apps
// show. Pure: QVariant in, QVariant out.
namespace logos::installer {

// One dependency as {name, version, signer}; version and signer empty when
// not declared. A manifest entry is a plain name or such an object.
QVariantList dependencyEntries(const QVariantList& manifestDependencies);

// The same entries as text: "name", "name ~0.1.0", "name ~0.1.0 [signer=did]".
QStringList renderDependencies(const QVariantList& manifestDependencies);

// The intents a manifest provides, and which of those it publishes to links
// (`"web": true`, a real boolean).
QStringList providedIntents(const QVariantMap& manifest);
QStringList webProvidedIntents(const QVariantMap& manifest);

// A manifest at 0.4.0 or later guarantees a 256x256 icon, safe to show
// edge to edge. Mirrors logos-package's Manifest::requiresIconContract().
bool supportsFullBleedIcon(const QString& manifestVersion);

// The catalog row's icon, when it is a URL. A sandboxed view can't load it;
// the backend fetches it and hands the view its own copy.
QString catalogIconUrl(const QVariantMap& catalogRow);

// The section a row is listed under: its repository's URL, else its
// repository's name, else "local".
QString sourceKeyOf(const QVariantMap& row);

// Where an installed package's bytes came from, from its `installType` and
// `source`. A downloaded package from before sources were recorded counts as
// GitHub.
DownloadSource downloadSourceOf(const QString& installType, const QString& source);

// One row from one catalog row. `installedByName` is keyed by module name;
// `validVariants` are this host's. The newest version is selected.
QVariantMap buildPackageRow(const QVariantMap& catalogRow,
                            const QHash<QString, QVariantMap>& installedByName,
                            const QStringList& validVariants);

// One row for an installed package no catalog publishes. Its repository is
// "local".
QVariantMap buildLocalPackageRow(const QVariantMap& installed);

// The row's package is on disk.
bool isInstalledRow(const QVariantMap& row);

// The row is hidden by "show unavailable" off (see Availability.h).
bool isUnavailableOnThisPlatform(const QVariantMap& row);

// Select another of the row's versions: the version, hash, size, date,
// origin, availability and action follow it.
void selectVersion(QVariantMap& row, int versionIndex);

}  // namespace logos::installer
