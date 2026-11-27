// Who signed a package, and whether the person trusts them (Trust.h): how a
// release is judged from the catalogue, an installed copy from its record, a
// local file from its verification, and what the install dialogs are told to
// show. Basecamp tests what it adds on top: its own names and the plan.

#include "check.h"

#include <logos/installer/Trust.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace logos::trust;

namespace {

// Canonical did:jwk of two made-up Ed25519 keys (bytes 1..32 and 101..132),
// spelled the way liblgx spells them.
const QString kOtherDid = QStringLiteral(
    "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6IkFRSURCQVVHQndnSkNnc01EUTRQRUJFU0V4"
    "UVZGaGNZR1JvYkhCMGVIeUEifQ");
const QString kThirdDid = QStringLiteral(
    "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6IlpXWm5hR2xxYTJ4dGJtOXdjWEp6ZEhWMmQz"
    "aDVlbnQ4Zlg1X2dJR0NnNFEifQ");

QVariantMap release(const QString& name, const QString& version, const QString& rootHash, const QString& did = {},
                    const QString& signerName = {}, const QString& signerUrl = {})
{
    QVariantMap entry{{QStringLiteral("rootHash"), rootHash},
                      {QStringLiteral("manifest"), QVariantMap{{QStringLiteral("name"), name},
                                                               {QStringLiteral("version"), version}}}};
    if (!did.isEmpty()) {
        entry[QStringLiteral("signature")] = QVariantMap{
            {QStringLiteral("did"), did},
            {QStringLiteral("signer"), QVariantMap{{QStringLiteral("name"), signerName},
                                                   {QStringLiteral("url"), signerUrl}}}};
    }
    return entry;
}

QVariantMap verified(bool isSigned, bool signatureValid, bool packageValid, const QString& did = {})
{
    return {{QStringLiteral("isSigned"), isSigned},
            {QStringLiteral("signatureValid"), signatureValid},
            {QStringLiteral("packageValid"), packageValid},
            {QStringLiteral("signerDid"), did},
            {QStringLiteral("signerName"), QStringLiteral("Someone")},
            {QStringLiteral("signerUrl"), QStringLiteral("https://someone.example")}};
}

QString didOfJwk(const QByteArray& json, QByteArray::Base64Options options =
                     QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
{
    return QStringLiteral("did:jwk:") + QString::fromLatin1(json.toBase64(options));
}

QStringList names(const QVariantList& rows)
{
    QStringList out;
    for (const QVariant& row : rows)
        out << row.toMap().value(QStringLiteral("name")).toString();
    return out;
}

} // namespace

// The built-in key is one string split over two literals: a slip there
// would make every Logos package read as untrusted.
TEST(theLogosKeyIsAnEd25519PublicKey)
{
    CHECK(logosDid().startsWith(QStringLiteral("did:jwk:")));
    const QByteArray jwk = QByteArray::fromBase64(
        logosDid().mid(8).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QJsonObject key = QJsonDocument::fromJson(jwk).object();
    CHECK_EQ(key.value(QStringLiteral("kty")).toString(), QStringLiteral("OKP"));
    CHECK_EQ(key.value(QStringLiteral("crv")).toString(), QStringLiteral("Ed25519"));
    CHECK_EQ(QByteArray::fromBase64(key.value(QStringLiteral("x")).toString().toLatin1(),
                                    QByteArray::Base64UrlEncoding).size(), 32);
    CHECK(isCanonicalDid(logosDid()));
}

// A key is shown by a fingerprint of the key itself: every Ed25519 did:jwk
// starts with the same 22 characters, and its last few can be chosen.
TEST(aKeyIsShownByItsOwnFingerprint)
{
    CHECK_EQ(fingerprint(logosDid()), QStringLiteral("4BBC C11D CB35 F9FD"));
    CHECK_EQ(fingerprint(kOtherDid), QStringLiteral("AE21 6C2E F524 7A37"));
    // The other key, with a member added so the DID ends like Logos's.
    const QString lookalike = didOfJwk(
        "{\"crv\":\"Ed25519\",\"kty\":\"OKP\",\"x\":\"AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA\","
        "\"y\":\"Ao5iI\"}");
    CHECK_EQ(lookalike.right(8), logosDid().right(8));
    CHECK_EQ(fingerprint(lookalike), QString());
}

// liblgx finds the key in a DID leniently. Anything but its own spelling
// of a key names no signer Basecamp will vouch for.
TEST(anotherSpellingOfAKeyIsNotThatKey)
{
    const QByteArray x = "ZTtB3iOEaVCXUKQl4JolGuu2HLo_b9HRCevF4H6o5iI";   // Logos's key
    const QStringList spellings{
        didOfJwk("{\"kty\":\"OKP\",\"crv\":\"Ed25519\",\"x\":\"" + x + "\"}"),
        didOfJwk("{\"crv\":\"Ed25519\",\"kty\":\"OKP\",\"x\":\"" + x + "\",\"y\":\"1\"}"),
        didOfJwk("{\"crv\":\"Ed25519\",\"kty\":\"OKP\",\"x\":\"" + x + "\"}",
                 QByteArray::Base64UrlEncoding),   // padded
        didOfJwk("{\"crv\":\"Ed25519\",\"kty\":\"OKP\", \"x\":\"" + x + "\"}"),
        QStringLiteral("did:jwk:not base64!"),
        QStringLiteral("did:key:z6Mk")};
    // Not that key, nor any it could pass for, a revoked one included:
    // what an install would bring is refused. It used to be "couldn't
    // check", which can still be installed. An installed copy says so.
    for (const QString& did : spellings) {
        CHECK(!isCanonicalDid(did));
        CHECK_EQ(fromVerifiedFile(QStringLiteral("app"), {}, verified(true, true, true, did),
                                  {did, logosDid()}).status, Status::Invalid);
        CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("a"),
                             {release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("a"), did)}, {did})
                     .status, Status::Invalid);
        CHECK_EQ(fromInstalled(QStringLiteral("app"), {}, {{QStringLiteral("installType"), QStringLiteral("user")},
                                                           {QStringLiteral("signerDid"), did}}, {did})
                     .status, Status::Unknown);
    }
}

