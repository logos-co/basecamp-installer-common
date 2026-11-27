#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace logos::installer {

class PackageModel;

// Who signed each package an app brings (PackageModel::packagesBroughtBy()),
// and whether the person trusts them: Trust.h's summary() verdict of each, by
// name, `about` being the app. What stays on disk is judged by the copy there;
// what installs, by the release picked for it, as Basecamp's install dialog
// judges it before Basecamp's own rules. `keys` is the keyring
// (PackageModules::listTrustedKeys()); unread (`keysRead` false), no signer
// is trusted and a signed package is Unknown.
QVariantMap trustOfBrought(const PackageModel& model, const QVariantList& brought,
                           const QVariantList& keys, bool keysRead, const QString& about);

}  // namespace logos::installer
