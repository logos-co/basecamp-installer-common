// Installing what Basecamp approves, and nothing else, with fake package
// modules and a fake Basecamp. The fake's package files hold what they are
// ("name|version|rootHash|signer"), so what inspect and verify read follows
// the file's content, as package_manager's do.

#include "check.h"
#include "fixtures.h"

#include <logos/installer/Approval.h>
#include <logos/installer/Consent.h>
#include <logos/installer/InstallModules.h>
#include <logos/installer/InstallProgress.h>
#include <logos/installer/Installer.h>
#include <logos/installer/PackageModel.h>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <tuple>

using namespace logos::installer;

namespace {

const QString kRepo = QStringLiteral("https://repo.example");

QVariantMap release(const QString& name, const QString& version, const QString& rootHash,
                    const QString& signer = {}, bool optional = false)
{
    QVariantMap r{{"name", name}, {"version", version}, {"rootHash", rootHash}, {"repositoryUrl", kRepo}};
    if (!signer.isEmpty()) r.insert("signer", signer);
    if (optional) r.insert("optional", true);
    return r;
}

QVariantMap planOf(const QVariantList& rows)
{
    return {{"plan", rows}};
}

struct FakeModules : InstallModules {
    QTemporaryDir files;
    QStringList calls;
    // What the downloader serves, keyed "name@version".
    QHash<QString, QString> served;          // -> "name|version|rootHash|signer"
    QHash<QString, QString> downloadErrors;  // name -> error
    QHash<QString, QVariantMap> installed;   // name -> record
    QStringList installFails, uninstallFails;
    QString installCallError;
    bool installedAnyway = false;            // a call that failed, but the files landed
    int readFails = 0;                       // the next reads of the installed packages that fail
    int readFailsAfterInstall = 0;           // readFails, once an install was asked for
    QSet<int> failingReads;                  // reads that fail, counted from 1
    int readCount = 0;
    QHash<QString, QString> diskHashOverride;
    // What getInstalledPackages reports after an install, when not what it wrote.
    QHash<QString, QString> diskVersionOverride;
    QStringList installedPaths;

    void serve(const QString& name, const QString& version, const QString& rootHash,
               const QString& signer = {})
    {
        served.insert(name + "@" + version, QStringList{name, version, rootHash, signer}.join('|'));
    }
    QString writeFile(const QString& fileName, const QString& content)
    {
        const QString path = QDir(files.path()).filePath(fileName);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content.toUtf8());
        return path;
    }
    static QStringList read(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll()).split('|');
    }
    void addInstalled(const QString& name, const QString& version, const QString& rootHash,
                      const QString& type = QStringLiteral("user"))
    {
        installed.insert(name, {{"name", name}, {"version", version}, {"installType", type},
                                {"hashes", QVariantMap{{"root", rootHash}}}});
    }

    void downloadPinned(const QString& repositoryUrl, const QString& name, const QString& version,
                        const QString& rootHash, std::function<void(const QVariantMap&)> done) override
    {
        calls << "download " + name;
        if (downloadErrors.contains(name)) {
            done({{"name", name}, {"error", downloadErrors.value(name)}});
            return;
        }
        const QString content = served.value(name + "@" + version);
        if (content.isEmpty()) {
            done({{"name", name}, {"error", "no such release"}});
            return;
        }
        const QStringList parts = content.split('|');
        // The reply says what the catalog entry says; the file holds what it is.
        done({{"name", name}, {"version", version}, {"rootHash", rootHash == parts.value(2) ? rootHash : parts.value(2)},
              {"repositoryUrl", repositoryUrl}, {"source", "https://repo.example/" + name + ".lgx"},
              {"path", writeFile(name + "-" + version + ".lgx", content)}});
    }
    void verifyPackage(const QString& path, std::function<void(const QVariantMap&)> done) override
    {
        const QStringList p = read(path);
        calls << "verify " + p.value(0);
        done({{"packageValid", p.size() == 4}, {"isSigned", !p.value(3).isEmpty()},
              {"signatureValid", !p.value(3).isEmpty()}, {"signerDid", p.value(3)}});
    }
    void inspectPackage(const QString& path, std::function<void(const QVariantMap&)> done) override
    {
        const QStringList p = read(path);
        calls << "inspect " + p.value(0);
        if (p.size() != 4) {
            done({{"error", "Failed to load LGX package"}});
            return;
        }
        done({{"name", p.value(0)}, {"version", p.value(1)}, {"rootHash", p.value(2)},
              {"signatureStatus", p.value(3).isEmpty() ? "unsigned" : "signed"}, {"signerDid", p.value(3)},
              {"isAlreadyInstalled", installed.contains(p.value(0))},
              {"installedVersion", installed.value(p.value(0)).value("version")}});
    }
    void installPlugin(const QString& path, const QString&,
                       std::function<void(const QVariantMap&, const QString&)> done) override
    {
        const QStringList p = read(path);
        calls << "install " + p.value(0);
        installedPaths << path;
        readFails = readFailsAfterInstall;
        if (!installCallError.isEmpty()) {
            if (installedAnyway) addInstalled(p.value(0), p.value(1), p.value(2));
            done({}, installCallError);
            return;
        }
        if (installFails.contains(p.value(0))) {
            done({{"name", p.value(0)}, {"path", ""}, {"error", "no variant for this platform"}}, {});
            return;
        }
        addInstalled(p.value(0), p.value(1), p.value(2));
        done({{"name", p.value(0)}, {"path", "/modules/" + p.value(0)}}, {});
    }
    void uninstallPackage(const QString& name, std::function<void(const QVariantMap&)> done) override
    {
        calls << "uninstall " + name;
        if (uninstallFails.contains(name)) {
            done({{"success", false}, {"error", "busy"}});
            return;
        }
        installed.remove(name);
        done({{"success", true}});
    }
    void readInstalledPackages(std::function<void(const QVariantList&, bool)> done) override
    {
        calls << "read";
        if (failingReads.contains(++readCount)) {
            done({}, false);
            return;
        }
        if (readFails > 0) {
            --readFails;
            done({}, false);
            return;
        }
        QVariantList list;
        for (auto it = installed.cbegin(); it != installed.cend(); ++it) {
            QVariantMap record = it.value();
            if (diskVersionOverride.contains(it.key()))
                record.insert("version", diskVersionOverride.value(it.key()));
            if (diskHashOverride.contains(it.key()))
                record.insert("hashes", QVariantMap{{"root", diskHashOverride.value(it.key())}});
            list.append(record);
        }
        done(list, true);
    }
};