TEST(theReleaseThePlanNamesDecides)
{
    const QVariantList versions{
        release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("old"), logosDid(), QStringLiteral("Logos")),
        release(QStringLiteral("app"), QStringLiteral("2.0.0"), QStringLiteral("new"), kOtherDid, QStringLiteral("Other"),
                QStringLiteral("https://other.example")),
        release(QStringLiteral("app"), QStringLiteral("3.0.0"), QStringLiteral("bare"))};
    const QStringList trusted{logosDid()};

    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("old"),
                         versions, trusted).status, Status::Trusted);

    const Package other = fromCatalog(QStringLiteral("app"), QStringLiteral("2.0.0"),
                                      QStringLiteral("new"), versions, trusted);
    CHECK_EQ(other.status, Status::Untrusted);
    CHECK_EQ(other.did, kOtherDid);
    CHECK_EQ(other.signerName, QStringLiteral("Other"));
    CHECK_EQ(other.signerUrl, QStringLiteral("https://other.example"));

    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("3.0.0"), QStringLiteral("bare"),
                         versions, trusted).status, Status::Unsigned);
    // Not in the catalogue we have: who signed it is unknown, which is
    // not the same as nobody.
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("gone"),
                         versions, trusted).status, Status::Unknown);
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), {}, versions, trusted).status, Status::Unknown);
}

// The regression. A catalogue lists the release the plan downloads
// unsigned, and another version with the same content hash "signed by
// Logos", which nobody downloads and so nobody checks. Matching the hash
// alone read the plan as Logos's.
TEST(aReleaseNobodyDownloadsCannotVouchForOneThatIs)
{
    const QVariantList versions{
        release(QStringLiteral("delivery_module"), QStringLiteral("9.0.1-a"), QStringLiteral("H"), logosDid(), QStringLiteral("Logos")),
        release(QStringLiteral("delivery_module"), QStringLiteral("9.0.0"), QStringLiteral("H"))};
    CHECK_EQ(fromCatalog(QStringLiteral("delivery_module"), QStringLiteral("9.0.0"),
                         QStringLiteral("H"), versions, {logosDid()}).status, Status::Unsigned);
}

// The same release listed twice with different signers: which one the
// downloader holds the file to is not Basecamp's to guess.
TEST(releasesThatDisagreeAboutTheSignerAreUnknown)
{
    const QVariantList disagree{
        release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"), logosDid()),
        release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"))};
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"),
                         disagree, {logosDid()}).status, Status::Unknown);

    const QVariantList agree{
        release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"), logosDid()),
        release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"), logosDid())};
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"),
                         agree, {logosDid()}).status, Status::Trusted);
}

// A release the download source cannot serve is never the one fetched.
TEST(aReleaseTheSourceCannotServeIsNotTheOne)
{
    QVariantMap unserved = release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"), logosDid());
    unserved[QStringLiteral("sourceAvailable")] = false;
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"),
                         {unserved}, {logosDid()}).status, Status::Unknown);
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"),
                         {unserved, release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("H"))},
                         {logosDid()}).status, Status::Unsigned);
}

// A local file is judged by verifying it, not by what it says.
TEST(aLocalFileIsJudgedByItsVerification)
{
    const QStringList trusted{logosDid()};
    CHECK_EQ(fromVerifiedFile(QStringLiteral("app"), {}, verified(true, true, true, logosDid()),
                              trusted).status, Status::Trusted);
    const Package other = fromVerifiedFile(QStringLiteral("app"), {},
                                           verified(true, true, true, kOtherDid), trusted);
    CHECK_EQ(other.status, Status::Untrusted);
    CHECK_EQ(other.signerName, QStringLiteral("Someone"));
    CHECK_EQ(fromVerifiedFile(QStringLiteral("app"), {}, verified(false, false, true), trusted).status, Status::Unsigned);
    CHECK_EQ(fromVerifiedFile(QStringLiteral("app"), {}, verified(true, false, true, logosDid()),
                              trusted).status, Status::Invalid);
    CHECK_EQ(fromVerifiedFile(QStringLiteral("app"), {}, verified(false, false, false), trusted).status, Status::Invalid);
    CHECK_EQ(fromVerifiedFile(QStringLiteral("app"), {}, QVariantMap{}, trusted).status, Status::Invalid);
}

