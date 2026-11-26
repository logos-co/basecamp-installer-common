#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace logos::installer {

// What installing calls on package_downloader and package_manager, as a
// backend reaches them through its typed wrappers. Every call answers once,
// on the backend's thread, within the backend's own deadline.
class InstallModules {
public:
    virtual ~InstallModules() = default;

    // package_downloader.downloadPinned(): exactly that release. {name, path,
    // version, rootHash, repositoryUrl, source}, or {name, error}.
    virtual void downloadPinned(const QString& repositoryUrl, const QString& name,
                                const QString& version, const QString& rootHash,
                                std::function<void(const QVariantMap&)> done) = 0;
    // package_manager.verifyPackage(): {packageValid, isSigned, signatureValid, signerDid}.
    virtual void verifyPackage(const QString& path, std::function<void(const QVariantMap&)> done) = 0;
    // package_manager.inspectPackage(): {name, version, rootHash, signatureStatus,
    // signerDid, isAlreadyInstalled, installedVersion}, or {error}.
    virtual void inspectPackage(const QString& path, std::function<void(const QVariantMap&)> done) = 0;
    // package_manager.installPlugin(path, false, source): its reply, or
    // `callError` when the call itself failed (the package may be installed
    // all the same).
    virtual void installPlugin(const QString& path, const QString& source,
                               std::function<void(const QVariantMap& reply, const QString& callError)> done) = 0;
    // package_manager.uninstallPackage(): {success, error}.
    virtual void uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done) = 0;
    // package_manager.getInstalledPackages(); `read` false when the call failed.
    virtual void readInstalledPackages(std::function<void(const QVariantList& installed, bool read)> done) = 0;
};

}  // namespace logos::installer