struct FakeBasecamp : Consent {
    struct Asked { QString intent; QVariantMap params; };
    QList<Asked> asked;
    bool hold = false;
    QList<std::function<void(const Answer&)>> waiting;
    std::function<Answer(const QString&, const QVariantMap&)> decide;

    void ask(const QString& intent, const QVariantMap& params,
             std::function<void(const Answer&)> answered) override
    {
        asked.append({intent, params});
        if (hold) {
            waiting.append(std::move(answered));
            return;
        }
        answered(decide ? decide(intent, params) : Answer{});
    }
    void answerNext(const Answer& a)
    {
        if (!waiting.isEmpty()) waiting.takeFirst()(a);
    }
};

Consent::Answer approve(const QVariantMap& data)
{
    Consent::Answer a;
    a.approved = true;
    a.data = data;
    return a;
}

Consent::Answer refuse(const QString& error)
{
    Consent::Answer a;
    a.error = error;
    return a;
}

struct Run {
    FakeModules modules;
    FakeBasecamp basecamp;
    InstallProgress progress;
    Installer installer{&modules, &basecamp, &progress};
    QList<std::tuple<QString, Installer::Outcome, QString>> finished;

    Run()
    {
        installer.setClearDelay(10);
        installer.setConfirmRetries(3, 5);
        QObject::connect(&installer, &Installer::finished, &installer,
                         [this](const QString& name, Installer::Outcome o, const QString& e) {
                             finished.append({name, o, e});
                         });
    }
    QStringList installs() const { return modules.calls.filter(QRegularExpression("^(install|uninstall) ")); }
    QStringList actions() const { return modules.calls.filter(QRegularExpression("^(download|verify|inspect|install|uninstall) ")); }
    Installer::Outcome outcome() const { return finished.isEmpty() ? Installer::Outcome(-1) : std::get<1>(finished.last()); }
    QString error() const { return finished.isEmpty() ? QString() : std::get<2>(finished.last()); }
};

}  // namespace

// ── Installed only as approved ───────────────────────────────────────────

TEST(installs_exactly_what_basecamp_approved) {
    Run r;
    r.modules.serve("dep", "1.0.0", "d");
    r.modules.serve("app", "2.0.0", "a", "did:jwk:pub");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("dep", "1.0.0", "d"), release("app", "2.0.0", "a", "did:jwk:pub")}));
    };
    r.installer.install("app", "2.0.0", kRepo);

    CHECK_EQ(r.basecamp.asked.size(), 1);
    CHECK_EQ(r.basecamp.asked.value(0).intent, QString::fromLatin1(Consent::kConfirmInstall));
    CHECK_EQ(r.basecamp.asked.value(0).params.value("name").toString(), QStringLiteral("app"));
    CHECK_EQ(r.basecamp.asked.value(0).params.value("repositoryUrl").toString(), kRepo);
    CHECK_EQ(r.actions(), (QStringList{"download dep", "download app", "verify dep", "inspect dep",
                                           "verify app", "inspect app", "install dep", "install app"}));
    CHECK(r.outcome() == Installer::Outcome::Done);
    CHECK_EQ(r.progress.stage("app"), InstallStage::Installed);
    SPIN(40);
    CHECK(!r.progress.has("app"));
    CHECK(!r.progress.has("dep"));
}