// A check that didn't answer blocks like a bad signature, and says which it was.
TEST(aCheckThatFailedBlocksAndSaysSo)
{
    Package app{QStringLiteral("app"), {}, Status::Invalid};
    app.checkFailed = true;
    const QVariantMap s = summary({app}, {});
    CHECK_EQ(s.value(QStringLiteral("blocked")).toBool(), true);
    const QVariantMap row = s.value(QStringLiteral("packages")).toList().value(0).toMap();
    CHECK_EQ(row.value(QStringLiteral("status")).toString(), QStringLiteral("invalid"));
    CHECK_EQ(row.value(QStringLiteral("checkFailed")).toBool(), true);
    const QVariantMap verdict = s.value(QStringLiteral("byName")).toMap().value(QStringLiteral("app")).toMap();
    CHECK_EQ(verdict.value(QStringLiteral("checkFailed")).toBool(), true);
}

// Nothing to decide, but each row still says who signed it: before, a
// fully trusted install showed nothing at all.
TEST(aFullyTrustedInstallAsksNothingButSaysWhoSignedIt)
{
    const QVariantList keys{QVariantMap{{QStringLiteral("did"), logosDid()},
                                        {QStringLiteral("displayName"), QStringLiteral("claimed")}},
                            QVariantMap{{QStringLiteral("did"), kOtherDid},
                                        {QStringLiteral("displayName"), QStringLiteral("Acme")}}};
    const QVariantMap s = summary({{QStringLiteral("a"), {}, Status::Trusted, logosDid()},
                                   {QStringLiteral("b"), {}, Status::Trusted, logosDid()},
                                   {QStringLiteral("c"), {}, Status::Trusted, kOtherDid}}, keys);
    CHECK_EQ(s.value(QStringLiteral("needsPrompt")).toBool(), false);
    CHECK_EQ(s.value(QStringLiteral("blocked")).toBool(), false);
    CHECK(s.value(QStringLiteral("packages")).toList().isEmpty());
    CHECK(s.value(QStringLiteral("publishers")).toList().isEmpty());

    const QVariantMap byName = s.value(QStringLiteral("byName")).toMap();
    CHECK_EQ(byName.size(), 3);
    // Logos goes by Basecamp's name for it, whatever its keyring entry says.
    CHECK_EQ(byName.value(QStringLiteral("a")).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("trusted"));
    CHECK_EQ(byName.value(QStringLiteral("a")).toMap().value(QStringLiteral("publisher")).toString(), QStringLiteral("Logos"));
    CHECK_EQ(byName.value(QStringLiteral("c")).toMap().value(QStringLiteral("publisher")).toString(), QStringLiteral("Acme"));
}

// Every package judged has a row verdict, keyed by its planned name.
TEST(everyPackageGetsARowVerdict)
{
    const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Untrusted, kOtherDid,
                                    QStringLiteral("Logos")},
                                   {QStringLiteral("loose"), {}, Status::Unsigned},
                                   {QStringLiteral("unchecked"), {}, Status::Unknown},
                                   {QStringLiteral("bad"), {}, Status::Invalid},
                                   {QStringLiteral("core"), {}, Status::Trusted, logosDid()}}, {});
    const QVariantMap byName = s.value(QStringLiteral("byName")).toMap();
    const auto field = [&byName](const char* name, const char* key) {
        return byName.value(QLatin1String(name)).toMap().value(QLatin1String(key)).toString();
    };
    CHECK_EQ(field("app", "status"), QStringLiteral("untrusted"));
    CHECK_EQ(field("app", "publisher"), QStringLiteral("Logos"));
    CHECK_EQ(field("app", "impersonates"), QStringLiteral("Logos"));
    CHECK_EQ(field("loose", "status"), QStringLiteral("unsigned"));
    CHECK_EQ(field("unchecked", "status"), QStringLiteral("unknown"));
    CHECK_EQ(field("bad", "status"), QStringLiteral("invalid"));
    CHECK_EQ(field("core", "status"), QStringLiteral("trusted"));
}

// One prompt for the whole install: every package that is not trusted,
// and each publisher not trusted yet once, with the packages it signed.
TEST(oneInstallIsOnePromptListingWhatIsNotTrusted)
{
    const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Untrusted, kOtherDid,
                                    QStringLiteral("Other")},
                                   {QStringLiteral("dep"), {}, Status::Untrusted, kOtherDid},
                                   {QStringLiteral("loose"), {}, Status::Unsigned},
                                   {QStringLiteral("unchecked"), {}, Status::Unknown},
                                   {QStringLiteral("core"), {}, Status::Trusted, logosDid()}}, {});
    CHECK_EQ(s.value(QStringLiteral("needsPrompt")).toBool(), true);
    CHECK_EQ(s.value(QStringLiteral("blocked")).toBool(), false);
    CHECK_EQ(names(s.value(QStringLiteral("packages")).toList()), (QStringList{QStringLiteral("app"), QStringLiteral("dep"), QStringLiteral("loose"),
                          QStringLiteral("unchecked")}));

    const QVariantList publishers = s.value(QStringLiteral("publishers")).toList();
    CHECK_EQ(publishers.size(), 1);
    const QVariantMap publisher = publishers.first().toMap();
    CHECK_EQ(publisher.value(QStringLiteral("did")).toString(), kOtherDid);
    CHECK_EQ(publisher.value(QStringLiteral("fingerprint")).toString(), fingerprint(kOtherDid));
    CHECK_EQ(publisher.value(QStringLiteral("name")).toString(), QStringLiteral("Other"));
    CHECK_EQ(publisher.value(QStringLiteral("impersonates")).toString(), QString());
    CHECK_EQ(publisher.value(QStringLiteral("packages")).toStringList(), (QStringList{QStringLiteral("app"), QStringLiteral("dep")}));
}

