// Holding an install to what Basecamp approved. From logos-package-manager-
// ui's tests/test_install_sequence.cpp and logos-basecamp's
// package_install_decision_test.cpp and install_plugin_success_test.cpp.

#include "check.h"

#include <logos/installer/Approval.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>

using namespace logos::installer;

namespace {

QVariantMap approvedRow(const QString& name, const QString& version, const QString& rootHash)
{
    return {{"name", name}, {"version", version}, {"rootHash", rootHash},
            {"repositoryUrl", "https://repo.example"}};
}

QVariantMap downloaded(const QString& name, const QString& version, const QString& rootHash)
{
    return {{"name", name}, {"version", version}, {"rootHash", rootHash},
            {"repositoryUrl", "https://repo.example"}, {"path", "/tmp/" + name + ".lgx"}};
}

QVariantMap withKey(QVariantMap m, const char* key, const QVariant& value)
{
    m.insert(key, value);
    return m;
}

}  // namespace

// ── What an approval approves ────────────────────────────────────────────

TEST(the_approval_says_where_the_package_comes_from) {
    const QVariantMap file{{"file", QVariantMap{{"rootHash", "h1"}, {"signer", ""}}}};
    const QVariantMap plan{{"plan", QVariantList{approvedRow("app", "2.0.0", "a")}}};
    CHECK(sourceOf(file, true) == ApprovalSource::File);
    CHECK(sourceOf(file, false) == ApprovalSource::Refused);
    CHECK(sourceOf(plan, true) == ApprovalSource::Catalog);
    CHECK(sourceOf(plan, false) == ApprovalSource::Catalog);
}

TEST(an_approval_naming_nothing_installs_nothing) {
    CHECK(sourceOf({}, true) == ApprovalSource::Unapproved);
    const QVariantMap optionalOnly{{"optionalPackages", QVariantList{QVariantMap{{"name", "extra"}}}}};
    CHECK(sourceOf(optionalOnly, false) == ApprovalSource::Unapproved);
}

TEST(an_approval_is_refused_unless_it_holds_what_is_asked) {
    const QVariantMap plan{{"plan", QVariantList{approvedRow("app", "2.0.0", "a")}}};
    const QVariantMap file{{"file", QVariantMap{{"rootHash", "h1"}, {"signer", ""}}}};
    CHECK_EQ(refusal(plan, false, "app"), QString());
    CHECK_EQ(refusal(file, true, "app"), QString());
    CHECK(refusal({}, true, "app").contains("did not approve"));
    CHECK(refusal(file, false, "app").contains("no longer the one waiting"));
    CHECK(refusal(plan, false, "other").contains("does not include other"));
    CHECK(!refusal(QVariantMap{{"plan", QVariantList{}}}, false, "app").isEmpty());
}

// Anything less would let the download choose.
TEST(an_approval_must_name_each_release_exactly) {
    const QVariantList plan{approvedRow("dep", "1.0.0", "d"), approvedRow("app", "2.0.0", "a")};
    CHECK_EQ(malformedPlan(plan, "app"), QString());
    for (const char* key : {"version", "rootHash", "repositoryUrl"}) {
        QVariantMap partial = approvedRow("dep", "1.0.0", "d");
        partial.remove(key);
        CHECK(malformedPlan({partial, approvedRow("app", "2.0.0", "a")}, "app").contains("dep"));
    }
    CHECK(malformedPlan({approvedRow("dep", "1.0.0", "d")}, "app").contains("app"));
    CHECK(!malformedPlan({}, "app").isEmpty());
}

