#include "logos/installer/Approval.h"

#include "logos/installer/RowState.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

namespace logos::installer {

namespace {

QString str(const QVariantMap& m, const char* key)
{
    return m.value(QLatin1String(key)).toString();
}

// A release as approved: which package, which build, from which repository.
QString releaseOf(const QVariantMap& m)
{
    return QStringList{str(m, "name"), str(m, "version"), str(m, "rootHash"), str(m, "repositoryUrl")}
        .join(QChar(0x1f));
}

// An installed copy carries no repository.
QString buildOf(const QString& name, const QString& version, const QString& rootHash)
{
    return QStringList{name, version, rootHash}.join(QChar(0x1f));
}

QString installedName(const QVariantMap& record)
{
    const QString module = str(record, "moduleName");
    return module.isEmpty() ? str(record, "name") : module;
}

// A check that didn't run refuses as well, but says that is why.
QString checkFailed(const QVariantMap& reply, const QVariantMap& row)
{
    const QString error = str(reply, "error");
    if (error.isEmpty()) return {};
    return QStringLiteral("%1 v%2 could not be checked, so nothing was installed: %3 Try again.")
        .arg(str(row, "name"), str(row, "version"), error);
}

}  // namespace

ApprovalSource sourceOf(const QVariantMap& approval, bool fileWaiting)
{
    if (approval.contains(QStringLiteral("file")))
        return fileWaiting ? ApprovalSource::File : ApprovalSource::Refused;
    if (approval.contains(QStringLiteral("plan"))) return ApprovalSource::Catalog;
    return ApprovalSource::Unapproved;
}

QString refusal(const QVariantMap& approval, bool fileWaiting, const QString& name)
{
    switch (sourceOf(approval, fileWaiting)) {
    case ApprovalSource::Unapproved:
        return QStringLiteral("Basecamp did not approve this, so nothing was installed.");
    case ApprovalSource::Refused:
        return QStringLiteral("The file you approved is no longer the one waiting to install, "
                              "so nothing was installed. Open it again to review it.");
    case ApprovalSource::Catalog:
        return malformedPlan(approval.value(QStringLiteral("plan")).toList(), name);
    case ApprovalSource::File:
        break;
    }
    return {};
}

QString malformedPlan(const QVariantList& plan, const QString& name)
{
    bool holdsName = false;
    for (const QVariant& v : plan) {
        const QVariantMap row = v.toMap();
        for (const char* key : {"name", "version", "rootHash", "repositoryUrl"}) {
            if (str(row, key).isEmpty())
                return QStringLiteral("The approval does not say exactly which release of %1 to "
                                      "install, so nothing was installed.")
                    .arg(str(row, "name").isEmpty() ? name : str(row, "name"));
        }
        holdsName = holdsName || str(row, "name") == name;
    }
    if (holdsName) return {};
    return QStringLiteral("The approval does not include %1, so nothing was installed.").arg(name);
}

QVariantList stillToInstall(const QVariantList& plan, const QVariantList& installed,
                            const QString& keep)
{
    QSet<QString> onDisk;
    for (const QVariant& v : installed) {
        const QVariantMap m = v.toMap();
        onDisk.insert(buildOf(installedName(m), str(m, "version"),
                              m.value(QStringLiteral("hashes")).toMap().value(QStringLiteral("root")).toString()));
    }
    QVariantList rows;
    for (const QVariant& v : plan) {
        const QVariantMap row = v.toMap();
        if ((!keep.isEmpty() && str(row, "name") == keep)
            || !onDisk.contains(buildOf(str(row, "name"), str(row, "version"), str(row, "rootHash"))))
            rows.append(row);
    }
    return rows;
}

QString notAsApproved(const QVariantList& fetched, const QVariantList& requested,
                      const QVariantList& plan)
{
    QSet<QString> releases;
    for (const QVariant& v : plan) releases.insert(releaseOf(v.toMap()));
    QSet<QString> arrived;
    QHash<QString, QString> failed;
    for (const QVariant& v : fetched) {
        const QVariantMap row = v.toMap();
        const QString error = str(row, "error");
        if (!error.isEmpty()) {
            failed.insert(str(row, "name"), error);
            continue;
        }
        if (!releases.contains(releaseOf(row)))
            return QStringLiteral("What this installs changed after you approved it (%1 v%2), so "
                                  "nothing was installed. Try again to review it.")
                .arg(str(row, "name"), str(row, "version"));
        arrived.insert(releaseOf(row));
    }
    // All of it, or none: only an optional package may be missing.
    for (const QVariant& v : requested) {
        const QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("optional")).toBool() || arrived.contains(releaseOf(row))) continue;
        const QString name = str(row, "name");
        if (failed.contains(name)) return QStringLiteral("%1: %2").arg(name, failed.value(name));
        return QStringLiteral("%1 v%2 could not be fetched as approved, so nothing was installed. "
                              "Try again to review it.")
            .arg(name, str(row, "version"));
    }
    return {};
}

QString signerChanged(const QVariantMap& verified, const QVariantList& plan, const QVariantMap& row)
{
    if (const QString failed = checkFailed(verified, row); !failed.isEmpty()) return failed;
    QString signer;
    for (const QVariant& v : plan) {
        if (releaseOf(v.toMap()) == releaseOf(row)) signer = str(v.toMap(), "signer");
    }
    const bool held = verified.value(QStringLiteral("packageValid")).toBool()
        && (signer.isEmpty()
                ? !verified.value(QStringLiteral("isSigned")).toBool()
                : verified.value(QStringLiteral("isSigned")).toBool()
                      && verified.value(QStringLiteral("signatureValid")).toBool()
                      && str(verified, "signerDid") == signer);
    if (held) return {};
    return QStringLiteral("%1 v%2 is not signed as you approved it, so nothing was installed. "
                          "Try again to review it.")
        .arg(str(row, "name"), str(row, "version"));
}