// A key that takes the name of Logos, or of a publisher the user trusts,
// is flagged, and the dialogs do not offer to trust it.
TEST(aPublisherTakingATrustedNameIsFlagged)
{
    const QVariantList keys{QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Acme")},
                                        {QStringLiteral("did"), kThirdDid}},
                            QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Nimbus")},
                                        {QStringLiteral("did"), QStringLiteral("did:jwk:nimbus")}},
                            QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Mail")},
                                        {QStringLiteral("did"), QStringLiteral("did:jwk:mail")}}};
    const auto flagged = [&keys](const QString& claimed, const QString& did) {
        const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Untrusted, did, claimed}}, keys);
        return s.value(QStringLiteral("publishers")).toList().first().toMap()
            .value(QStringLiteral("impersonates")).toString();
    };
    CHECK_EQ(flagged(QStringLiteral("Logos"), kOtherDid), QStringLiteral("Logos"));
    CHECK_EQ(flagged(QStringLiteral("  LOGOS "), kOtherDid), QStringLiteral("Logos"));
    CHECK_EQ(flagged(QStringLiteral("ａｃｍｅ"), kOtherDid), QStringLiteral("Acme"));   // full width
    // Logos itself, deleted from the keyring, is not pretending to be anyone.
    CHECK_EQ(flagged(QStringLiteral("Logos"), logosDid()), QString());
    CHECK_EQ(flagged(QStringLiteral("Other"), kOtherDid), QString());
    CHECK_EQ(flagged(QStringLiteral("Logos Inc"), kOtherDid), QStringLiteral("Logos"));   // a word of it
    // As shown, too: cut where it is shown, what is left reads as Logos,
    // though the whole name does not.
    const QString padded = QStringLiteral("Logos") + QString(70, QLatin1Char('.')) + QStringLiteral("Labs");
    CHECK_EQ(flagged(padded, kOtherDid), QStringLiteral("Logos"));
    CHECK_EQ(summary({{QStringLiteral("app"), {}, Status::Untrusted, kOtherDid, padded}}, keys,
                     QStringLiteral("app"))
                 .value(QStringLiteral("canTrust")).toBool(), false);

    // A name that reads the same is the same: invisible characters (in the
    // BMP or not), lookalike letters from other scripts, digits and a
    // capital I passing for letters, spacing and punctuation.
    const QStringList lookalikes{
        QStringLiteral("Logos\u200B"), QStringLiteral("\uFEFFLogos"), QStringLiteral("Lo\u034Fgos"),
        QStringLiteral("Logos\uFE0F"), QStringLiteral("Lo\U000E0067gos"), QStringLiteral("Logos\u00AD"),
        QStringLiteral("L\u043Egos"), QStringLiteral("L\u03BFgos"), QStringLiteral("L0gos"),
        QStringLiteral("Iogos"), QStringLiteral("Lo gos"), QStringLiteral("Logos."),
        QStringLiteral("L\u0585g\u0585s"), QStringLiteral("Lo\u0581os"), QStringLiteral("L\u1D0Fgos"),
        QStringLiteral("L\u1040gos"), QStringLiteral("\u04C0ogos"),
        QStringLiteral("Logo\uABA5"), QStringLiteral("\uABAEogos")};   // Cherokee small letters
    for (const QString& name : lookalikes)
        CHECK(flagged(name, kOtherDid) == QStringLiteral("Logos"));
    CHECK_EQ(flagged(QStringLiteral("\u0410cme"), kOtherDid), QStringLiteral("Acme"));   // Cyrillic A
    // Case aside, for names with an i too, and capitals from other scripts.
    for (const QString& name : {QStringLiteral("NIMBUS"), QStringLiteral("nimbus"),
                                QStringLiteral("N\u0406MBUS"), QStringLiteral("\u039DIMBUS")})
        CHECK(flagged(name, kOtherDid) == QStringLiteral("Nimbus"));
    CHECK_EQ(flagged(QStringLiteral("Nirnbus"), kOtherDid), QStringLiteral("Nimbus"));   // rn for m
    CHECK_EQ(flagged(QStringLiteral("Nimble"), kOtherDid), QString());
    // i and l alike only where a capital I could be either: "Mall" is not "Mail".
    CHECK_EQ(flagged(QStringLiteral("Mail"), kOtherDid), QStringLiteral("Mail"));
    CHECK_EQ(flagged(QStringLiteral("Mall"), kOtherDid), QString());
}

