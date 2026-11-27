#include "logos/installer/PackageRows.h"

#include "logos/installer/Availability.h"
#include "logos/installer/RowState.h"

namespace logos::installer {

namespace {

// An `origin*` field, or the fallback when the producer left it empty: the
// downloader stamps these always, so empty means the source never resolved.
QString originOf(const QVariantMap& src, const char* key, const QString& fallback)
{
    const QString v = src.value(QLatin1String(key)).toString();
    return v.isEmpty() ? fallback : v;
}

QString orFallback(const QString& value, const QString& fallback)
{
    return value.isEmpty() ? fallback : value;
}

// The row's availability for its selected version, and its action.
void applySelection(QVariantMap& row, int index)
{
    const QVariantList versions = row.value(QStringLiteral("availableVersions")).toList();
    const QVariantMap pick = index >= 0 && index < versions.size()
        ? versions.at(index).toMap() : QVariantMap();
    if (!pick.isEmpty()) {
        row[QStringLiteral("dateUpdated")] = pick.value(QStringLiteral("releasedAt"));
        row[QStringLiteral("size")]        = pick.value(QStringLiteral("size"));
    }

    Availability availability;
    availability.variantAvailable = row.value(QStringLiteral("isVariantAvailable")).toBool();
    availability.variantReason = static_cast<NotAvailableReason>(
        row.value(QStringLiteral("variantNotAvailableReason")).toInt());
    row[QStringLiteral("isSourceAvailable")] = sourceAvailable(pick);
    row[QStringLiteral("notAvailableReason")] = toInt(notAvailableReason(availability, pick));

    const bool installable = availability.variantAvailable && sourceAvailable(pick);
    row[QStringLiteral("rowAction")] = toInt(resolveRowAction(
        isInstalledRow(row), installable,
        static_cast<InstallStatus>(row.value(QStringLiteral("installStatus")).toInt()),
        row.value(QStringLiteral("installedVersion")).toString(),
        row.value(QStringLiteral("installedHash")).toString(),
        row.value(QStringLiteral("version")).toString(),
        row.value(QStringLiteral("hash")).toString()));
}

}  // namespace

QVariantList dependencyEntries(const QVariantList& manifestDependencies)
{
    QVariantList out;
    for (const QVariant& dep : manifestDependencies) {
        const bool plain = dep.typeId() == QMetaType::QString;
        const QVariantMap m = plain ? QVariantMap() : dep.toMap();
        const QString name = plain ? dep.toString() : m.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        out.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("version"), m.value(QStringLiteral("version")).toString()},
            {QStringLiteral("signer"), m.value(QStringLiteral("signer")).toString()},
        });
    }
    return out;
}

QStringList renderDependencies(const QVariantList& manifestDependencies)
{
    QStringList out;
    for (const QVariant& v : dependencyEntries(manifestDependencies)) {
        const QVariantMap e = v.toMap();
        QString s = e.value(QStringLiteral("name")).toString();
        const QString version = e.value(QStringLiteral("version")).toString();
        const QString signer = e.value(QStringLiteral("signer")).toString();
        if (!version.isEmpty()) s += QLatin1Char(' ') + version;
        if (!signer.isEmpty()) s += QStringLiteral(" [signer=") + signer + QLatin1Char(']');
        out.append(s);
    }
    return out;
}

QStringList providedIntents(const QVariantMap& manifest)
{
    QStringList out;
    for (const QVariant& v : manifest.value(QStringLiteral("provides")).toList()) {
        const QString intent = v.typeId() == QMetaType::QString
            ? v.toString()
            : v.toMap().value(QStringLiteral("intent")).toString();
        if (!intent.isEmpty() && !out.contains(intent)) out.append(intent);
    }
    return out;
}

QStringList webProvidedIntents(const QVariantMap& manifest)
{
    QStringList out;
    for (const QVariant& v : manifest.value(QStringLiteral("provides")).toList()) {
        const QVariantMap entry = v.toMap();
        const QString intent = entry.value(QStringLiteral("intent")).toString();
        // "false" read as true would publish what its author withheld.
        const QVariant web = entry.value(QStringLiteral("web"));
        if (!intent.isEmpty() && web.typeId() == QMetaType::Bool && web.toBool()
            && !out.contains(intent))
            out.append(intent);
    }
    return out;
}