// The person's picks go to Basecamp with the request, to plan with; what it
// approves is still all that installs.
TEST(the_versions_and_optional_packages_picked_go_with_the_request) {
    Run r;
    r.basecamp.hold = true;
    const QVariantMap versions{{"dep", "0.9.0"}};
    const QVariantMap optional{{"extra", true}, {"other", false}};
    r.installer.install("app", "2.0.0", kRepo, {{"versions", versions}, {"optional", optional}});
    CHECK_EQ(r.basecamp.asked.value(0).params.value("versions").toMap(), versions);
    CHECK_EQ(r.basecamp.asked.value(0).params.value("optional").toMap(), optional);
    r.basecamp.answerNext(refuse("cancelled"));
    SPIN(10);

    r.installer.upgrade("app", "2.0.0", 0, kRepo, {{"versions", versions}});
    CHECK_EQ(r.basecamp.asked.value(1).intent, QString::fromLatin1(Consent::kConfirmUpgrade));
    CHECK_EQ(r.basecamp.asked.value(1).params.value("versions").toMap(), versions);
    CHECK(!r.basecamp.asked.value(1).params.contains("optional"));
    r.basecamp.answerNext(refuse("cancelled"));
    SPIN(10);

    // None picked: nothing added.
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(!r.basecamp.asked.value(2).params.contains("versions"));
    CHECK(!r.basecamp.asked.value(2).params.contains("optional"));
}

// The copies are this backend's own: another download of the release can't
// change them midway.
TEST(installs_from_private_copies) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK_EQ(r.modules.installedPaths.size(), 1);
    CHECK(!r.modules.installedPaths.value(0).startsWith(r.modules.files.path()));
}

TEST(nothing_changes_when_the_person_cancels) {
    Run r;
    r.basecamp.decide = [](const QString&, const QVariantMap&) { return refuse("cancelled"); };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.modules.calls.isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Cancelled);
    CHECK(!r.progress.has("app"));
}

TEST(no_answer_from_basecamp_is_a_failure_and_changes_nothing) {
    Run r;
    r.basecamp.decide = [](const QString&, const QVariantMap&) { return refuse("unavailable"); };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.modules.calls.isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("unavailable"));
}

TEST(an_approval_without_a_plan_installs_nothing) {
    for (const QVariantMap& data : {QVariantMap{},
                                    QVariantMap{{"optionalPackages", QVariantList{QVariantMap{{"name", "x"}}}}},
                                    QVariantMap{{"plan", QVariantList{release("other", "1.0.0", "o")}}},
                                    QVariantMap{{"plan", QVariantList{QVariantMap{{"name", "app"}}}}},
                                    QVariantMap{{"file", QVariantMap{{"rootHash", "a"}}}}}) {
        Run r;
        r.basecamp.decide = [data](const QString&, const QVariantMap&) { return approve(data); };
        r.installer.install("app", "2.0.0", kRepo);
        CHECK(r.modules.calls.isEmpty());
        CHECK(r.outcome() == Installer::Outcome::Failed);
        CHECK_EQ(r.progress.stage("app"), InstallStage::Failed);
        CHECK(!r.progress.error("app").isEmpty());
    }
}

// The downloader served another build than the one approved.
TEST(a_download_outside_the_approval_installs_nothing) {
    Run r;
    r.modules.serve("app", "2.0.0", "rebuilt");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.installs().isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("changed after you approved it"));
}

// All or nothing, and nothing more is fetched once a required package failed.
TEST(a_required_package_that_failed_installs_nothing) {
    Run r;
    r.modules.downloadErrors.insert("dep", "timed out");
    r.modules.serve("app", "2.0.0", "a");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("dep", "1.0.0", "d"), release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK_EQ(r.actions(), QStringList{"download dep"});
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK_EQ(r.error(), QStringLiteral("dep: timed out"));
    CHECK_EQ(r.progress.stage("app"), InstallStage::Failed);
    CHECK(!r.progress.has("dep"));
}

TEST(an_optional_package_that_failed_is_left_out) {
    Run r;
    r.modules.downloadErrors.insert("extra", "no peers");
    r.modules.serve("app", "2.0.0", "a");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("extra", "1.0.0", "e", {}, true), release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.outcome() == Installer::Outcome::Done);
    CHECK_EQ(r.installs(), QStringList{"install app"});
    CHECK_EQ(r.progress.stage("extra"), InstallStage::Failed);
}