// What a publisher calls itself is shown as text, and nothing in it can
// break the line, turn it around, or push what follows out of view.
TEST(aPublishersOwnTextIsMadeFitToShow)
{
    CHECK_EQ(shown(QStringLiteral("Acme\nTrusted by Logos")), QStringLiteral("Acme Trusted by Logos"));
    CHECK_EQ(shown(QStringLiteral("Acme\u202Esgol")), QStringLiteral("Acme sgol"));   // right-to-left override
    CHECK_EQ(shown(QStringLiteral("A\u200Bcme")), QStringLiteral("A cme"));           // zero width
    CHECK_EQ(shown(QStringLiteral("  Acme   Inc ")), QStringLiteral("Acme Inc"));
    // Invisible outside the BMP too, and those that are not format characters.
    CHECK_EQ(shown(QStringLiteral("Ac\U000E0067me")), QStringLiteral("Ac me"));   // tag character
    CHECK_EQ(shown(QStringLiteral("Acme\uFE0F")), QStringLiteral("Acme"));         // variation selector
    CHECK_EQ(shown(QStringLiteral("Ac\u034Fme")), QStringLiteral("Ac me"));        // combining grapheme joiner
    // Braille's blank is drawn as a space and folds like one, so a run of
    // them cannot push the rest of a name out of view.
    CHECK_EQ(shown(QStringLiteral("Logos") + QString(58, QChar(0x2800)) + QStringLiteral("Labs")), QStringLiteral("Logos Labs"));
    const QString longName = shown(QString(200, QLatin1Char('a')));
    CHECK_EQ(longName.size(), 64);
    CHECK(longName.endsWith(QChar(0x2026)));
    // A cut never splits a character in two.
    const QString emoji = QString(62, QLatin1Char('a')) + QStringLiteral("\U0001F600\U0001F600");
    CHECK(!shown(emoji).at(shown(emoji).size() - 2).isHighSurrogate());

    const QVariantMap s = summary({{QStringLiteral("app\n"), {}, Status::Untrusted, kOtherDid,
                                    QStringLiteral("<b>Acme</b>\u202E")}}, {});
    const QVariantMap publisher = s.value(QStringLiteral("publishers")).toList().first().toMap();
    CHECK_EQ(publisher.value(QStringLiteral("name")).toString(), QStringLiteral("<b>Acme</b>"));
    CHECK_EQ(publisher.value(QStringLiteral("packages")).toStringList(), QStringList{QStringLiteral("app")});
}

// An installed copy is judged by package_manager's record of it: the
// "Open with…" chooser says this of every installed provider.
TEST(anInstalledCopyIsJudgedByItsRecord)
{
    const QStringList trusted{logosDid()};
    const auto judged = [&trusted](const QVariantMap& record) {
        return fromInstalled(QStringLiteral("app"), QStringLiteral("1.0.0"), record, trusted);
    };
    CHECK_EQ(judged({{QStringLiteral("installType"), QStringLiteral("embedded")}}).status, Status::Bundled);
    CHECK_EQ(judged({{QStringLiteral("installType"), QStringLiteral("user")},
                     {QStringLiteral("signerDid"), logosDid()}}).status, Status::Trusted);
    CHECK_EQ(judged({{QStringLiteral("installType"), QStringLiteral("user")},
                     {QStringLiteral("signerDid"), kOtherDid}}).status, Status::Untrusted);
    CHECK_EQ(judged({{QStringLiteral("installType"), QStringLiteral("user")}}).status, Status::Unsigned);
    CHECK_EQ(judged({}).status, Status::Unknown);
    CHECK(judged({}).installed);

    // As the chooser shows it: Logos by name.
    const QVariantMap verdict = summary({judged({{QStringLiteral("installType"), QStringLiteral("user")},
                                                 {QStringLiteral("signerDid"), logosDid()}})}, {})
                                    .value(QStringLiteral("byName")).toMap()
                                    .value(QStringLiteral("app")).toMap();
    CHECK_EQ(verdict.value(QStringLiteral("status")).toString(), QStringLiteral("trusted"));
    CHECK_EQ(verdict.value(QStringLiteral("publisher")).toString(), QStringLiteral("Logos"));
}

// "Trust & install" is offered for who signed the package the dialog is
// about, and trusts nobody else: it used to trust every signer in the
// plan in one click. The other answer is a plain "Install" only when that
// settles everything that asks, otherwise "Install anyway".
TEST(whatTrustingWouldSettle)
{
    const auto flags = [](const QList<Package>& plan) {
        const QVariantMap s = summary(plan, {}, QStringLiteral("app"));
        return std::make_pair(s.value(QStringLiteral("canTrust")).toBool(),
                              s.value(QStringLiteral("trustCoversAll")).toBool());
    };
    const Package app{QStringLiteral("app"), {}, Status::Untrusted, kOtherDid, QStringLiteral("Other")};
    const Package sameKey{QStringLiteral("dep"), {}, Status::Untrusted, kOtherDid};
    const Package dep{QStringLiteral("dep"), {}, Status::Untrusted, kThirdDid, QStringLiteral("Third")};
    const Package lookalike{QStringLiteral("fake"), {}, Status::Untrusted, kThirdDid, QStringLiteral("Logos")};
    const Package loose{QStringLiteral("loose"), {}, Status::Unsigned};
    CHECK_EQ(flags({app, sameKey}), std::make_pair(true, true));
    CHECK_EQ(flags({app, dep}), std::make_pair(true, false));
    CHECK_EQ(flags({app, loose}), std::make_pair(true, false));
    CHECK_EQ(flags({app, {QStringLiteral("unchecked"), {}, Status::Unknown}}), std::make_pair(true, false));
    CHECK_EQ(flags({app, lookalike}), std::make_pair(true, false));
    // A dependency's publisher is never the one offered.
    CHECK_EQ(flags({{QStringLiteral("app"), {}, Status::Trusted, logosDid()}, dep}), std::make_pair(false, false));
    CHECK_EQ(flags({{QStringLiteral("app"), {}, Status::Unsigned}, dep}), std::make_pair(false, false));
    CHECK_EQ(flags({{QStringLiteral("app"), {}, Status::Untrusted, kThirdDid, QStringLiteral("Logos")}}), std::make_pair(false, false));
    CHECK_EQ(flags({loose}), std::make_pair(false, false));
    // Installed already, or only offered, a package asks nothing.
    Package installed = loose;
    installed.installed = true;
    CHECK_EQ(flags({app, installed}), std::make_pair(true, true));

    const QVariantMap s = summary({app, dep}, {}, QStringLiteral("app"));
    CHECK_EQ(s.value(QStringLiteral("trust")).toMap().value(QStringLiteral("did")).toString(), kOtherDid);
    CHECK_EQ(s.value(QStringLiteral("publishers")).toList().size(), 2);   // both still listed
    CHECK(summary({app}, {}).value(QStringLiteral("trust")).toMap().isEmpty());   // about nothing
}