QString releaseChanged(const QVariantMap& inspected, const QVariantMap& row)
{
    if (const QString failed = checkFailed(inspected, row); !failed.isEmpty()) return failed;
    const bool same = !inspected.contains(QStringLiteral("error"))
        && !str(row, "rootHash").isEmpty()
        && str(inspected, "name") == str(row, "name")
        && str(inspected, "version") == str(row, "version")
        && str(inspected, "rootHash") == str(row, "rootHash");
    if (same) return {};
    return QStringLiteral("%1 v%2 is not the release you approved, so nothing was installed. "
                          "Try again to review it.")
        .arg(str(row, "name"), str(row, "version"));
}

QString digestChanged(const QString& sha256, const QVariantList& plan, const QVariantMap& row)
{
    QString approved;
    for (const QVariant& v : plan) {
        if (releaseOf(v.toMap()) == releaseOf(row)) approved = str(v.toMap(), "sha256");
    }
    if (approved.isEmpty() || approved.compare(sha256, Qt::CaseInsensitive) == 0) return {};
    return QStringLiteral("%1 v%2 is not the file you approved, so nothing was installed. "
                          "Try again to review it.")
        .arg(str(row, "name"), str(row, "version"));
}

QString fileChanged(const QVariantMap& inspected, const QVariantMap& approvedFile,
                    const QString& name, const QString& version, const QString& sha256)
{
    const QString signer = str(approvedFile, "signer");
    const QString status = str(inspected, "signatureStatus");
    const QString digest = str(approvedFile, "sha256");
    const bool same = !inspected.contains(QStringLiteral("error"))
        && !str(approvedFile, "rootHash").isEmpty()
        && !digest.isEmpty() && digest == sha256
        && str(inspected, "name") == name
        && str(inspected, "version") == version
        && str(inspected, "rootHash") == str(approvedFile, "rootHash")
        && (signer.isEmpty() ? status == QLatin1String("unsigned")
                             : status == QLatin1String("signed") && str(inspected, "signerDid") == signer);
    if (same) return {};
    return QStringLiteral("The file changed after you approved it, so it was not installed. "
                          "Open it again to review it.");
}

QString sha256Of(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

QVariantList intoPrivateCopies(const QVariantList& fetched, const QString& dir, QString* error)
{
    QVariantList out;
    int n = 0;
    for (const QVariant& v : fetched) {
        QVariantMap row = v.toMap();
        const QString path = str(row, "path");
        if (!path.isEmpty()) {
            const QString copy =
                QDir(dir).filePath(QStringLiteral("%1-%2").arg(++n).arg(QFileInfo(path).fileName()));
            if (!QFile::copy(path, copy)) {
                if (error)
                    *error = QStringLiteral("Couldn't make a private copy of %1 to install.").arg(str(row, "name"));
                return {};
            }
            row.insert(QStringLiteral("path"), copy);
        }
        out.append(row);
    }
    return out;
}

bool removeBeforeInstall(const QVariantList& installed, const QString& name)
{
    for (const QVariant& v : installed) {
        const QVariantMap record = v.toMap();
        if (str(record, "name") == name || str(record, "moduleName") == name)
            return str(record, "installType") == QLatin1String("user");
    }
    return false;
}

bool pluginInstalled(const QVariantMap& reply)
{
    return reply.contains(QStringLiteral("path")) && str(reply, "error").isEmpty();
}

QString notOnDisk(const QVariantList& installed, const QString& name, const QString& version,
                  const QString& rootHash)
{
    QString onDisk;
    bool found = false;
    for (const QVariant& v : installed) {
        const QVariantMap record = v.toMap();
        if (str(record, "name") != name && str(record, "moduleName") != name) continue;
        // Basecamp's own copy is not one an install wrote.
        if (str(record, "installType") == QLatin1String("embedded")) continue;
        found = true;
        const QString recordVersion = str(record, "version");
        const QString recordHash =
            record.value(QStringLiteral("hashes")).toMap().value(QStringLiteral("root")).toString();
        if (!recordVersion.isEmpty() && versionCompare(recordVersion, version) == 0
            && !rootHash.isEmpty() && recordHash == rootHash)
            return {};
        if (onDisk.isEmpty()) onDisk = recordVersion;
    }
    if (!found)
        return QStringLiteral("The installer reported success, but %1 is not installed on disk.").arg(name);
    if (!onDisk.isEmpty() && versionCompare(onDisk, version) != 0)
        return QStringLiteral("The installer reported success, but v%1 is still on disk; v%2 was "
                              "not written. Uninstall %3 and install it again.")
            .arg(onDisk, version, name);
    return QStringLiteral("The installer reported success, but the %1 on disk is not the release "
                          "you approved. Uninstall it and install it again.")
        .arg(name);
}

int upgradeMode(const QString& version, const QString& installedVersion)
{
    const int cmp = versionCompare(version, installedVersion);
    return cmp > 0 ? 0 : cmp < 0 ? 1 : 2;
}

}  // namespace logos::installer
