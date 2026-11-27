// Who signed each package an app brings (PackageTrust.h): what installs is
// judged by the release picked for it, what stays on disk by the copy there.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/PackageModel.h>
#include <logos/installer/PackageTrust.h>
#include <logos/installer/Trust.h>

using namespace logos::installer;
using namespace fixtures;

namespace {

// A made-up Ed25519 key, spelled the way liblgx spells it.
const QString kAcmeDid = QStringLiteral(
    "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6IkFRSURCQVVHQndnSkNnc01EUTRQRUJFU0V4"
    "UVZGaGNZR1JvYkhCMGVIeUEifQ");

// `row` with its version `index` signed by `did` as `signer`.
QVariantMap signedAt(QVariantMap row, int index, const QString& did, const QString& signer)
{
    QVariantList versions = row.value("versions").toList();
    QVariantMap version = versions.at(index).toMap();
    version.insert("signature", QVariantMap{{"did", did}, {"signer", QVariantMap{{"name", signer}}}});
    versions[index] = version;
    row.insert("versions", versions);
    return row;
}

const QVariantList kLogosTrusted{
    QVariantMap{{"did", logos::trust::logosDid()}, {"displayName", "Logos"}}};

struct Wallet {
    PackageModel model;
    Wallet()
    {
        const QVariantMap wallet = signedAt(
            catalogRow("repo1", "wallet_ui", "2.0.0", "W", {"signer", "token_list"}), 0,
            logos::trust::logosDid(), "Logos");
        // Newest by Acme, the one before by Logos.
        QVariantMap signer = signedAt(withOlderVersion(
            catalogRow("repo1", "signer", "1.1.0", "S2", {}, {}, "core"), "1.0.0", "S1"), 0, kAcmeDid, "Acme");
        signer = signedAt(signer, 1, logos::trust::logosDid(), "Logos");
        model.setCatalog({wallet, signer, catalogRow("repo1", "token_list", "0.1.0", "T", {}, {}, "core")});
        model.setInstalled({installed("token_list", "0.1.0", "T")});
    }
    QVariantMap verdicts(const QVariantMap& versions = {}, bool keysRead = true)
    {
        const int row = model.rowOf("wallet_ui", "repo1");
        return trustOfBrought(model, model.packagesBroughtBy(row, {}, versions), kLogosTrusted, keysRead,
                              "wallet_ui");
    }
};

QString statusOf(const QVariantMap& verdicts, const QString& name)
{
    return verdicts.value(name).toMap().value("status").toString();
}

}  // namespace

TEST(each_package_is_judged_as_it_would_install) {
    Wallet w;
    const QVariantMap v = w.verdicts();
    CHECK_EQ(statusOf(v, "wallet_ui"), QStringLiteral("trusted"));
    CHECK_EQ(v.value("wallet_ui").toMap().value("publisher").toString(), QStringLiteral("Logos"));
    CHECK_EQ(statusOf(v, "signer"), QStringLiteral("untrusted"));
    CHECK_EQ(v.value("signer").toMap().value("publisher").toString(), QStringLiteral("Acme"));
    // On disk as it is: its copy, which nobody signed, and it asks nothing.
    CHECK_EQ(statusOf(v, "token_list"), QStringLiteral("unsigned"));
    CHECK(v.value("token_list").toMap().value("installed").toBool());
}

// The version picked is the release judged: an older one Logos signed.
TEST(a_version_picked_is_judged_by_its_own_signer) {
    Wallet w;
    const QVariantMap v = w.verdicts({{"signer", "1.0.0"}});
    CHECK_EQ(statusOf(v, "signer"), QStringLiteral("trusted"));
    CHECK_EQ(v.value("signer").toMap().value("publisher").toString(), QStringLiteral("Logos"));
}

// A keyring that can't be read trusts nobody: whoever signed it, Unknown.
TEST(without_the_keyring_no_signer_is_trusted) {
    Wallet w;
    const QVariantMap v = w.verdicts({}, false);
    CHECK_EQ(statusOf(v, "wallet_ui"), QStringLiteral("unknown"));
    CHECK_EQ(statusOf(v, "signer"), QStringLiteral("unknown"));
    CHECK_EQ(statusOf(v, "token_list"), QStringLiteral("unsigned"));
}

// A release keeps its signature and digest through the model.
TEST(a_release_keeps_its_signature_and_digest) {
    PackageModel model;
    QVariantMap row = signedAt(catalogRow("repo1", "app", "1.0.0", "A"), 0, kAcmeDid, "Acme");
    QVariantList versions = row.value("versions").toList();
    QVariantMap version = versions.first().toMap();
    version.insert("sha256", "f00d");
    versions[0] = version;
    row.insert("versions", versions);
    model.setCatalog({row});
    const QVariantMap entry =
        model.rowAt(model.rowOf("app")).value("availableVersions").toList().first().toMap();
    CHECK_EQ(entry.value("signature").toMap().value("did").toString(), kAcmeDid);
    CHECK_EQ(entry.value("sha256").toString(), QStringLiteral("f00d"));
}

CHECK_MAIN()