bool supportsFullBleedIcon(const QString& manifestVersion)
{
    const QStringList parts = manifestVersion.split(QLatin1Char('.'));
    if (parts.size() < 2) return false;
    bool okMajor = false, okMinor = false;
    const int major = parts.at(0).toInt(&okMajor);
    const int minor = parts.at(1).toInt(&okMinor);
    if (!okMajor || !okMinor) return false;
    return major > 0 || minor >= 4;
}

QString catalogIconUrl(const QVariantMap& catalogRow)
{
    const QString raw = catalogRow.value(QStringLiteral("icon")).toString();
    return raw.contains(QLatin1String("://")) ? raw : QString();
}

QString sourceKeyOf(const QVariantMap& row)
{
    const QString url = row.value(QStringLiteral("repositoryUrl")).toString();
    if (!url.isEmpty()) return url;
    const QString name = row.value(QStringLiteral("repositoryName")).toString();
    return name.isEmpty() ? QStringLiteral("local") : name;
}

DownloadSource downloadSourceOf(const QString& installType, const QString& source)
{
    if (installType.isEmpty()) return DownloadSource::NoSource;
    if (installType != QLatin1String("user")) return DownloadSource::Builtin;
    if (source.startsWith(QLatin1String("logos:"))) return DownloadSource::Storage;
    if (source.startsWith(QLatin1String("file:"))) return DownloadSource::LocalFile;
    return DownloadSource::GitHub;
}

