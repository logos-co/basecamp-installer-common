#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Holding an install to what Basecamp approved. An approval is Basecamp's
// answer to a backend's confirm_install or confirm_upgrade: a plan
// ({plan: [{name, version, rootHash, repositoryUrl, signer?, sha256?, optional?}]})
// for a catalog release, or a file ({file: {rootHash, signer, sha256}}) for a
// local .lgx. Pure functions, from both installer apps' pipelines.
namespace logos::installer {

enum class ApprovalSource { File, Catalog, Refused, Unapproved };

// A file approval needs its file still waiting (`fileWaiting`); a plan is
// always the catalog's; anything else approves nothing.
ApprovalSource sourceOf(const QVariantMap& approval, bool fileWaiting);

// Why `approval` lets nothing be installed as `name`, or empty: it names
// neither a plan nor a file, its file is gone, or its plan does not say
// exactly what to install.
QString refusal(const QVariantMap& approval, bool fileWaiting, const QString& name);

// Why `plan` does not say exactly what to install (a row without its name,
// version, root hash or repository, or no row for `name`), or empty.
QString malformedPlan(const QVariantList& plan, const QString& name);

// `plan` without what is installed already as approved (name, version and
// root hash, against getInstalledPackages() rows), except `keep`: the package
// an upgrade asks for, which is fetched again even as it is ("the same
// version again").
QVariantList stillToInstall(const QVariantList& plan, const QVariantList& installed,
                            const QString& keep);

// Why the packages fetched for `requested` (stillToInstall()) may not be
// installed: one `plan` doesn't hold, or a required one that failed or didn't
// come. Empty when they may; a failed optional one is just left out.
QString notAsApproved(const QVariantList& fetched, const QVariantList& requested,
                      const QVariantList& plan);

// Why a fetched file, as package_manager's verifyPackage() read it, is not
// signed as `plan` approved its release: not valid, or not by the approved
// key (signed at all, for one approved unsigned). A check that didn't run
// proves nothing.
QString signerChanged(const QVariantMap& verified, const QVariantList& plan, const QVariantMap& row);

// Why the file inspectPackage() describes as `inspected` is not the release
// `row` names (name, version, root hash), or empty.
QString releaseChanged(const QVariantMap& inspected, const QVariantMap& row);

// Why a fetched file whose SHA-256 is `sha256` is not the file `plan`
// approved for `row`'s release, or empty. A release approved unsigned carries
// the catalog's digest of its file, as its root hash leaves its manifest out;
// one approved without a digest is held to its root hash alone.
QString digestChanged(const QString& sha256, const QVariantList& plan, const QVariantMap& row);

// Why the file inspectPackage() describes as `inspected`, whose SHA-256 is
// `sha256`, is not the one approved ({rootHash, signer, sha256}) as `name` at
// `version`, or empty. An approval without the file's digest approves nothing.
QString fileChanged(const QVariantMap& inspected, const QVariantMap& approvedFile,
                    const QString& name, const QString& version, const QString& sha256);

// The SHA-256 of the file at `path`, in hex; empty if it can't be read.
QString sha256Of(const QString& path);

// `fetched` with each downloaded file (`path`) copied into `dir`, `path` then
// naming the copy: what is checked and installed can't change under another
// write to the downloader's path. Empty, with `error` set, when a copy fails.
QVariantList intoPrivateCopies(const QVariantList& fetched, const QString& dir, QString* error);

// Whether the installed copy of `name` must be removed before its
// replacement installs: a user-installed one is; an embedded one stays in the
// application, and installPlugin writes the replacement beside it.
bool removeBeforeInstall(const QVariantList& installed, const QString& name);

// Whether installPlugin's reply says it installed: every real reply carries
// `path` (empty for a QML-only package), and a failure an `error`.
bool pluginInstalled(const QVariantMap& reply);

// Why `installed` (getInstalledPackages() after an install) doesn't hold
// `name` at `version` with `rootHash`, or empty.
QString notOnDisk(const QVariantList& installed, const QString& name, const QString& version,
                  const QString& rootHash);

// confirm_upgrade's mode for replacing `installedVersion` with `version`:
// 0 upgrade, 1 downgrade, 2 the same version again.
int upgradeMode(const QString& version, const QString& installedVersion);

}  // namespace logos::installer