TEST(a_copy_signed_by_someone_else_installs_nothing) {
    Run r;
    r.modules.serve("app", "2.0.0", "a", "did:jwk:mallory");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a", "did:jwk:pub")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.installs().isEmpty());
    CHECK(r.error().contains("not signed as you approved it"));
}

// The reply named the approved build; the file holds another.
TEST(a_copy_that_isnt_the_approved_release_installs_nothing) {
    struct Lying : FakeModules {
        void downloadPinned(const QString& repositoryUrl, const QString& name, const QString& version,
                            const QString& rootHash, std::function<void(const QVariantMap&)> done) override
        {
            calls << "download " + name;
            done({{"name", name}, {"version", version}, {"rootHash", rootHash}, {"repositoryUrl", repositoryUrl},
                  {"path", writeFile(name + ".lgx", QStringList{name, version, "other", ""}.join('|'))}});
        }
    } modules;
    FakeBasecamp basecamp;
    basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    InstallProgress progress;
    Installer installer(&modules, &basecamp, &progress);
    QString error;
    QObject::connect(&installer, &Installer::finished, &installer,
                     [&](const QString&, Installer::Outcome, const QString& e) { error = e; });
    installer.install("app", "2.0.0", kRepo);
    CHECK(modules.calls.filter("install ").isEmpty());
    CHECK(error.contains("not the release you approved"));
}

TEST(what_is_on_disk_already_is_not_fetched_again) {
    Run r;
    r.modules.addInstalled("dep", "1.0.0", "d");
    r.modules.serve("app", "2.0.0", "a");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("dep", "1.0.0", "d"), release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(!r.modules.calls.contains("download dep"));
    CHECK_EQ(r.installs(), QStringList{"install app"});
}

// An app as it is on disk, under a dependency that changed (#427): only the
// dependency is fetched and installed.
TEST(an_install_of_whats_current_fetches_only_what_drifted) {
    Run r;
    r.modules.addInstalled("app", "2.0.0", "a");
    r.modules.addInstalled("dep_a", "1.0.0", "da");
    r.modules.addInstalled("dep_b", "1.0.0", "db1");
    r.modules.serve("dep_b", "1.0.0", "db2");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("dep_a", "1.0.0", "da"), release("dep_b", "1.0.0", "db2"),
                               release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK_EQ(r.modules.calls.filter("download "), QStringList{"download dep_b"});
    CHECK_EQ(r.installs(), (QStringList{"uninstall dep_b", "install dep_b"}));
    CHECK(r.outcome() == Installer::Outcome::Done);
}

// Installed meanwhile, all of it as approved: nothing to fetch, and done.
TEST(an_install_of_what_is_on_disk_already_is_done) {
    Run r;
    r.modules.addInstalled("app", "2.0.0", "a");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.modules.calls.filter("download ").isEmpty());
    CHECK(r.installs().isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Done);
}

// The same version again is what was asked for: it is fetched as it is.
TEST(a_reinstall_fetches_its_package_even_as_it_is) {
    Run r;
    r.modules.addInstalled("app", "2.0.0", "a");
    r.modules.serve("app", "2.0.0", "a");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.upgrade("app", "2.0.0", 2, kRepo);
    CHECK_EQ(r.modules.calls.filter("download "), QStringList{"download app"});
    CHECK(r.outcome() == Installer::Outcome::Done);
}

// ── Replacing a copy ─────────────────────────────────────────────────────

// Nothing is removed until its replacement is here and checked.
TEST(an_upgrade_removes_the_users_copy_only_after_its_replacement_is_checked) {
    Run r;
    r.modules.addInstalled("app", "1.0.0", "a1");
    r.modules.serve("app", "2.0.0", "a2");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a2")}));
    };
    r.installer.upgrade("app", "2.0.0", 0, kRepo);
    CHECK_EQ(r.basecamp.asked.value(0).intent, QString::fromLatin1(Consent::kConfirmUpgrade));
    CHECK_EQ(r.basecamp.asked.value(0).params.value("mode").toInt(), 0);
    CHECK_EQ(r.actions(), (QStringList{"download app", "verify app", "inspect app",
                                           "uninstall app", "install app"}));
    CHECK(r.outcome() == Installer::Outcome::Done);
}

TEST(a_replacement_that_fails_its_checks_leaves_the_old_copy) {
    Run r;
    r.modules.addInstalled("app", "1.0.0", "a1");
    r.modules.serve("app", "2.0.0", "a2", "did:jwk:mallory");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a2", "did:jwk:pub")}));
    };
    r.installer.upgrade("app", "2.0.0", 0, kRepo);
    CHECK(r.installs().isEmpty());
    CHECK(r.modules.installed.contains("app"));
}