// Queued behind an install that brought `dep`: not fetched again. The
// package an upgrade asks for always is, the same version again too.
TEST(what_is_installed_already_is_not_fetched_again) {
    const QVariantList plan{approvedRow("dep", "1.0.0", "d"), approvedRow("lib", "2.0.0", "l"),
                            approvedRow("app", "2.0.0", "a")};
    const QVariantList installed{
        QVariantMap{{"name", "dep"}, {"version", "1.0.0"}, {"hashes", QVariantMap{{"root", "d"}}}},
        QVariantMap{{"name", "lib"}, {"version", "1.0.0"}, {"hashes", QVariantMap{{"root", "l1"}}}},
        QVariantMap{{"name", "app"}, {"version", "2.0.0"}, {"hashes", QVariantMap{{"root", "a"}}}}};
    const QVariantList left = stillToInstall(plan, installed, "app");
    CHECK_EQ(left.size(), 2);
    CHECK_EQ(left.value(0).toMap().value("name").toString(), QStringLiteral("lib"));
    CHECK_EQ(left.value(1).toMap().value("name").toString(), QStringLiteral("app"));
    const QVariantList byModule{QVariantMap{{"name", "Dep"}, {"moduleName", "dep"}, {"version", "1.0.0"},
                                            {"hashes", QVariantMap{{"root", "d"}}}}};
    CHECK_EQ(stillToInstall(plan, byModule, "app").size(), 2);
    // Nothing kept: the app as it is on disk is not fetched again.
    CHECK_EQ(stillToInstall(plan, installed, QString()).size(), 1);
}

// ── What arrived ─────────────────────────────────────────────────────────

TEST(an_approved_download_may_install) {
    const QVariantList plan{approvedRow("dep", "1.0.0", "d"), approvedRow("app", "2.0.0", "a")};
    CHECK_EQ(notAsApproved({downloaded("dep", "1.0.0", "d"), downloaded("app", "2.0.0", "a")}, plan, plan),
             QString());
}

// The catalog changed after the dialog: a new dependency, a rebuild of the
// same version, the same build from another repository.
TEST(a_download_outside_the_plan_installs_nothing) {
    const QVariantList plan{approvedRow("app", "2.0.0", "a")};
    for (const QVariantMap& extra : {downloaded("rln", "0.1.0", "r"), downloaded("app", "2.0.0", "other"),
                                     withKey(downloaded("app", "2.0.0", "a"), "repositoryUrl", "https://other")}) {
        const QString why = notAsApproved({extra, downloaded("app", "2.0.0", "a")}, plan, plan);
        CHECK(why.contains(extra.value("name").toString() + " v" + extra.value("version").toString()));
    }
    CHECK(!notAsApproved({downloaded("app", "2.0.0", "a")}, {}, {}).isEmpty());
}

// All or nothing: a required package that failed or never came stops the
// install. An optional one may fail or be gone.
TEST(an_install_short_of_a_required_package_installs_nothing) {
    const QVariantMap opt = withKey(approvedRow("opt", "1.1.0", "o"), "optional", true);
    const QVariantList requested{approvedRow("dep", "1.0.0", "d"), opt, approvedRow("app", "2.0.0", "a")};
    const QVariantMap dep = downloaded("dep", "1.0.0", "d");
    const QVariantMap app = downloaded("app", "2.0.0", "a");
    CHECK_EQ(notAsApproved({dep, app}, requested, requested), QString());
    const QVariantMap appFailed{{"name", "app"}, {"error", "timed out"}};
    CHECK_EQ(notAsApproved({dep, appFailed}, requested, requested), QStringLiteral("app: timed out"));
    CHECK(notAsApproved({dep}, requested, requested).contains("app v2.0.0"));
    const QVariantMap optFailed{{"name", "opt"}, {"error", "download failed for 'opt'"}};
    CHECK_EQ(notAsApproved({dep, optFailed, app}, requested, requested), QString());
}

// The download picks a catalog entry by root hash alone, which covers neither
// the manifest nor who signed it.
TEST(a_download_must_be_signed_as_approved) {
    const QVariantMap signedRow = withKey(approvedRow("app", "2.0.0", "a"), "signer", "did:jwk:a");
    const QVariantList plan{signedRow, approvedRow("dep", "1.0.0", "d")};
    const QVariantMap app = downloaded("app", "2.0.0", "a");
    const QVariantMap dep = downloaded("dep", "1.0.0", "d");
    const auto verified = [](bool isSigned, const QString& did) {
        return QVariantMap{{"isSigned", isSigned}, {"signatureValid", isSigned}, {"packageValid", true},
                           {"signerDid", did}};
    };
    CHECK_EQ(signerChanged(verified(true, "did:jwk:a"), plan, app), QString());
    CHECK(signerChanged(verified(true, "did:jwk:b"), plan, app).contains("app v2.0.0"));
    CHECK(!signerChanged(verified(false, ""), plan, app).isEmpty());
    CHECK(!signerChanged(withKey(verified(true, "did:jwk:a"), "signatureValid", false), plan, app).isEmpty());
    CHECK_EQ(signerChanged(verified(false, ""), plan, dep), QString());
    CHECK(!signerChanged(verified(true, "did:jwk:b"), plan, dep).isEmpty());
    CHECK(!signerChanged({}, plan, dep).isEmpty());
}