QVariantMap buildPackageRow(const QVariantMap& obj,
                            const QHash<QString, QVariantMap>& installedByName,
                            const QStringList& validVariants)
{
    QVariantMap pkg;
    const QString name = obj.value(QStringLiteral("name")).toString();
    const QVariantList rawVersions = obj.value(QStringLiteral("versions")).toList();
    const QVariantMap newest = rawVersions.isEmpty() ? QVariantMap() : rawVersions.first().toMap();
    const QVariantMap manifest = newest.value(QStringLiteral("manifest")).toMap();

    // The catalog lifts header fields from the newest manifest; the manifest
    // itself is the fallback.
    const auto header = [&](const char* rowKey, const char* manifestKey) {
        return orFallback(obj.value(QLatin1String(rowKey)).toString(),
                          manifest.value(QLatin1String(manifestKey)).toString());
    };
    const QString moduleName = orFallback(header("moduleName", "name"), name);

    pkg[QStringLiteral("name")]        = name;
    pkg[QStringLiteral("moduleName")]  = moduleName;
    pkg[QStringLiteral("displayName")] = orFallback(header("displayName", "display_name"),
                                                    moduleName);
    pkg[QStringLiteral("description")] = header("description", "description");
    pkg[QStringLiteral("type")]        = header("type", "type");
    pkg[QStringLiteral("category")]    = header("category", "category");
    pkg[QStringLiteral("catalogIconUrl")] = catalogIconUrl(obj);
    // The index publishes the icon beside it, content-addressed.
    pkg[QStringLiteral("catalogIconSha256")] =
        newest.value(QStringLiteral("icon")).toMap().value(QStringLiteral("sha256")).toString();
    pkg[QStringLiteral("supportsFullBleedIcon")] =
        supportsFullBleedIcon(obj.value(QStringLiteral("manifestVersion")).toString());
    pkg[QStringLiteral("provides")]    = providedIntents(manifest);
    pkg[QStringLiteral("webProvides")] = webProvidedIntents(manifest);

    // `repository*` is the shelf the package sits on (the configured
    // repository, also for packages it draws in from other catalogs);
    // `origin*` is who published it, which is who the bytes are trusted from.
    const QString repositoryUrl  = obj.value(QStringLiteral("repositoryUrl")).toString();
    const QString repositoryName = obj.value(QStringLiteral("repositoryName")).toString();
    const QString repositoryDisplayName =
        obj.value(QStringLiteral("repositoryDisplayName")).toString();
    pkg[QStringLiteral("repositoryUrl")]         = repositoryUrl;
    pkg[QStringLiteral("repositoryName")]        = repositoryName;
    pkg[QStringLiteral("repositoryDisplayName")] = repositoryDisplayName;
    const QString originUrl  = originOf(obj, "originRepositoryUrl", repositoryUrl);
    const QString originName = originOf(obj, "originRepositoryName", repositoryName);
    pkg[QStringLiteral("originRepositoryUrl")]  = originUrl;
    pkg[QStringLiteral("originRepositoryName")] = originName;
    pkg[QStringLiteral("originRepositoryDisplayName")] =
        originOf(obj, "originRepositoryDisplayName", repositoryDisplayName);
    pkg[QStringLiteral("sourceKey")] = sourceKeyOf(pkg);

    QVariantList availableVersions;
    for (const QVariant& vv : rawVersions) {
        const QVariantMap vm = vv.toMap();
        const QVariantMap vManifest = vm.value(QStringLiteral("manifest")).toMap();
        QVariantMap entry;
        entry[QStringLiteral("version")]      = vManifest.value(QStringLiteral("version")).toString();
        entry[QStringLiteral("rootHash")]     = vm.value(QStringLiteral("rootHash")).toString();
        entry[QStringLiteral("releasedAt")]   = vm.value(QStringLiteral("releasedAt")).toString();
        entry[QStringLiteral("size")]         = vm.value(QStringLiteral("size"));
        entry[QStringLiteral("publisherRef")] = vm.value(QStringLiteral("publisherRef")).toString();
        entry[QStringLiteral("url")]          = vm.value(QStringLiteral("url")).toString();

        const QStringList usable = vm.value(QStringLiteral("usableSources")).toStringList();
        QVariantList sources;
        if (usable.contains(QStringLiteral("logos"))) sources.append(toInt(DownloadSource::Storage));
        if (usable.contains(QStringLiteral("http"))) sources.append(toInt(DownloadSource::GitHub));
        entry[QStringLiteral("sources")] = sources;
        entry[QStringLiteral("sourceAvailable")] = sourceAvailable(vm);
        entry[QStringLiteral("requiredSource")] = vm.value(QStringLiteral("requiredSource")).toString();
        entry[QStringLiteral("sourceUnavailableReason")] = toInt(sourceUnavailableReason(vm));
        entry[QStringLiteral("signed")]    = vm.contains(QStringLiteral("signature"));
        entry[QStringLiteral("signerDid")] =
            vm.value(QStringLiteral("signature")).toMap().value(QStringLiteral("did")).toString();
        entry[QStringLiteral("manifest")]  = vManifest;
        // Who signed it, and the file's digest: what the trust judge
        // (Trust.h) reads a release by.
        if (vm.contains(QStringLiteral("signature")))
            entry[QStringLiteral("signature")] = vm.value(QStringLiteral("signature"));
        entry[QStringLiteral("sha256")] = vm.value(QStringLiteral("sha256")).toString();
        // Per version: after a merge, one package's versions can come from
        // several catalogs.
        entry[QStringLiteral("originRepositoryUrl")]  = originOf(vm, "originRepositoryUrl", originUrl);
        entry[QStringLiteral("originRepositoryName")] = originOf(vm, "originRepositoryName", originName);
        availableVersions.append(entry);
    }
    pkg[QStringLiteral("availableVersions")]    = availableVersions;
    pkg[QStringLiteral("selectedVersionIndex")] = 0;

    // The version's hash is the catalog's `rootHash`, not the manifest's.
    const QString releaseVersion = manifest.value(QStringLiteral("version")).toString();
    const QString releaseHash = newest.value(QStringLiteral("rootHash")).toString();
    pkg[QStringLiteral("version")]       = releaseVersion;
    pkg[QStringLiteral("hash")]          = releaseHash;
    pkg[QStringLiteral("latestVersion")] = releaseVersion;

    const bool isInstalled = installedByName.contains(moduleName);
    const QVariantMap inst = installedByName.value(moduleName);
    const QString installedVersion = inst.value(QStringLiteral("version")).toString();
    const QString installedHash =
        inst.value(QStringLiteral("hashes")).toMap().value(QStringLiteral("root")).toString();
    const QString installType = inst.value(QStringLiteral("installType")).toString();
    pkg[QStringLiteral("installedVersion")] = installedVersion;
    pkg[QStringLiteral("installedHash")]    = installedHash;
    pkg[QStringLiteral("installType")]      = installType;
    pkg[QStringLiteral("downloadSource")] = toInt(downloadSourceOf(
        installType, inst.value(QStringLiteral("source")).toString()));

    pkg[QStringLiteral("installStatus")] = toInt(installStatusOf(
        isInstalled, installedVersion, installedHash, releaseVersion, releaseHash));
    pkg[QStringLiteral("errorMessage")] = QString();

    const Availability availability = availabilityOf(availableVersions, validVariants);
    pkg[QStringLiteral("isVariantAvailable")] = availability.variantAvailable;
    pkg[QStringLiteral("variantNotAvailableReason")] = toInt(availability.variantReason);
    pkg[QStringLiteral("hasInstallableVersion")] = availability.hasInstallableVersion;
    pkg[QStringLiteral("updateAvailable")] =
        hasUpdateAvailable(isInstalled, installedVersion, releaseVersion);

    QVariantList deps = obj.value(QStringLiteral("dependencies")).toList();
    if (deps.isEmpty()) deps = manifest.value(QStringLiteral("dependencies")).toList();
    pkg[QStringLiteral("dependencies")] = renderDependencies(deps);
    pkg[QStringLiteral("dependencyEntries")] = dependencyEntries(deps);

    applySelection(pkg, 0);
    return pkg;
}