TEST(an_embedded_copy_stays_and_its_replacement_goes_beside_it) {
    Run r;
    r.modules.addInstalled("app", "1.0.0", "a1", "embedded");
    r.modules.serve("app", "2.0.0", "a2");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a2")}));
    };
    r.installer.upgrade("app", "2.0.0", 0, kRepo);
    CHECK_EQ(r.installs(), QStringList{"install app"});
}

TEST(a_copy_that_wont_go_stops_the_install) {
    Run r;
    r.modules.addInstalled("app", "1.0.0", "a1");
    r.modules.uninstallFails << "app";
    r.modules.serve("app", "2.0.0", "a2");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a2")}));
    };
    r.installer.upgrade("app", "2.0.0", 0, kRepo);
    CHECK_EQ(r.installs(), QStringList{"uninstall app"});
    CHECK(r.error().contains("Could not remove the installed version: busy"));
}

// ── After installPlugin ──────────────────────────────────────────────────

TEST(an_install_that_didnt_reach_the_disk_fails) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.modules.diskVersionOverride.insert("app", "1.0.0");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("v1.0.0 is still on disk"));
}

TEST(a_failed_dependency_fails_the_app_and_says_which) {
    Run r;
    r.modules.serve("dep", "1.0.0", "d");
    r.modules.serve("app", "2.0.0", "a");
    r.modules.installFails << "dep";
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("dep", "1.0.0", "d"), release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK_EQ(r.installs(), QStringList{"install dep"});
    CHECK_EQ(r.error(), QStringLiteral("dep: no variant for this platform"));
    CHECK_EQ(r.progress.stage("app"), InstallStage::Failed);
    CHECK_EQ(r.progress.stage("dep"), InstallStage::Failed);
}

// The deadline cancels nothing: say the package may be there.
TEST(a_call_that_timed_out_says_the_package_may_be_installed) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.modules.installCallError = "package_manager did not reply within 300 s";
    r.modules.readFailsAfterInstall = 100;
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    SPIN(60);
    CHECK(r.error().contains("may in fact be installed"));
}

// package_manager runs one call at a time: a read that answers came after
// the install ended, so what it shows is the outcome.
TEST(a_call_that_timed_out_is_settled_by_the_next_read_that_answers) {
    Run r;
    r.installer.setConfirmRetries(3, 1000);
    r.modules.serve("app", "2.0.0", "a");
    r.modules.installCallError = "package_manager did not reply within 300 s";
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("not installed on disk"));
}

// Basecamp's own copy of the same release is not the install landing.
TEST(an_embedded_copy_of_the_release_doesnt_confirm_a_timed_out_install) {
    Run r;
    r.modules.addInstalled("app", "2.0.0", "a", "embedded");
    r.modules.serve("app", "2.0.0", "a");
    r.modules.installCallError = "package_manager did not reply within 300 s";
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.upgrade("app", "2.0.0", 2, kRepo);
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("not installed on disk"));
}

// Not knowing what is on disk installs nothing.
TEST(a_failed_read_before_planning_installs_nothing) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.modules.readFails = 1;
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.actions().isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("could not be read"));
}

// Not knowing whether the user's copy is there: it isn't replaced blind.
TEST(a_failed_read_before_installing_installs_nothing) {
    Run r;
    r.modules.addInstalled("app", "1.0.0", "old");
    r.modules.serve("app", "2.0.0", "a");
    r.modules.failingReads = {2};   // the plan's read answers; the one before installing doesn't
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.upgrade("app", "2.0.0", 0, kRepo);
    CHECK(r.installs().isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("could not be read"));
    CHECK_EQ(r.modules.installed.value("app").value("version").toString(), QStringLiteral("1.0.0"));
}

// Until the disk says, the request keeps its turn and its files.
TEST(a_call_that_timed_out_holds_its_turn_and_files_until_the_disk_says) {
    Run r;
    r.installer.setConfirmRetries(3, 20);
    r.modules.serve("app", "2.0.0", "a");
    r.modules.serve("next", "1.0.0", "n");
    r.modules.installCallError = "package_manager did not reply within 300 s";
    r.modules.readFailsAfterInstall = 100;
    r.basecamp.decide = [](const QString&, const QVariantMap& params) {
        return params.value("name") == "app" ? approve(planOf({release("app", "2.0.0", "a")}))
                                             : refuse("cancelled");
    };
    r.installer.install("app", "2.0.0", kRepo);
    r.installer.install("next", "1.0.0", kRepo);
    CHECK(r.finished.isEmpty());
    CHECK(r.installer.isBusy());
    CHECK_EQ(r.basecamp.asked.size(), 1);
    CHECK(QFile::exists(r.modules.installedPaths.value(0)));
    SPIN(150);
    CHECK_EQ(std::get<0>(r.finished.value(0)), QStringLiteral("app"));
    CHECK(std::get<1>(r.finished.value(0)) == Installer::Outcome::Failed);
    // Never confirmed: the install may still be reading its copy.
    CHECK(QFile::exists(r.modules.installedPaths.value(0)));
    CHECK_EQ(r.basecamp.asked.size(), 2);
}

