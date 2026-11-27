#include <logos/installer/PackageTrust.h>

#include <logos/installer/PackageModel.h>
#include <logos/installer/Trust.h>
#include <logos/installer/Types.h>

namespace logos::installer {

QVariantMap trustOfBrought(const PackageModel& model, const QVariantList& brought,
                           const QVariantList& keys, bool keysRead, const QString& about)
{
    const QStringList trusted = keysRead ? trust::trustedDids(keys) : QStringList{};
    QList<trust::Package> packages;
    for (const QVariant& v : brought) {
        const QVariantMap b = v.toMap();
        const QString name = b.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        const auto action = static_cast<RowAction>(b.value(QStringLiteral("rowAction")).toInt());
        const QVariantMap record = model.installedRecord(name);
        if (action == RowAction::NoOp || (action == RowAction::NotAvailable && !record.isEmpty())) {
            packages << trust::fromInstalled(name, b.value(QStringLiteral("installedVersion")).toString(),
                                             record, trusted);
            continue;
        }
        const QVariantMap row = model.rowAt(model.rowOf(name, b.value(QStringLiteral("repositoryUrl")).toString()));
        const QVariantList versions = row.value(QStringLiteral("availableVersions")).toList();
        const QVariantMap picked = versions.value(b.value(QStringLiteral("selectedVersionIndex")).toInt()).toMap();
        const QString rootHash = picked.isEmpty() ? row.value(QStringLiteral("hash")).toString()
                                                  : picked.value(QStringLiteral("rootHash")).toString();
        packages << trust::fromCatalog(name, b.value(QStringLiteral("version")).toString(), rootHash,
                                       versions, trusted);
    }
    const QVariantMap verdicts = keysRead ? trust::summary(packages, keys, about)
                                          : trust::summary(trust::unchecked(packages), {}, about);
    return verdicts.value(QStringLiteral("byName")).toMap();
}

}  // namespace logos::installer