// Logos's keys rotate. A keyring holding the key Logos used before still
// stands for Logos, and so for its new key, and what either signed is
// Logos's. A revoked key is still the user's yes to Logos but trusts
// nothing itself, and what it signed can't be installed.
TEST(aLogosKeyOfAnyAgeStandsForLogos)
{
    const QString oldKey = logosDid();
    const QVariantList holdsOld{QVariantMap{{QStringLiteral("name"), logosKeyName()},
                                            {QStringLiteral("did"), oldKey},
                                            {QStringLiteral("displayName"), logosName()}}};
    const LogosKeys rotated{{kThirdDid, oldKey}, {}};
    CHECK_EQ(trustedDids(holdsOld, rotated), (QStringList{kThirdDid, oldKey}));
    CHECK(trustedDids({}, rotated).isEmpty());   // Logos deleted
    const auto byName = [](const QVariantMap& s, const char* key) {
        return s.value(QStringLiteral("byName")).toMap().value(QStringLiteral("app")).toMap()
            .value(QLatin1String(key)).toString();
    };
    QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Trusted, kThirdDid}}, holdsOld,
                            QStringLiteral("app"), rotated);
    CHECK_EQ(byName(s, "publisher"), QStringLiteral("Logos"));
    s = summary({{QStringLiteral("app"), {}, Status::Untrusted, oldKey, QStringLiteral("Logos")}}, {},
                QStringLiteral("app"), rotated);
    CHECK_EQ(byName(s, "impersonates"), QString());
    CHECK_EQ(s.value(QStringLiteral("canTrust")).toBool(), true);

    const LogosKeys leaked{{kThirdDid}, {oldKey}};
    CHECK_EQ(trustedDids(holdsOld, leaked), QStringList{kThirdDid});
    s = summary({{QStringLiteral("app"), {}, Status::Untrusted, oldKey, QStringLiteral("Logos")}}, holdsOld,
                QStringLiteral("app"), leaked);
    CHECK_EQ(byName(s, "status"), QStringLiteral("invalid"));
    CHECK_EQ(s.value(QStringLiteral("blocked")).toBool(), true);
    CHECK_EQ(s.value(QStringLiteral("canTrust")).toBool(), false);
}

// Without a keyring to check them against, signers are neither trusted
// nor offered: a name a trusted publisher holds can't be told apart then.
TEST(withoutAKeyringNoSignerIsTrustedOrOffered)
{
    const QList<Package> judged = unchecked({{QStringLiteral("app"), {}, Status::Untrusted, kOtherDid,
                                              QStringLiteral("Acme")},
                                             {QStringLiteral("core"), {}, Status::Trusted, logosDid()},
                                             {QStringLiteral("loose"), {}, Status::Unsigned},
                                             {QStringLiteral("bad"), {}, Status::Invalid}});
    CHECK_EQ(judged[0].status, Status::Unknown);
    CHECK_EQ(judged[1].status, Status::Unknown);
    CHECK_EQ(judged[2].status, Status::Unsigned);
    CHECK_EQ(judged[3].status, Status::Invalid);
    const QVariantMap s = summary(judged, {}, QStringLiteral("app"));
    CHECK_EQ(s.value(QStringLiteral("canTrust")).toBool(), false);
    CHECK(s.value(QStringLiteral("publishers")).toList().isEmpty());
}

TEST(anythingInvalidBlocksTheInstall)
{
    const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Unsigned},
                                   {QStringLiteral("bad"), {}, Status::Invalid}}, {});
    CHECK_EQ(s.value(QStringLiteral("needsPrompt")).toBool(), true);
    CHECK_EQ(s.value(QStringLiteral("blocked")).toBool(), true);
}

TEST(aPublisherIsTrustedUnderAStableSafeName)
{
    CHECK_EQ(keyNameFor(logosDid()), logosKeyName());
    const QString name = keyNameFor(kOtherDid);
    CHECK_EQ(keyNameFor(kOtherDid), name);
    CHECK(name != keyNameFor(kThirdDid));
    CHECK(!name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
            && !name.contains(QStringLiteral("..")));
}