TEST(a_call_that_timed_out_but_landed_is_done) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.modules.installCallError = "package_manager did not reply within 300 s";
    r.modules.installedAnyway = true;
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    SPIN(20);
    CHECK(r.outcome() == Installer::Outcome::Done);
}

// A reinstall of the same version that left the old build in place.
TEST(an_install_whose_build_on_disk_isnt_the_approved_one_fails) {
    Run r;
    r.modules.addInstalled("app", "2.0.0", "old");
    r.modules.serve("app", "2.0.0", "new");
    r.modules.diskHashOverride.insert("app", "old");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "new")}));
    };
    r.installer.upgrade("app", "2.0.0", 2, kRepo);
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("not the release you approved"));
}

TEST(an_install_that_cant_be_read_back_is_read_again) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.modules.readFailsAfterInstall = 2;
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    CHECK(r.finished.isEmpty());
    SPIN(40);
    CHECK(r.outcome() == Installer::Outcome::Done);
}

// Not Done on the installer's word alone.
TEST(an_install_that_never_reads_back_fails) {
    Run r;
    r.modules.serve("app", "2.0.0", "a");
    r.modules.readFailsAfterInstall = 100;
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("app", "2.0.0", "a")}));
    };
    r.installer.install("app", "2.0.0", kRepo);
    SPIN(60);
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(r.error().contains("could not be read to confirm it"));
}

// ── One at a time ────────────────────────────────────────────────────────

// The next is asked about only once the last has finished.
TEST(one_request_at_a_time_in_the_order_asked) {
    Run r;
    r.basecamp.hold = true;
    r.modules.serve("a", "1.0.0", "ha");
    r.modules.serve("b", "1.0.0", "hb");
    r.installer.install("a", "1.0.0", kRepo);
    r.installer.install("b", "1.0.0", kRepo);
    CHECK_EQ(r.basecamp.asked.size(), 1);
    CHECK(r.installer.isBusy());
    CHECK_EQ(r.installer.waiting(), QStringList{"b"});
    r.basecamp.answerNext(approve(planOf({release("a", "1.0.0", "ha")})));
    CHECK_EQ(r.installs(), QStringList{"install a"});
    CHECK_EQ(r.basecamp.asked.size(), 1);
    SPIN(5);
    CHECK_EQ(r.basecamp.asked.size(), 2);
    CHECK_EQ(r.basecamp.asked.value(1).params.value("name").toString(), QStringLiteral("b"));
    r.basecamp.answerNext(refuse("cancelled"));
    SPIN(5);
    CHECK(!r.installer.isBusy());
}

// Waiting behind another request: its row says Queued, until its turn,
// when Basecamp is asked about it.
TEST(a_request_waiting_its_turn_shows_as_queued) {
    Run r;
    r.basecamp.hold = true;
    r.modules.serve("a", "1.0.0", "ha");
    r.installer.install("a", "1.0.0", kRepo);
    CHECK(!r.progress.has("a"));
    r.installer.install("b", "1.0.0", kRepo);
    CHECK_EQ(r.progress.stage("b"), InstallStage::Queued);
    CHECK(r.progress.belongsTo("b", kRepo));
    CHECK(!r.progress.belongsTo("b", "https://other.example/logos-repo.json"));
    r.basecamp.answerNext(refuse("cancelled"));
    SPIN(5);
    CHECK_EQ(r.basecamp.asked.size(), 2);
    CHECK(!r.progress.has("b"));
    r.basecamp.answerNext(refuse("cancelled"));
    SPIN(5);
    CHECK(!r.progress.has("b"));
}

TEST(asking_twice_asks_once) {
    Run r;
    r.basecamp.hold = true;
    r.installer.install("a", "1.0.0", kRepo);
    r.installer.install("a", "1.0.0", kRepo);
    CHECK_EQ(r.basecamp.asked.size(), 1);
    CHECK(r.installer.waiting().isEmpty());
}

