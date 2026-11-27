# basecamp-installer-common

What Logos' two installer apps, App Manager and Package Manager, share with each other and with Basecamp's consent dialogs: package rows and the rules behind them, and the words for them. Compiled and copied into each consumer; not a module.

## What is in it

**C++** (`include/logos/installer/`, namespace `logos::installer`), pure Qt Core:

| Header | What |
|---|---|
| `Types.h` | `InstallStatus`, `InstallStage`, `RowAction`, `NotAvailableReason`, `DownloadSource`. Each app's `.rep` mirrors the values: append, never renumber |
| `Availability.h` | whether a catalog package installs on this host, and why not |
| `RowState.h` | a row against disk (`installStatusOf`), its primary action, updates, uninstallable |
| `PackageRows.h` | catalog rows and installed packages into rows; selecting a version |
| `RepositoryLabels.h` | the label each repository is shown under, and the order of the sections |
| `PackageModel.h` | the package list: rows from the catalog, what is installed and this host's variants, rebuilt in place; App Manager's option to count an app's dependencies in its status; what installing a package brings (`packagesBroughtBy()`) |
| `InstallProgress.h` | the installs in flight: each package's stage and download, and the whole install an app started |
| `PackageFilter.h` | search (name, display name, description), type, category, installed (on disk), unavailable, repository and local filters; sections in order, sorting within them; the category and type lists |
| `PagingProxy.h` | one page of a long list |
| `CatalogClient.h` | keeps the list filled from the package modules: load, reload, refresh on their events; repositories, download source, storage state |
| `PackageModules.h` | the package modules as the backend reaches them; the library makes no module calls of its own |
| `IconFetcher.h` | the catalog's icons, fetched over HTTP(S), checked (PNG, size, the catalog's sha256), kept by hash, and given to the view as `data:` URLs |
| `OnlineWatch.h` | whether this host is online, for the download notice |
| `Installer.h` | installs, upgrades and uninstalls only what Basecamp approves; one request at a time, an install or upgrade waiting its turn asked about at once (`waits`; a `ticket` for its turn); an install or upgrade asks with the versions and optional packages picked (`versions`, `optional`) |
| `Consent.h` | asking Basecamp to approve (`basecamp.packages.confirm_install / _upgrade / _uninstall`), raised by the backend so the answer comes back to it alone |
| `IntentConsent.h` | `Consent` through intents_module: the request, then its result read with the receipt only the backend holds once it has settled |
| `InstallModules.h` | the package-module calls installing makes, as the backend reaches them |
| `Approval.h` | holding an install to the approval: the plan or file, nothing outside it, nothing missing, same signer, same release |
| `Trust.h` | who signed a package and whether the person trusts them, the one judge Basecamp's install dialogs and the apps share (namespace `logos::trust`): Logos's keys, a catalogue release, an installed copy or a local file judged, and what a dialog shows (`summary()`). Needs ICU (below) |
| `PackageTrust.h` | `Trust.h`'s verdict, by name, of each package `PackageModel::packagesBroughtBy()` lists: the release picked for what installs, the copy on disk for what stays (`trustOfBrought()`) |

**JavaScript** (`qml/Installer/`), `.pragma library`, facts and words only, nothing visual:

| File | What |
|---|---|
| `Types.js` | the values of `Types.h` |
| `Downloads.js` | bytes and progress text, transport caption and icon name, finding peers, the download notice |
| `Stages.js` | stage, progress and action words; version transitions |
| `Availability.js` | why a package isn't available, in words |
| `Trust.js` | a `summary()` verdict in words, and its mark: check or warning, and its tone |

Components and screens stay in each app.

## Using it