QVariantMap buildLocalPackageRow(const QVariantMap& installed)
{
    QVariantMap pkg;
    const QString name = installed.value(QStringLiteral("name")).toString();
    const QString moduleName = orFallback(installed.value(QStringLiteral("moduleName")).toString(), name);

    pkg[QStringLiteral("name")]        = name;
    pkg[QStringLiteral("moduleName")]  = moduleName;
    pkg[QStringLiteral("displayName")] =
        orFallback(installed.value(QStringLiteral("displayName")).toString(), moduleName);
    pkg[QStringLiteral("description")] = installed.value(QStringLiteral("description")).toString();
    pkg[QStringLiteral("type")]        = installed.value(QStringLiteral("type")).toString();
    // The module's own category: "local" is a repository, not a category.
    pkg[QStringLiteral("category")]    = installed.value(QStringLiteral("category")).toString();
    pkg[QStringLiteral("catalogIconUrl")] = QString();
    pkg[QStringLiteral("catalogIconSha256")] = QString();
    pkg[QStringLiteral("supportsFullBleedIcon")] =
        supportsFullBleedIcon(installed.value(QStringLiteral("manifestVersion")).toString());
    pkg[QStringLiteral("provides")]    = QStringList();
    pkg[QStringLiteral("webProvides")] = QStringList();
    pkg[QStringLiteral("size")]        = 0;
    pkg[QStringLiteral("dateUpdated")] = QString();

    pkg[QStringLiteral("repositoryUrl")]               = QString();
    pkg[QStringLiteral("repositoryName")]              = QStringLiteral("local");
    pkg[QStringLiteral("repositoryDisplayName")]       = QStringLiteral("local");
    pkg[QStringLiteral("originRepositoryUrl")]         = QString();
    pkg[QStringLiteral("originRepositoryName")]        = QStringLiteral("local");
    pkg[QStringLiteral("originRepositoryDisplayName")] = QStringLiteral("local");
    pkg[QStringLiteral("sourceKey")]                   = QStringLiteral("local");

    pkg[QStringLiteral("availableVersions")]    = QVariantList{};
    pkg[QStringLiteral("selectedVersionIndex")] = 0;

    const QString installedVersion = installed.value(QStringLiteral("version")).toString();
    const QString installedHash =
        installed.value(QStringLiteral("hashes")).toMap().value(QStringLiteral("root")).toString();
    const QString installType = installed.value(QStringLiteral("installType")).toString();
    pkg[QStringLiteral("version")]          = installedVersion;
    pkg[QStringLiteral("hash")]             = installedHash;
    pkg[QStringLiteral("latestVersion")]    = installedVersion;
    pkg[QStringLiteral("installedVersion")] = installedVersion;
    pkg[QStringLiteral("installedHash")]    = installedHash;
    pkg[QStringLiteral("installType")]      = installType;
    pkg[QStringLiteral("downloadSource")]   = toInt(downloadSourceOf(
        installType, installed.value(QStringLiteral("source")).toString()));
    pkg[QStringLiteral("installStatus")]    = toInt(InstallStatus::Installed);
    pkg[QStringLiteral("errorMessage")]     = QString();
    pkg[QStringLiteral("isVariantAvailable")]        = true;
    pkg[QStringLiteral("variantNotAvailableReason")] = toInt(NotAvailableReason::Available);
    pkg[QStringLiteral("isSourceAvailable")]         = true;
    pkg[QStringLiteral("notAvailableReason")]        = toInt(NotAvailableReason::Available);
    pkg[QStringLiteral("hasInstallableVersion")]     = true;
    pkg[QStringLiteral("rowAction")]        = toInt(RowAction::NoOp);
    pkg[QStringLiteral("updateAvailable")]  = false;

    // `dependencies` is the edge list; `dependencyConstraints` repeats the
    // entries that declared a range or a signer.
    QVariantMap constraintByName;
    for (const QVariant& c : installed.value(QStringLiteral("dependencyConstraints")).toList()) {
        const QVariantMap cm = c.toMap();
        constraintByName.insert(cm.value(QStringLiteral("name")).toString(), cm);
    }
    QVariantList deps;
    for (const QVariant& dep : installed.value(QStringLiteral("dependencies")).toList()) {
        const QString depName = dep.toString();
        deps.append(constraintByName.contains(depName) ? constraintByName.value(depName)
                                                       : QVariant(depName));
    }
    pkg[QStringLiteral("dependencies")] = renderDependencies(deps);
    pkg[QStringLiteral("dependencyEntries")] = dependencyEntries(deps);
    return pkg;
}

