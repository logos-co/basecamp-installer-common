#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Who signed a package, and whether the person trusts them: the one judge
// Basecamp's install dialogs and the installer apps share, so what an app's
// details say is what the dialog says. Basecamp adds what only it can judge
// (its own apps and modules, an app's backend name) before it asks.
// Pure functions; the lookalike checks use ICU's spoof checker, which the
// consumer links (BasecampInstallerCommon.cmake's TRUST sources).
namespace logos::trust {

// Logos's signing keys, as Basecamp and the installer apps ship them.
struct LogosKeys {
    QStringList dids;     // the key Logos signs with now, then older ones still good
    QStringList revoked;  // never to be trusted again
};

// The list. A new key goes first, and the one it replaces stays while what it
// signed should install without asking; a leaked key moves to `revoked`. None
// is ever just dropped. Checked before every release: see the README,
// "Logos's keys".
LogosKeys logosKeys();
QString logosDid();       // the key Logos signs with now
QString logosKeyName();
QString logosName();
QString logosUrl();

enum class Status {
    Trusted,    // signed by a trusted publisher
    Untrusted,  // signed by a publisher not trusted yet
    Unsigned,   // nobody signed it
    Unknown,    // who signed it could not be checked
    Invalid,    // damaged, or a signature that does not hold
    Bundled,    // already there, shipped with Basecamp itself
};

struct Package {
    QString name{};
    QString version{};
    Status status = Status::Unsigned;
    QString did{};
    QString signerName{};  // the package's own claim: shown, never trusted
    QString signerUrl{};
    QString sha256{};      // the catalogue's digest of the whole file, its manifest included
    bool installed = false;
    bool offered = false;
    // Basecamp's own rules, set by it before summary():
    bool builtIn = false;      // one of Basecamp's own apps, whoever signed it: Invalid
    bool reserved = false;     // ships with Basecamp, and not signed by Logos: Invalid
    bool backendTaken = false; // an app whose `<name>_backend` another installed package has: Invalid
    bool checkFailed = false;  // its signature check didn't answer: Invalid, nothing to go on
    QString replacesSignedBy{};  // the key that signed the copy it replaces, when another signs it
    bool replacesUnverified = false;  // replaces a copy no checkable key signed, and isn't Logos's
};

// Whether `did` is an Ed25519 key's did:jwk exactly as liblgx writes it. Any
// other spelling of a key is refused: liblgx reads the key out of it leniently,
// and its text can be made to look like another key's.
bool isCanonicalDid(const QString& did);

// What the dialogs show for a key: the first 64 bits of the SHA-256 of the raw
// public key, as "7F3A 91C2 0B4D E8A6". Empty unless `did` is canonical.
QString fingerprint(const QString& did);

// A publisher's or package's own text, made fit to show: line breaks, and
// characters that are invisible or turn the text around, become spaces, and a
// long one is cut so that what follows it stays in view.
QString shown(const QString& text);

// The DIDs package_manager's listTrustedKeys() trusts. A Logos key in it, of
// any age, stands for every key Logos signs with; a revoked one for none.
QStringList trustedDids(const QVariantList& trustedKeys, const LogosKeys& logos = logosKeys());

// A planned package, judged by the release the downloader would fetch for it:
// the one among `versions` (one repository's entries for the package) with
// this version and rootHash, that the download source can serve. That is the
// release whose signer the downloader holds the file to. Unknown when there is
// none, or when such releases disagree about who signed it.
Package fromCatalog(const QString& name, const QString& version, const QString& rootHash,
                    const QVariantList& versions, const QStringList& trusted);

// A package on disk, judged by package_manager's record of the installed copy
// ({installType, signerDid}): shipped with Basecamp, or signed (or not) by the
// key the copy names. Unknown without a record. Marked `installed`.
Package fromInstalled(const QString& name, const QString& version, const QVariantMap& record,
                      const QStringList& trusted);

// A local .lgx file, judged by package_manager's verifyPackage() of it.
Package fromVerifiedFile(const QString& name, const QString& version,
                         const QVariantMap& verified, const QStringList& trusted);

// package_manager's record of `name` among the `installed` packages: the one
// by exactly that name, else one a case-insensitive file system finds under it
// (Wallet_UI installs over wallet_ui). Empty if none.
QVariantMap installedRecordOf(const QVariantList& installed, const QString& name);

// Every key of the publisher `did` is: all of Logos's, old and revoked too,
// for a Logos key; otherwise just `did`.
QStringList publisherKeys(const QString& did, const LogosKeys& logos = logosKeys());

// A trusted key's name as it is shown: Logos's for a Logos key, and nothing
// for any other key whose name reads as Logos, whoever put it in the keyring.
QString publisherName(const QString& displayName, const QString& did, const LogosKeys& logos = logosKeys());

// A trusted key's address as it is shown: Logos's for a Logos key, and
// nothing for any other key whose address claims Logos.
QString publisherUrl(const QString& url, const QString& did, const LogosKeys& logos = logosKeys());

// What the install dialogs show, `about` being the package the dialog is
// about (without one, nothing is offered for trust):
//   needsPrompt     something is not signed by a trusted publisher
//   blocked         something is damaged or wrongly signed: Cancel only
//   canTrust        `about` is signed by a publisher not trusted yet, which
//                   may be: "Trust & install" is offered, and trusts that
//                   publisher (`trust`) and nobody else
//   trustCoversAll  trusting it would leave nothing to ask about, so the
//                   other answer is a plain "Install", not "Install anyway"
//   trust        that publisher, as in `publishers`, or empty
//   packages     [{name, version, status, did, fingerprint, signerName,
//                signerUrl}], the ones not trusted
//   publishers   [{did, fingerprint, name, url, packages, impersonates}], the
//                signers not trusted yet. `impersonates` is the name of a
//                trusted publisher (or Logos) whose name this one uses with
//                another key; such a publisher is not offered for trust.
//   byName       {<package>: {status, publisher, impersonates, installed}}
//                for every package judged, keyed by its name as planned: what
//                its row says (Trust.js puts it in words). `publisher` is the
//                name a trusted publisher was trusted by, or the name an
//                untrusted one claims. A package `installed` already, or only
//                `offered`, is shown here and nowhere else: it asks nothing.
// Every name and URL in it has been through shown(). A package signed with a
// revoked Logos key is Invalid.
QVariantMap summary(const QList<Package>& packages, const QVariantList& trustedKeys,
                    const QString& about = {}, const LogosKeys& logos = logosKeys());

// `packages` as judged without a keyring to check their signers against:
// trusted or not, a signed one is Unknown.
QList<Package> unchecked(QList<Package> packages);

QString keyNameFor(const QString& did);
QString statusName(Status status);

} // namespace logos::trust
