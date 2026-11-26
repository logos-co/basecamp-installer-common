#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace logos::installer {

// The label each repository is shown under, keyed by its URL, from
// package_downloader's repository list. Its own name, qualified by its owner
// (or host) when another repository has the same name, and by owner/repo when
// that owner has two. Names compare trimmed and case-folded; the label keeps
// the repository's own spelling.
QVariantMap repositoryLabels(const QVariantList& repositories);

// The label of the repository for installed packages no catalog publishes.
QString localRepositoryLabel();

// The order sections are listed in, as section keys (see sourceKeyOf): the
// default repository (`isDefault`, from the registry, never matched by name),
// then the others by label, then "local".
QStringList sourceOrder(const QVariantList& repositories);

}  // namespace logos::installer