// The list Basecamp ships has a key to sign with now, every key spelled
// as liblgx spells it, and none both good and revoked.
TEST(theShippedLogosKeysAreSound)
{
    const LogosKeys keys = logosKeys();
    CHECK(!keys.dids.isEmpty());
    CHECK_EQ(logosDid(), keys.dids.first());
    for (const QString& did : keys.dids + keys.revoked)
        CHECK(isCanonicalDid(did));
    for (const QString& did : keys.revoked)
        CHECK(!keys.dids.contains(did));
}

// Logos's name is taken even when Logos has no key left to sign with:
// with every key revoked, any key used to be able to take it.
TEST(logosNameIsTakenEvenWithNoKey)
{
    const LogosKeys noneLeft{{}, {logosDid()}};
    const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Untrusted, kOtherDid,
                                    QStringLiteral("Logos")}}, {}, QStringLiteral("app"), noneLeft);
    CHECK_EQ(s.value(QStringLiteral("publishers")).toList().first().toMap()
                 .value(QStringLiteral("impersonates")).toString(), QStringLiteral("Logos"));
    CHECK_EQ(s.value(QStringLiteral("canTrust")).toBool(), false);
}

// A key that isn't Logos's never reads as Logos, whoever put the name in
// the keyring (another module, or the lgx CLI): it used to say "Signed by
// Logos".
TEST(aKeyThatIsNotLogossNeverReadsAsLogos)
{
    CHECK_EQ(publisherName(QStringLiteral("Logos"), kOtherDid), QString());
    CHECK_EQ(publisherName(QStringLiteral("\uFF2C\uFF4F\uFF47\uFF4F\uFF53"), kOtherDid), QString());   // full width
    CHECK_EQ(publisherName(QStringLiteral("Logos Labs"), kOtherDid), QString());   // a word of it
    CHECK_EQ(publisherName(QStringLiteral("Acme Labs"), kOtherDid), QStringLiteral("Acme Labs"));
    CHECK_EQ(publisherName(QStringLiteral("anything"), logosDid()), QStringLiteral("Logos"));
    const QVariantList keys{QVariantMap{{QStringLiteral("did"), kOtherDid},
                                        {QStringLiteral("displayName"), QStringLiteral("Logos")}}};
    const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Trusted, kOtherDid}}, keys,
                                  QStringLiteral("app"));
    CHECK_EQ(s.value(QStringLiteral("byName")).toMap().value(QStringLiteral("app")).toMap()
                 .value(QStringLiteral("publisher")).toString(), QStringLiteral("key ") + fingerprint(kOtherDid));
}

// "Trust & install" stores the publisher as the package the dialog is
// about names it: dependencies come first in a plan, and it used to take
// a dependency's name for the same key.
TEST(trustGoesByTheNameOfWhatTheDialogIsAbout)
{
    const QVariantMap s = summary({{QStringLiteral("dep"), {}, Status::Untrusted, kOtherDid,
                                    QStringLiteral("Other Deps Ltd")},
                                   {QStringLiteral("app"), {}, Status::Untrusted, kOtherDid,
                                    QStringLiteral("Other"), QStringLiteral("https://other.example")}},
                                  {}, QStringLiteral("app"));
    const QVariantMap trust = s.value(QStringLiteral("trust")).toMap();
    CHECK_EQ(trust.value(QStringLiteral("name")).toString(), QStringLiteral("Other"));
    CHECK_EQ(trust.value(QStringLiteral("url")).toString(), QStringLiteral("https://other.example"));
}

// Trusting Logos trusts every key it signs with, so it settles them all:
// it used to say "anyway" for one signed with another of Logos's keys.
TEST(trustingLogosSettlesEveryLogosKey)
{
    const LogosKeys rotated{{kThirdDid, logosDid()}, {}};
    const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Untrusted, logosDid(), QStringLiteral("Logos")},
                                   {QStringLiteral("dep"), {}, Status::Untrusted, kThirdDid, QStringLiteral("Logos")}},
                                  {}, QStringLiteral("app"), rotated);
    CHECK_EQ(s.value(QStringLiteral("canTrust")).toBool(), true);
    CHECK_EQ(s.value(QStringLiteral("trustCoversAll")).toBool(), true);
    CHECK_EQ(publisherKeys(logosDid(), rotated), (QStringList{kThirdDid, logosDid()}));
    CHECK_EQ(publisherKeys(kOtherDid, rotated), QStringList{kOtherDid});
}

// A record is found by its exact name before one differing only in case,
// which a case-insensitive file system finds too: the first
// case-insensitive match used to stand for an exact one listed after it.
TEST(anInstalledRecordIsFoundByItsExactNameFirst)
{
    const QVariantList installed{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Wallet_UI")},
                    {QStringLiteral("signerDid"), kOtherDid}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("wallet_ui")},
                    {QStringLiteral("signerDid"), kThirdDid}}};
    CHECK_EQ(installedRecordOf(installed, QStringLiteral("wallet_ui"))
                 .value(QStringLiteral("signerDid")).toString(), kThirdDid);
    CHECK_EQ(installedRecordOf(installed, QStringLiteral("WALLET_ui"))
                 .value(QStringLiteral("signerDid")).toString(), kOtherDid);
    CHECK(installedRecordOf(installed, QStringLiteral("chat")).isEmpty());
}