// Another version, source or pick is not a double click.
TEST(asking_for_something_else_is_another_request) {
    Run r;
    r.basecamp.hold = true;
    r.installer.install("a", "1.0.0", kRepo);
    r.installer.install("a", "2.0.0", kRepo);
    r.installer.install("a", "2.0.0", "https://other.example");
    r.installer.install("a", "2.0.0", "https://other.example", {{"optional", QVariantMap{{"x", true}}}});
    r.installer.install("a", "2.0.0", "https://other.example", {{"optional", QVariantMap{{"x", true}}}});
    CHECK_EQ(r.installer.waiting(), (QStringList{"a", "a", "a"}));
}

// ── A local file ─────────────────────────────────────────────────────────

TEST(a_local_file_is_installed_from_a_private_copy_as_approved) {
    Run r;
    const QString picked = r.modules.writeFile("picked.lgx", "tool|1.0.0|t|did:jwk:me");
    QString stagedAsked;
    r.basecamp.decide = [&](const QString&, const QVariantMap& params) {
        stagedAsked = params.value("localPath").toString();
        return approve({{"file", QVariantMap{{"rootHash", "t"}, {"signer", "did:jwk:me"},
                                             {"sha256", sha256Of(stagedAsked)}}}});
    };
    r.installer.installFile(picked);
    CHECK(!stagedAsked.isEmpty());
    CHECK(stagedAsked != picked);
    CHECK_EQ(r.basecamp.asked.value(0).params.value("name").toString(), QStringLiteral("tool"));
    CHECK_EQ(r.installs(), QStringList{"install tool"});
    CHECK_EQ(r.modules.installedPaths.value(0), stagedAsked);
    CHECK(r.outcome() == Installer::Outcome::Done);
    CHECK(!QFile::exists(stagedAsked));
}

// An install that never confirmed may still read the staged copy.
TEST(a_local_file_whose_install_never_confirmed_keeps_its_copy) {
    Run r;
    r.modules.installCallError = "package_manager did not reply within 300 s";
    r.modules.readFailsAfterInstall = 100;
    const QString picked = r.modules.writeFile("picked.lgx", "tool|1.0.0|t|");
    r.basecamp.decide = [](const QString&, const QVariantMap& params) {
        const QString staged = params.value("localPath").toString();
        return approve({{"file", QVariantMap{{"rootHash", "t"}, {"signer", ""}, {"sha256", sha256Of(staged)}}}});
    };
    r.installer.installFile(picked);
    SPIN(60);
    CHECK(r.outcome() == Installer::Outcome::Failed);
    CHECK(QFile::exists(r.modules.installedPaths.value(0)));
}

TEST(a_local_file_that_changed_after_approval_installs_nothing) {
    Run r;
    const QString picked = r.modules.writeFile("picked.lgx", "tool|1.0.0|t|");
    r.basecamp.decide = [&](const QString&, const QVariantMap&) {
        return approve({{"file", QVariantMap{{"rootHash", "t"}, {"signer", ""}, {"sha256", "not-this-file"}}}});
    };
    r.installer.installFile(picked);
    CHECK(r.installs().isEmpty());
    CHECK(r.error().contains("The file changed after you approved it"));
}

TEST(a_local_file_approved_without_its_digest_installs_nothing) {
    Run r;
    const QString picked = r.modules.writeFile("picked.lgx", "tool|1.0.0|t|");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve({{"file", QVariantMap{{"rootHash", "t"}, {"signer", ""}}}});
    };
    r.installer.installFile(picked);
    CHECK(r.installs().isEmpty());
    CHECK(r.error().contains("The file changed after you approved it"));
}

TEST(a_local_file_approved_as_a_catalog_release_installs_nothing) {
    Run r;
    const QString picked = r.modules.writeFile("picked.lgx", "tool|1.0.0|t|");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve(planOf({release("tool", "1.0.0", "t")}));
    };
    r.installer.installFile(picked);
    CHECK(r.modules.calls.filter("download ").isEmpty());
    CHECK(r.installs().isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Failed);
}

TEST(a_local_file_for_an_installed_package_asks_for_an_upgrade) {
    Run r;
    r.modules.addInstalled("tool", "1.0.0", "t1");
    const QString picked = r.modules.writeFile("picked.lgx", "tool|0.9.0|t0|");
    r.basecamp.decide = [](const QString&, const QVariantMap&) { return refuse("cancelled"); };
    r.installer.installFile(picked);
    CHECK_EQ(r.basecamp.asked.value(0).intent, QString::fromLatin1(Consent::kConfirmUpgrade));
    CHECK_EQ(r.basecamp.asked.value(0).params.value("mode").toInt(), 1);
}

TEST(only_a_readable_lgx_is_offered) {
    Run r;
    r.installer.installFile(r.modules.writeFile("notes.txt", "x"));
    CHECK(r.basecamp.asked.isEmpty());
    CHECK(r.error().contains("not a .lgx package"));
    SPIN(5);
    r.installer.installFile(r.modules.writeFile("broken.lgx", "x"));
    CHECK(r.basecamp.asked.isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Failed);
}