bool isInstalledRow(const QVariantMap& row)
{
    return !row.value(QStringLiteral("installedVersion")).toString().isEmpty()
        || !row.value(QStringLiteral("installedHash")).toString().isEmpty()
        || !row.value(QStringLiteral("installType")).toString().isEmpty();
}

bool isUnavailableOnThisPlatform(const QVariantMap& row)
{
    return isUnavailableOnThisPlatform(
        row.value(QStringLiteral("hasInstallableVersion"), true).toBool(), isInstalledRow(row));
}

void selectVersion(QVariantMap& row, int versionIndex)
{
    const QVariantList versions = row.value(QStringLiteral("availableVersions")).toList();
    if (versionIndex < 0 || versionIndex >= versions.size()) versionIndex = 0;
    row[QStringLiteral("selectedVersionIndex")] = versionIndex;
    if (versionIndex < versions.size()) {
        const QVariantMap pick = versions.at(versionIndex).toMap();
        row[QStringLiteral("version")] = pick.value(QStringLiteral("version"));
        row[QStringLiteral("hash")]    = pick.value(QStringLiteral("rootHash"));
        if (!pick.value(QStringLiteral("originRepositoryName")).toString().isEmpty()) {
            row[QStringLiteral("originRepositoryUrl")]  = pick.value(QStringLiteral("originRepositoryUrl"));
            row[QStringLiteral("originRepositoryName")] = pick.value(QStringLiteral("originRepositoryName"));
        }
    }
    applySelection(row, versionIndex);
}

}  // namespace logos::installer
