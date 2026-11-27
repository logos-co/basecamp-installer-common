#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace logos::installer {

// package_downloader and package_manager, as a backend reaches them: through
// its typed wrappers, so the library makes no module calls of its own. Every
// call answers once through its callback, on the backend's thread. A module
// that doesn't know a method answers with an empty value; a read whose call
// failed says so (`read` false), so it isn't taken for an empty one.
class PackageModules {
public:
    virtual ~PackageModules() = default;

    // package_downloader.start(): `started` once it answers success. A load
    // after one that didn't (no answer, or a refusal) asks again; a
    // downloader that predates start() refuses it and is running already.
    virtual void startDownloader(std::function<void(bool started)> done) = 0;
    virtual void listRepositories(std::function<void(const QVariantList&, bool read)> done) = 0;
    virtual void getCatalog(std::function<void(const QVariantList&, bool read)> done) = 0;
    // package_downloader.refreshCatalog(): fetch every repository again.
    // `error` is empty on success.
    virtual void refreshCatalog(std::function<void(const QString& error)> done) = 0;
    virtual void getDownloadSource(std::function<void(const QString&)> done) = 0;
    // {state, error}
    virtual void getStorageState(std::function<void(const QVariantMap&)> done) = 0;

    virtual void getInstalledPackages(std::function<void(const QVariantList&, bool read)> done) = 0;
    virtual void getValidVariants(std::function<void(const QStringList&, bool read)> done) = 0;
    // package_manager.listTrustedKeys(): the keys the person trusts, in the
    // keyring Basecamp points package_manager at. A backend that doesn't show
    // who signed a package needn't read it.
    virtual void listTrustedKeys(std::function<void(const QVariantList&, bool read)> done)
    {
        done({}, false);
    }
};

}  // namespace logos::installer