// Two files listed as one unsigned release: which comes isn't ours to
// guess. Signed, the signature pins it whichever file comes.
TEST(twoFilesListedAsOneUnsignedReleaseAreUnknown)
{
    QVariantMap one = release(QStringLiteral("loose"), QStringLiteral("1.0.0"), QStringLiteral("l"));
    one[QStringLiteral("sha256")] = QStringLiteral("one");
    QVariantMap two = one;
    two[QStringLiteral("sha256")] = QStringLiteral("two");
    CHECK_EQ(fromCatalog(QStringLiteral("loose"), QStringLiteral("1.0.0"), QStringLiteral("l"), {one, two}, {})
                 .status, Status::Unknown);
    CHECK_EQ(fromCatalog(QStringLiteral("loose"), QStringLiteral("1.0.0"), QStringLiteral("l"), {one, one}, {})
                 .status, Status::Unsigned);
    QVariantMap signedOne = release(QStringLiteral("app"), QStringLiteral("2.0.0"), QStringLiteral("a"), logosDid());
    signedOne[QStringLiteral("sha256")] = QStringLiteral("one");
    QVariantMap signedTwo = signedOne;
    signedTwo[QStringLiteral("sha256")] = QStringLiteral("two");
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("2.0.0"), QStringLiteral("a"),
                         {signedOne, signedTwo}, {logosDid()}).status, Status::Trusted);
}

// A release installs under its own manifest's name, which the download is
// held to: one listed as "cool_backend" but installing as
// capability_module used to pass every check, and so did one with no
// manifest at all.
TEST(aReleaseInstallsUnderTheNameItIsListedAs)
{
    CHECK_EQ(fromCatalog(QStringLiteral("cool_backend"), QStringLiteral("1.0.0"), QStringLiteral("a"),
                         {release(QStringLiteral("capability_module"), QStringLiteral("1.0.0"),
                                  QStringLiteral("a"), kOtherDid)},
                         {kOtherDid}).status, Status::Invalid);
    QVariantMap nameless = release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("a"), kOtherDid);
    nameless.remove(QStringLiteral("manifest"));
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("a"), {nameless},
                         {kOtherDid}).status, Status::Invalid);
    CHECK_EQ(fromCatalog(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("a"),
                         {release(QStringLiteral("app"), QStringLiteral("1.0.0"), QStringLiteral("a"), kOtherDid)},
                         {kOtherDid}).status, Status::Trusted);
}

// An address claiming Logos isn't shown for another key; a path may
// mention anything.
TEST(anAddressClaimingLogosIsLogoss)
{
    CHECK_EQ(publisherUrl(QStringLiteral("https://logos.co"), kOtherDid), QString());
    CHECK_EQ(publisherUrl(QStringLiteral("logos.co"), kOtherDid), QString());
    CHECK_EQ(publisherUrl(QStringLiteral("https://logos-official.example"), kOtherDid), QString());
    CHECK_EQ(publisherUrl(QStringLiteral("https://github.com/acme/logos-tools"), kOtherDid), QStringLiteral("https://github.com/acme/logos-tools"));
    CHECK_EQ(publisherUrl(QStringLiteral("https://acme.example"), kOtherDid), QStringLiteral("https://acme.example"));
    CHECK_EQ(publisherUrl(QStringLiteral("https://elsewhere.example"), logosDid()), logosUrl());
}

// A word of a name that starts reading as Logos makes it Logos's, not
// just the whole name: "Logos Official" used to be offered for trust.
TEST(aNameWithLogosInItIsLogoss)
{
    const auto flagged = [](const QString& claimed, const QString& did) {
        const QVariantMap s = summary({{QStringLiteral("app"), {}, Status::Untrusted, did, claimed}}, {},
                                      QStringLiteral("app"));
        return s.value(QStringLiteral("byName")).toMap().value(QStringLiteral("app")).toMap()
            .value(QStringLiteral("impersonates")).toString();
    };
    for (const QString& name : {QStringLiteral("Logos Official"), QStringLiteral("Official Logos"),
                                QStringLiteral("LogosHub"), QStringLiteral("My L o g o s"),
                                QStringLiteral("the-logos-team"), QStringLiteral("L0gos Labs")})
        CHECK(flagged(name, kOtherDid) == QStringLiteral("Logos"));
    for (const QString& name : {QStringLiteral("Neologos"), QStringLiteral("Apologos Labs"),
                                QStringLiteral("Other")})
        CHECK(flagged(name, kOtherDid).isEmpty());
    CHECK_EQ(flagged(QStringLiteral("Logos Official"), logosDid()), QString());   // Logos's own key
    CHECK_EQ(publisherName(QStringLiteral("Logos Official"), kOtherDid), QString());
}

TEST(theTrustedDidsComeFromTheKeyring)
{
    CHECK_EQ(trustedDids({QVariantMap{{QStringLiteral("name"), QStringLiteral("logos")},
                                      {QStringLiteral("did"), logosDid()}},
                          QVariantMap{{QStringLiteral("name"), QStringLiteral("again")},
                                      {QStringLiteral("did"), logosDid()}},
                          QVariantMap{{QStringLiteral("name"), QStringLiteral("empty")}}}), QStringList{logosDid()});
}

CHECK_MAIN()