Take the flake as an input, stage `packages.default` into the source tree (as Package Manager stages logos-package's headers), then:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/vendor/basecamp-installer-common/cmake/BasecampInstallerCommon.cmake)
logos_module(
    ...
    SOURCES ... ${BASECAMP_INSTALLER_COMMON_SOURCES}
    INCLUDE_DIRS ... ${BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS}   # plus logos-package's headers
)
```

To show who signed a package, add `${BASECAMP_INSTALLER_COMMON_TRUST_SOURCES}`
too and link ICU, as Qt does: `find_package(ICU REQUIRED COMPONENTS uc i18n)`,
then `ICU::i18n ICU::uc`. They need nothing else of the library, so Basecamp
builds them alone; an app that compiles both adds
`${BASECAMP_INSTALLER_COMMON_PACKAGE_TRUST_SOURCES}` for `PackageTrust.h`. The
keyring comes from `PackageModules::listTrustedKeys()`: package_manager's,
which Basecamp points at its own.

A backend implements `PackageModules` with its typed wrappers, forwards the
modules' events, and shows the list through a filter:

```cpp
using namespace logos::installer;
PackageModel packages;                 // App Manager: setStatusIncludesDependencies(true)
InstallProgress progress;
packages.setProgress(&progress);

CatalogClient catalog(&modules, &packages);
catalog.load();                        // once the modules are reachable
// package_manager *FileInstalled / *Uninstalled  -> catalog.onPackagesChanged()
// package_downloader catalogChanged              -> catalog.onCatalogChanged()
// package_downloader storageStateChanged         -> catalog.onStorageStateChanged(state, error)

IconFetcher icons(&packages);
icons.setCacheDirectory(persistenceDir + "/icons");

PackageFilter shown;
shown.setSourceModel(&packages);
shown.setSourceOrder(catalog.sourceOrder());
```

Icons come from beside the catalog's index, over HTTP(S): the index names no
Logos Storage copy of them yet.

Installing: the backend implements `Consent` over intents_module (request,
then the result once settled) and `InstallModules` with its typed wrappers,
and forwards package_downloader's `downloadProgress` to the `InstallProgress`:

```cpp
Installer installer(&modules, &consent, &progress);
installer.setPackages(&packages);   // download sizes for the progress
// What the screen asks for. None of these takes an approval: Basecamp's
// answer to the installer's own confirm_* is the only one there is.
installer.install(name, version, repositoryUrl);
installer.upgrade(name, version, mode, repositoryUrl);
installer.installFile(path);
installer.uninstall({name});
// finished(name, Done | Cancelled | Failed, error)
// changeEnded(change): the change Basecamp approved with `change` in its
// answer is over. Forward it to Basecamp: it ends its change on this, not on
// finished(), which a request cancelled while waiting its turn emits too.
```

An approved catalog release is downloaded pinned, release by release; held to
the approval; copied where only this backend writes; each copy checked to be
the approved release and signed as approved, and the approved file when the
approval names its SHA-256 (Basecamp does, for an unsigned release, as its root
hash leaves its manifest out); then installed, a copy the user
installed earlier removed only after its replacement passed those checks; and
each install checked on disk: the approved version and root hash, in a copy
other than Basecamp's own. An install whose call timed out, or that can't be
read back, keeps its turn and its files until a read answers: package_manager
runs one call at a time, so that read came after the install ended. With no
answer, its files stay while the installer lives. A read of the installed
packages that fails before installing installs nothing. What is on disk already as
approved is not fetched again, except the package an upgrade names; an
uninstall removes what Basecamp's answer names, and nothing when it names
nothing. A local file is installed only while its SHA-256 is the one Basecamp
approved.

Installing is not a transaction: a package that fails after its old copy was
removed leaves neither, and what installed before it stays installed.

A `PackageModules` read whose call failed answers `read` false: the list stays
as it was, and `CatalogClient` says what it couldn't read (`loadFailed()`,
`loadError()`).

The category list has no "All": each app adds its own.

`tools/shared-words-check` fails when an app's own QML writes a shared phrase
again instead of calling the shared file:

```bash
tools/shared-words-check <this repo>/qml/Installer <app qml dir> \
    [--skip <the app's copy of qml/Installer>] [--allow <exceptions file>]
```

Copy `qml/Installer/` into the app's QML tree, and import the files relatively (`import "Installer/Downloads.js" as Downloads`): a sandboxed app view can import only its own package's files and the design system. Basecamp's flake makes itself and both apps follow one revision.

## Tests

```bash
nix build .#checks.<system>.unit   # C++ rules
nix build .#checks.<system>.qml    # the JavaScript
nix build .#checks.<system>.shared-words   # the checker
```

## Logos's keys

Basecamp and the apps trust Logos's packages by the keys in `logosKeys()`
(`src/Trust.cpp`). Before every release, check that the first one is the key
Logos signs with now: the "Official Logos signing key" in
[logos-modules-release's README](https://github.com/logos-co/logos-modules-release#official-logos-signing-key).
`lgx verify` on a freshly signed official package prints the same DID.

- **A new key:** put it first, and keep the old one while what it signed
  should still install without asking. The release must be out before Logos
  signs anything with the new key: logos-modules-release re-signs every
  current package in the same change as a rotation, and a Basecamp that
  doesn't know the key shows all of them as impostors.
- **A leaked key:** move it to `revoked`, and put its replacement first in the
  same change. Nothing it signed then installs.
- Never just delete a key from the list, and never leave it without a current
  key (the unit tests check). Users do nothing either way: a keyring holding
  any Logos key, old or revoked, stands for Logos and the keys it signs with
  now.