// The copy that installs is the release approved: its manifest's name and
// version, and its content's root hash.
TEST(a_copy_must_be_the_release_approved) {
    const QVariantMap row = approvedRow("app", "2.0.0", "a");
    const QVariantMap same{{"name", "app"}, {"version", "2.0.0"}, {"rootHash", "a"}};
    CHECK_EQ(releaseChanged(same, row), QString());
    CHECK(releaseChanged(withKey(same, "rootHash", "b"), row).contains("app v2.0.0"));
    CHECK(!releaseChanged(withKey(same, "version", "2.0.1"), row).isEmpty());
    CHECK(!releaseChanged(withKey(same, "name", "capability_module"), row).isEmpty());
    CHECK(!releaseChanged(withKey(same, "error", "Failed to load LGX package"), row).isEmpty());
    CHECK(!releaseChanged(same, withKey(row, "rootHash", "")).isEmpty());
}

// ── A local file ─────────────────────────────────────────────────────────

TEST(a_file_is_identified_by_the_sha256_of_all_of_it) {
    QTemporaryFile file;
    CHECK(file.open());
    file.write("abc");
    file.close();
    CHECK_EQ(sha256Of(file.fileName()),
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Of(file.fileName() + ".gone"), QString());
}

TEST(a_local_file_must_still_be_the_one_approved) {
    const QVariantMap approved{{"rootHash", "h1"}, {"signer", "did:jwk:a"}, {"sha256", "f1"}};
    const QVariantMap same{{"name", "app"}, {"version", "1.0.0"}, {"rootHash", "h1"},
                           {"signatureStatus", "signed"}, {"signerDid", "did:jwk:a"}};
    const auto changed = [](const QVariantMap& inspected, const QVariantMap& approval,
                            const QString& sha256 = QStringLiteral("f1")) {
        return !fileChanged(inspected, approval, "app", "1.0.0", sha256).isEmpty();
    };
    CHECK(!changed(same, approved));
    CHECK(changed(withKey(same, "rootHash", "h2"), approved));
    CHECK(changed(withKey(same, "signerDid", "did:jwk:b"), approved));
    CHECK(changed(withKey(same, "signatureStatus", "unsigned"), approved));
    CHECK(changed(withKey(same, "signatureStatus", "invalid"), approved));
    CHECK(changed(withKey(same, "error", "Failed to load LGX package"), approved));
    CHECK(changed(withKey(same, "name", "capability_module"), approved));
    CHECK(changed(withKey(same, "version", "9.9.9"), approved));
    const QVariantMap unsignedApproval{{"rootHash", "h1"}, {"signer", ""}, {"sha256", "f1"}};
    QVariantMap plain = withKey(same, "signatureStatus", "unsigned");
    plain.remove("signerDid");
    CHECK(!changed(plain, unsignedApproval));
    CHECK(changed(same, unsignedApproval));
    // The digest of the whole file, which an approval must carry.
    CHECK(changed(same, approved, "f2"));
    QVariantMap noDigest = approved;
    noDigest.remove("sha256");
    CHECK(changed(same, noDigest));
    CHECK(changed(same, noDigest, QString()));
    CHECK(changed(same, QVariantMap{{"signer", "did:jwk:a"}, {"sha256", "f1"}}));
}

// ── Installing ───────────────────────────────────────────────────────────

TEST(downloads_are_installed_from_private_copies) {
    QTemporaryDir from, to;
    QFile file(QDir(from.path()).filePath("app.lgx"));
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("package");
    file.close();
    QString error;
    const QVariantList copies = intoPrivateCopies(
        {QVariantMap{{"name", "app"}, {"path", file.fileName()}}, QVariantMap{{"name", "failed"}}},
        to.path(), &error);
    CHECK_EQ(error, QString());
    CHECK_EQ(copies.size(), 2);
    const QString copy = copies.value(0).toMap().value("path").toString();
    CHECK(copy.startsWith(to.path()));
    CHECK(QFile::exists(copy));
    CHECK(!copies.value(1).toMap().contains("path"));

    CHECK(intoPrivateCopies({QVariantMap{{"name", "gone"}, {"path", from.path() + "/gone.lgx"}}},
                            to.path(), &error).isEmpty());
    CHECK(error.contains("gone"));
}