// ── Uninstalling ─────────────────────────────────────────────────────────

TEST(uninstalling_asks_first_and_reports_each_package) {
    Run r;
    r.modules.addInstalled("a", "1.0.0", "ha");
    r.modules.addInstalled("b", "1.0.0", "hb");
    r.modules.uninstallFails << "b";
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve({{"names", QStringList{"a", "b"}}});
    };
    r.installer.uninstall({"a", "b"});
    CHECK_EQ(r.basecamp.asked.value(0).intent, QString::fromLatin1(Consent::kConfirmUninstall));
    CHECK_EQ(r.basecamp.asked.value(0).params.value("names").toStringList(), (QStringList{"a", "b"}));
    CHECK_EQ(r.installs(), (QStringList{"uninstall a", "uninstall b"}));
    CHECK_EQ(r.finished.size(), 2);
    CHECK(std::get<1>(r.finished.value(0)) == Installer::Outcome::Done);
    CHECK(std::get<1>(r.finished.value(1)) == Installer::Outcome::Failed);
    CHECK_EQ(std::get<2>(r.finished.value(1)), QStringLiteral("busy"));
}

// Basecamp's answer names what goes: an app and what only it needed.
TEST(an_uninstall_removes_what_basecamp_approved) {
    Run r;
    r.modules.addInstalled("app", "1.0.0", "ha");
    r.modules.addInstalled("dep", "1.0.0", "hd");
    r.basecamp.decide = [](const QString&, const QVariantMap&) {
        return approve({{"names", QStringList{"app", "dep"}}});
    };
    r.installer.uninstall({"app"});
    CHECK_EQ(r.basecamp.asked.value(0).params.value("names").toStringList(), QStringList{"app"});
    CHECK_EQ(r.installs(), (QStringList{"uninstall app", "uninstall dep"}));
    CHECK_EQ(r.finished.size(), 2);
}

// An approval that doesn't name what goes removes nothing.
TEST(an_uninstall_approved_without_names_removes_nothing) {
    for (const QVariantMap& data : {QVariantMap{}, QVariantMap{{"names", QStringList{}}},
                                    QVariantMap{{"names", QStringList{"app", ""}}}}) {
        Run r;
        r.modules.addInstalled("app", "1.0.0", "ha");
        r.basecamp.decide = [data](const QString&, const QVariantMap&) { return approve(data); };
        r.installer.uninstall({"app"});
        CHECK(r.installs().isEmpty());
        CHECK(r.outcome() == Installer::Outcome::Failed);
        CHECK(r.error().contains("did not say what to remove"));
    }
}

TEST(a_cancelled_uninstall_removes_nothing) {
    Run r;
    r.modules.addInstalled("a", "1.0.0", "ha");
    r.basecamp.decide = [](const QString&, const QVariantMap&) { return refuse("cancelled"); };
    r.installer.uninstall({"a"});
    CHECK(r.installs().isEmpty());
    CHECK(r.outcome() == Installer::Outcome::Cancelled);
}

// ── Progress ─────────────────────────────────────────────────────────────

// The catalog's sizes, so a tile's total is right before a byte moves.
TEST(a_plans_total_comes_from_the_catalog) {
    Run r;
    PackageModel packages;
    QVariantMap dep = fixtures::catalogRow(kRepo, "dep", "1.0.0", "d");
    QVariantMap app = fixtures::catalogRow(kRepo, "app", "2.0.0", "a");
    for (QVariantMap* row : {&dep, &app}) {
        QVariantList versions = row->value("versions").toList();
        QVariantMap v = versions.first().toMap();
        v.insert("size", row == &dep ? 3000 : 1000);
        versions[0] = v;
        row->insert("versions", versions);
    }
    packages.setCatalog({dep, app});
    r.installer.setPackages(&packages);
    r.basecamp.hold = true;
    r.installer.install("app", "2.0.0", kRepo);
    r.modules.serve("dep", "1.0.0", "d");
    r.modules.serve("app", "2.0.0", "a");
    // The download answers synchronously in the fake, so watch the plan
    // being registered instead.
    quint64 total = 0;
    QObject::connect(&r.progress, &InstallProgress::planStageChanged, &r.progress,
                     [&](const QString& name) { if (name == "app" && !total) total = r.progress.planDownloadTotal("app"); });
    r.basecamp.answerNext(approve(planOf({release("dep", "1.0.0", "d"), release("app", "2.0.0", "a")})));
    CHECK_EQ(total, quint64(4000));
}

CHECK_MAIN()
