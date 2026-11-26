#include "logos/installer/Availability.h"

#include <QSet>

namespace logos::installer {

namespace {

NotAvailableReason classifyUnavailable(const QStringList& offered, const QStringList& valid)
{
    if (offered.isEmpty()) return NotAvailableReason::NoVariantsPublished;
    QSet<QString> hostBases;
    for (const QString& v : valid) hostBases.insert(splitVariant(v).first);
    for (const QString& v : offered) {
        if (hostBases.contains(splitVariant(v).first))
            return NotAvailableReason::BuildFlavorMismatch;
    }
    return NotAvailableReason::PlatformMismatch;
}

}  // namespace

QPair<QString, QString> splitVariant(const QString& variant)
{
    static const QSet<QString> kKnownFlavors = {
        QStringLiteral("dev"), QStringLiteral("portable")
    };
    const int lastDash = variant.lastIndexOf(QLatin1Char('-'));
    if (lastDash <= 0) return {variant, QString()};
    const QString trailing = variant.mid(lastDash + 1);
    if (kKnownFlavors.contains(trailing)) return {variant.left(lastDash), trailing};
    return {variant, QString()};
}

QStringList offeredVariants(const QVariantMap& manifest)
{
    QStringList offered;
    const QVariantMap mainMap = manifest.value(QStringLiteral("main")).toMap();
    for (auto it = mainMap.constBegin(); it != mainMap.constEnd(); ++it) {
        if (!it.key().isEmpty()) offered.append(it.key());
    }
    return offered;
}

Availability availabilityOf(const QVariantList& versions, const QStringList& validVariants)
{
    Availability out;
    if (versions.isEmpty()) return out;
    if (validVariants.isEmpty()) {
        out.variantAvailable = true;
        out.variantReason = NotAvailableReason::Available;
        for (const QVariant& v : versions) {
            if (sourceAvailable(v.toMap())) { out.hasInstallableVersion = true; break; }
        }
        return out;
    }

    const QVariantMap manifest =
        versions.first().toMap().value(QStringLiteral("manifest")).toMap();
    const QStringList offered = offeredVariants(manifest);
    for (const QString& v : offered) {
        if (validVariants.contains(v)) { out.variantAvailable = true; break; }
    }
    // A QML-only package can carry an empty `main`: it installs everywhere.
    if (!out.variantAvailable && offered.isEmpty()
        && manifest.value(QStringLiteral("type")).toString() == QLatin1String("ui_qml")) {
        out.variantAvailable = true;
    }
    if (!out.variantAvailable) {
        out.variantReason = classifyUnavailable(offered, validVariants);
        return out;
    }
    out.variantReason = NotAvailableReason::Available;
    for (const QVariant& v : versions) {
        if (sourceAvailable(v.toMap())) { out.hasInstallableVersion = true; break; }
    }
    return out;
}

bool sourceAvailable(const QVariantMap& version)
{
    return version.value(QStringLiteral("sourceAvailable"), true).toBool();
}

NotAvailableReason sourceUnavailableReason(const QVariantMap& version)
{
    return version.value(QStringLiteral("requiredSource")).toString() == QLatin1String("http")
        ? NotAvailableReason::NotOverHttp
        : NotAvailableReason::NotOnLogosStorage;
}

NotAvailableReason notAvailableReason(const Availability& availability,
                                      const QVariantMap& version)
{
    if (!availability.variantAvailable) return availability.variantReason;
    if (!sourceAvailable(version)) return sourceUnavailableReason(version);
    return NotAvailableReason::Available;
}

bool isUnavailableOnThisPlatform(bool hasInstallableVersion, bool isInstalled)
{
    return !isInstalled && !hasInstallableVersion;
}

}  // namespace logos::installer