// A fresh scan decides: the user's copy goes first, an embedded one stays.
TEST(only_the_users_copy_is_removed_before_its_replacement) {
    const auto record = [](const QString& name, const QString& type, const QString& module = {}) {
        QVariantMap r{{"name", name}, {"installType", type}};
        if (!module.isEmpty()) r.insert("moduleName", module);
        return r;
    };
    CHECK(!removeBeforeInstall({record("chat_ui", "embedded")}, "chat_ui"));
    CHECK(removeBeforeInstall({record("chat_ui", "user")}, "chat_ui"));
    CHECK(removeBeforeInstall({record("Chat UI", "user", "chat_ui")}, "chat_ui"));
    CHECK(!removeBeforeInstall({record("other", "user")}, "chat_ui"));
}

// Every real reply carries `path`, empty for a QML-only package; a reply
// without it isn't an answer.
TEST(an_install_reply_says_whether_it_installed) {
    CHECK(pluginInstalled({{"name", "wallet_ui"}, {"path", "/m/wallet_ui/wallet_ui.so"}}));
    CHECK(pluginInstalled({{"name", "soulseek_ui"}, {"path", ""}}));
    CHECK(pluginInstalled({{"name", "x"}, {"path", "/p"}, {"error", ""}}));
    CHECK(!pluginInstalled({{"name", "soulseek"}, {"path", ""}, {"error", "no variant for darwin-arm64"}}));
    CHECK(!pluginInstalled({}));
}

TEST(an_install_is_checked_on_disk) {
    const auto record = [](const QString& name, const QString& version, const QString& root,
                           const QString& module = {}) {
        QVariantMap r{{"name", name}, {"version", version}, {"hashes", QVariantMap{{"root", root}}}};
        if (!module.isEmpty()) r.insert("moduleName", module);
        return r;
    };
    const QVariantList installed{record("app", "2.0.0", "a2"), record("Lib", "1.0.0", "l1", "lib")};
    CHECK_EQ(notOnDisk(installed, "app", "2.0.0", "a2"), QString());
    CHECK_EQ(notOnDisk(installed, "lib", "1.0.0", "l1"), QString());
    CHECK(notOnDisk(installed, "app", "2.1.0", "a3").contains("v2.0.0 is still on disk"));
    CHECK(notOnDisk(installed, "gone", "1.0.0", "g").contains("gone is not installed"));
    // The same version, another build: a reinstall that didn't land.
    CHECK(notOnDisk(installed, "app", "2.0.0", "other").contains("not the release you approved"));
    CHECK(!notOnDisk(installed, "app", QString(), "a2").isEmpty());
    CHECK(!notOnDisk(installed, "app", "2.0.0", QString()).isEmpty());
    CHECK(!notOnDisk({record("app", "", "a2")}, "app", "2.0.0", "a2").isEmpty());
    CHECK(!notOnDisk({QVariantMap{{"name", "app"}, {"version", "2.0.0"}}}, "app", "2.0.0", "a2").isEmpty());
    // Basecamp's own copy of the same release is not one an install wrote.
    QVariantMap embedded = record("app", "2.0.0", "a2");
    embedded.insert("installType", "embedded");
    CHECK(notOnDisk({embedded}, "app", "2.0.0", "a2").contains("app is not installed"));
    // An embedded copy beside the one installed.
    CHECK_EQ(notOnDisk({record("app", "1.0.0", "e1"), record("app", "2.0.0", "a2")}, "app", "2.0.0", "a2"),
             QString());
}

TEST(a_replacement_says_which_way_it_goes) {
    CHECK_EQ(upgradeMode("2.0.0", "1.0.0"), 0);
    CHECK_EQ(upgradeMode("1.0.0", "2.0.0"), 1);
    CHECK_EQ(upgradeMode("1.0.0", "1.0.0"), 2);
    CHECK_EQ(upgradeMode("1.0.0", "1.0.0-rc.1"), 0);
}

CHECK_MAIN()
