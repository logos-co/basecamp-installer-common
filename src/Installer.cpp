#include "logos/installer/Installer.h"

#include "logos/installer/Approval.h"
#include "logos/installer/Consent.h"
#include "logos/installer/InstallModules.h"
#include "logos/installer/InstallProgress.h"
#include "logos/installer/PackageModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace logos::installer {

struct Installer::Job {
    enum class Kind { Install, Upgrade, File, Uninstall };
    Kind kind = Kind::Install;
    QString name;
    QString version;
    QString repositoryUrl;
    int mode = 0;
    QVariantMap choices;        // Install, Upgrade: {versions, optional}
    QStringList names;          // Uninstall
    QString path;               // File: the staged copy
    QVariantList plan;          // what Basecamp approved
    QVariantMap file;           // the approved file: {rootHash, signer, sha256}
    QVariantList requested;     // the plan, without what is on disk already
    QVariantList fetched;
    std::shared_ptr<QTemporaryDir> copies;
    QHash<QString, QString> uninstallErrors;
    int run = 0;                // which request for `name` this is
    bool shownQueued = false;   // its row says Queued while it waits
    bool asked = false;         // Install, Upgrade: put to Basecamp
    bool answered = false;      // and its answer is in
    Consent::Answer answer;
    bool keepFiles = false;     // an install that never confirmed may still read them
};

namespace {

QString str(const QVariantMap& m, const char* key)
{
    return m.value(QLatin1String(key)).toString();
}

}  // namespace

Installer::Installer(InstallModules* modules, Consent* consent, InstallProgress* progress,
                     QObject* parent)
    : QObject(parent), m_modules(modules), m_consent(consent), m_progress(progress)
{
}

Installer::~Installer() = default;

void Installer::setPackages(PackageModel* packages)
{
    m_packages = packages;
}

QStringList Installer::waiting() const
{
    QStringList out;
    for (const JobPtr& job : m_queue) {
        if (job->kind == Job::Kind::Uninstall) out << job->names;
        else out << (job->name.isEmpty() ? QFileInfo(job->path).fileName() : job->name);
    }
    return out;
}

void Installer::install(const QString& name, const QString& version, const QString& repositoryUrl,
                        const QVariantMap& choices)
{
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::Install;
    job->name = name;
    job->version = version;
    job->repositoryUrl = repositoryUrl;
    job->choices = choices;
    enqueue(job);
}

void Installer::upgrade(const QString& name, const QString& version, int mode,
                        const QString& repositoryUrl, const QVariantMap& choices)
{
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::Upgrade;
    job->name = name;
    job->version = version;
    job->mode = mode;
    job->repositoryUrl = repositoryUrl;
    job->choices = choices;
    enqueue(job);
}

void Installer::installFile(const QString& path)
{
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::File;
    job->path = path;
    enqueue(job);
}

void Installer::uninstall(const QStringList& names)
{
    if (names.isEmpty()) return;
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::Uninstall;
    job->names = names;
    job->name = names.first();
    enqueue(job);
}

void Installer::enqueue(JobPtr job)
{
    // Asked twice (a double click): once is what was meant. Another version,
    // source or pick is another request.
    const auto same = [&job](const JobPtr& other) {
        return other && other->kind == job->kind && other->name == job->name
            && other->version == job->version && other->repositoryUrl == job->repositoryUrl
            && other->mode == job->mode && other->choices == job->choices
            && other->path == job->path && other->names == job->names;
    };
    if (same(m_current) || std::any_of(m_queue.cbegin(), m_queue.cend(), same)) return;
    const bool catalog = job->kind == Job::Kind::Install || job->kind == Job::Kind::Upgrade;
    // Behind another request: its row says so until its turn, and Basecamp
    // is asked about it now.
    const bool behind = bool(m_current);
    if (behind && catalog && !m_progress->isInFlight(job->name)) {
        m_progress->beginWaiting(job->name, job->version, job->repositoryUrl);
        job->shownQueued = true;
    }
    m_queue.append(job);
    if (behind && catalog) askCatalog(job);
    startNext();
}

void Installer::startNext()
{
    if (m_current || m_queue.isEmpty()) return;
    m_current = m_queue.takeFirst();
    emit busyChanged();
    const JobPtr job = m_current;
    switch (job->kind) {
    case Job::Kind::Install:
    case Job::Kind::Upgrade:
        // Asked about already: on with its answer, or once it comes.
        if (!job->asked) askCatalog(job);
        else if (job->answered) goOn(job);
        break;
    case Job::Kind::File:      stageFile(job);    break;
    case Job::Kind::Uninstall: askUninstall(job); break;
    }
}

// ── A catalog release ───────────────────────────────────────────────────

QVariantMap Installer::catalogParams(const JobPtr& job) const
{
    QVariantMap params{{QStringLiteral("name"), job->name},
                       {QStringLiteral("version"), job->version},
                       {QStringLiteral("repositoryUrl"), job->repositoryUrl},
                       {QStringLiteral("localPath"), QString()}};
    if (job->kind == Job::Kind::Upgrade) params.insert(QStringLiteral("mode"), job->mode);
    for (const QString& key : {QStringLiteral("versions"), QStringLiteral("optional")}) {
        const QVariantMap chosen = job->choices.value(key).toMap();
        if (!chosen.isEmpty()) params.insert(key, chosen);
    }
    return params;
}

void Installer::askCatalog(const JobPtr& job)
{
    QVariantMap params = catalogParams(job);
    // It can hold an approval until its turn: Basecamp asks about it now.
    params.insert(QStringLiteral("waits"), true);
    const QString intent = QLatin1String(job->kind == Job::Kind::Upgrade ? Consent::kConfirmUpgrade
                                                                         : Consent::kConfirmInstall);
    job->asked = true;
    job->run = ++m_runs[job->name];
    emit asking(job->name);
    QPointer<Installer> self(this);
    m_consent->ask(intent, params, [self, job](const Consent::Answer& answer) {
        if (!self) return;
        job->answered = true;
        job->answer = answer;
        if (self->m_current == job) self->goOn(job);
        else if (!answer.approved) self->drop(job, answer.error);
    });
}

void Installer::goOn(const JobPtr& job)
{
    if (!job->answer.approved) {
        endUnapproved(job, job->answer.error);
        return;
    }
    // Approved for its turn: the ticket buys the plan now.
    const QString ticket = str(job->answer.data, "ticket");
    if (!ticket.isEmpty()) {
        // Its go-ahead may wait on another app's change: Queued till then.
        if (!job->shownQueued && !m_progress->isInFlight(job->name)) {
            m_progress->beginWaiting(job->name, job->version, job->repositoryUrl);
            job->shownQueued = true;
        }
        askGoAhead(job, ticket);
        return;
    }
    runCatalog(job, job->answer.data);
}

void Installer::askGoAhead(const JobPtr& job, const QString& ticket)
{
    QVariantMap params = catalogParams(job);
    params.insert(QStringLiteral("ticket"), ticket);
    const QString intent = QLatin1String(job->kind == Job::Kind::Upgrade ? Consent::kConfirmUpgrade
                                                                         : Consent::kConfirmInstall);
    emit asking(job->name);
    QPointer<Installer> self(this);
    m_consent->ask(intent, params, [self, job](const Consent::Answer& answer) {
        if (!self) return;
        if (!answer.approved) {
            self->endUnapproved(job, answer.error);
            return;
        }
        if (answer.data.contains(QStringLiteral("ticket"))) {
            self->failBefore(job, QStringLiteral("Basecamp did not say to go ahead, so nothing was "
                                                 "installed."));
            return;
        }
        self->runCatalog(job, answer.data);
    });
}

void Installer::runCatalog(const JobPtr& job, const QVariantMap& approved)
{
    if (approved.contains(QStringLiteral("file"))) {
        failBefore(job, QStringLiteral("Basecamp approved a file, not this release, so "
                                       "nothing was installed."));
        return;
    }
    const QString refused = refusal(approved, false, job->name);
    if (!refused.isEmpty()) {
        failBefore(job, refused);
        return;
    }
    job->plan = approved.value(QStringLiteral("plan")).toList();
    // Its turn come: Queued no longer, on its way once its plan begins.
    if (job->shownQueued && m_progress->stage(job->name) == InstallStage::Queued)
        m_progress->clear(job->name);
    QPointer<Installer> self(this);
    m_modules->readInstalledPackages([self, job](const QVariantList& installed, bool read) {
        if (!self) return;
        // Not knowing what is on disk is not knowing what to fetch.
        if (!read) {
            self->failBefore(job, QStringLiteral("The installed packages could not be read, so "
                                                 "nothing was installed. Try again."));
            return;
        }
        // An install of what is on disk already fetches only what changed
        // under it (a dependency); an upgrade fetches its package again.
        job->requested = stillToInstall(job->plan, installed,
                                        job->kind == Job::Kind::Upgrade ? job->name : QString());
        QList<InstallProgress::PlannedPackage> planned;
        for (const QVariant& v : job->requested) {
            const QVariantMap row = v.toMap();
            planned.append({str(row, "name"), str(row, "version"), str(row, "rootHash"),
                            self->sizeOf(row), str(row, "repositoryUrl")});
        }
        self->m_progress->beginPlan(job->name, planned);
        self->download(job, 0);
    });
}

void Installer::download(const JobPtr& job, int index)
{
    if (index >= job->requested.size()) {
        hold(job);
        return;
    }
    const QVariantMap row = job->requested.at(index).toMap();
    QPointer<Installer> self(this);
    m_modules->downloadPinned(str(row, "repositoryUrl"), str(row, "name"), str(row, "version"),
                              str(row, "rootHash"),
        [self, job, index, row](const QVariantMap& reply) {
            if (!self) return;
            QVariantMap dl = reply;
            // What the downloader leaves out, it fetched as asked; the copy is
            // checked against the release before anything installs.
            for (const char* key : {"name", "version", "rootHash", "repositoryUrl"}) {
                if (str(dl, key).isEmpty()) dl.insert(QLatin1String(key), row.value(QLatin1String(key)));
            }
            if (str(dl, "path").isEmpty() && str(dl, "error").isEmpty())
                dl.insert(QStringLiteral("error"), QStringLiteral("package_downloader fetched nothing"));
            job->fetched.append(dl);
            // A required package that didn't come: nothing installs.
            if (!str(dl, "error").isEmpty() && !row.value(QStringLiteral("optional")).toBool()) {
                self->hold(job);
                return;
            }
            self->download(job, index + 1);
        });
}

void Installer::hold(const JobPtr& job)
{
    const QString why = notAsApproved(job->fetched, job->requested, job->plan);
    if (!why.isEmpty()) {
        failBefore(job, why);
        return;
    }
    // All of it is on disk already, as approved: nothing to do.
    if (job->requested.isEmpty()) {
        succeed(job);
        return;
    }
    QVariantList kept;
    for (const QVariant& v : job->fetched) {
        const QVariantMap row = v.toMap();
        if (str(row, "error").isEmpty()) kept.append(row);
        // An optional package that didn't come is left out, and its row says so.
        else m_progress->fail(str(row, "name"), str(row, "error"));
    }
    if (kept.isEmpty()) {
        failBefore(job, QStringLiteral("Nothing could be fetched."));
        return;
    }
    job->copies = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/installer-XXXXXX"));
    if (!job->copies->isValid()) {
        failBefore(job, QStringLiteral("Couldn't make a private folder to install from."));
        return;
    }
    QString error;
    kept = intoPrivateCopies(kept, job->copies->path(), &error);
    if (!error.isEmpty()) {
        failBefore(job, error);
        return;
    }
    checkCopies(job, kept, 0);
}

void Installer::checkCopies(const JobPtr& job, const QVariantList& rows, int index)
{
    if (index >= rows.size()) {
        installRows(job, rows, 0);
        return;
    }
    const QVariantMap row = rows.at(index).toMap();
    const QString path = str(row, "path");
    // The download picks a release's catalog entry by root hash alone, which
    // covers neither its manifest nor its signature: the copy is checked for
    // both before it installs, and is the file approved when the approval
    // names its digest.
    const QString differs = digestChanged(sha256Of(path), job->plan, row);
    if (!differs.isEmpty()) {
        failBefore(job, differs);
        return;
    }
    QPointer<Installer> self(this);
    m_modules->verifyPackage(path, [self, job, rows, index, row, path](const QVariantMap& verified) {
        if (!self) return;
        const QString why = signerChanged(verified, job->plan, row);
        if (!why.isEmpty()) {
            self->failBefore(job, why);
            return;
        }
        self->m_modules->inspectPackage(path, [self, job, rows, index, row](const QVariantMap& inspected) {
            if (!self) return;
            const QString changed = releaseChanged(inspected, row);
            if (!changed.isEmpty()) {
                self->failBefore(job, changed);
                return;
            }
            self->checkCopies(job, rows, index + 1);
        });
    });
}

void Installer::installRows(const JobPtr& job, const QVariantList& rows, int index)
{
    if (index >= rows.size()) {
        succeed(job);
        return;
    }
    const QVariantMap row = rows.at(index).toMap();
    const QString name = str(row, "name");
    m_progress->beginOrTrack(name, str(row, "version"), str(row, "rootHash"), job->name,
                             str(row, "repositoryUrl"));
    m_progress->setStage(name, InstallStage::Installing);

    // Read again, not from a cache: whether a copy is the user's decides
    // whether it goes first.
    QPointer<Installer> self(this);
    m_modules->readInstalledPackages([self, job, rows, index, row, name](const QVariantList& installed, bool read) {
        if (!self) return;
        // Not knowing whether the user's copy is there is not knowing whether
        // it goes first.
        if (!read) {
            self->failDuring(job, row, QStringLiteral("The installed packages could not be read, so "
                                                      "it was not installed. Try again."));
            return;
        }
        if (!removeBeforeInstall(installed, name)) {
            self->installOne(job, rows, index);
            return;
        }
        // Its replacement is here and checked.
        self->m_modules->uninstallPackage(name, [self, job, rows, index, row](const QVariantMap& result) {
            if (!self) return;
            if (!result.value(QStringLiteral("success")).toBool()) {
                const QString error = str(result, "error");
                self->failDuring(job, row, error.isEmpty()
                    ? QStringLiteral("Could not remove the installed version.")
                    : QStringLiteral("Could not remove the installed version: %1").arg(error));
                return;
            }
            self->installOne(job, rows, index);
        });
    });
}

void Installer::installOne(const JobPtr& job, const QVariantList& rows, int index)
{
    const QVariantMap row = rows.at(index).toMap();
    QPointer<Installer> self(this);
    m_modules->installPlugin(str(row, "path"), str(row, "source"),
        [self, job, rows, index, row](const QVariantMap& reply, const QString& callError) {
            if (!self) return;
            if (!callError.isEmpty()) {
                // The deadline cancels nothing: the install may still be
                // under way, so the disk decides.
                self->confirmOnDisk(job, rows, index, callError, 0);
                return;
            }
            if (!pluginInstalled(reply)) {
                const QString error = str(reply, "error");
                self->failDuring(job, row, error.isEmpty() ? QStringLiteral("Installation failed.") : error);
                return;
            }
            // The installer's word for it; the disk has the last one.
            self->confirmOnDisk(job, rows, index, {}, 0);
        });
}

void Installer::confirmOnDisk(const JobPtr& job, const QVariantList& rows, int index,
                              const QString& callError, int attempt)
{
    const QVariantMap row = rows.at(index).toMap();
    QPointer<Installer> self(this);
    m_modules->readInstalledPackages(
        [self, job, rows, index, row, callError, attempt](const QVariantList& installed, bool read) {
            if (!self) return;
            const QString missing = read ? notOnDisk(installed, str(row, "name"), str(row, "version"),
                                                     str(row, "rootHash"))
                                         : QString();
            if (read && missing.isEmpty()) {
                self->m_progress->finish(str(row, "name"));
                self->installRows(job, rows, index + 1);
                return;
            }
            // package_manager runs one call at a time, in order: a read that
            // answered came after the install ended, so the disk has the word.
            if (read) {
                self->failDuring(job, row, missing);
                return;
            }
            // Still running, or package_manager is gone. The next request's
            // calls queue behind it either way; its files stay while we do.
            if (attempt < self->m_confirmRetries) {
                QTimer::singleShot(self->m_confirmRetryMs, self, [self, job, rows, index, callError, attempt] {
                    if (self) self->confirmOnDisk(job, rows, index, callError, attempt + 1);
                });
                return;
            }
            job->keepFiles = true;
            self->failDuring(job, row, callError.isEmpty()
                ? QStringLiteral("package_manager reported it installed, but its installed packages "
                                 "could not be read to confirm it; check before retrying")
                : QStringLiteral("%1; the package may in fact be installed, check before retrying")
                      .arg(callError));
        });
}

// ── A local .lgx ────────────────────────────────────────────────────────

void Installer::stageFile(const JobPtr& job)
{
    const QFileInfo picked(job->path);
    const QString label = picked.fileName();
    if (!picked.isFile() || picked.suffix().compare(QLatin1String("lgx"), Qt::CaseInsensitive) != 0) {
        end(job, Outcome::Failed, QStringLiteral("%1 is not a .lgx package.").arg(label));
        return;
    }
    // What is checked and installed is a copy only this user can change; the
    // picked file stays free to change.
    if (!m_staging)
        m_staging = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/installer-XXXXXX"));
    const QString staged = m_staging->isValid()
        ? m_staging->filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".lgx"))
        : QString();
    if (staged.isEmpty() || !QFile::copy(job->path, staged)) {
        end(job, Outcome::Failed, QStringLiteral("Could not copy %1 to install it.").arg(label));
        return;
    }
    QFile::setPermissions(staged, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    job->path = staged;
    QPointer<Installer> self(this);
    m_modules->inspectPackage(staged, [self, job, label](const QVariantMap& inspected) {
        if (!self) return;
        const QString error = str(inspected, "error");
        if (!error.isEmpty() || str(inspected, "name").isEmpty()) {
            self->end(job, Outcome::Failed, error.isEmpty()
                ? QStringLiteral("Could not read a package name from %1.").arg(label) : error);
            return;
        }
        self->askFile(job, inspected);
    });
}

void Installer::askFile(const JobPtr& job, const QVariantMap& inspected)
{
    job->name = str(inspected, "name");
    job->version = str(inspected, "version");
    const bool replaces = inspected.value(QStringLiteral("isAlreadyInstalled")).toBool();
    QVariantMap params{{QStringLiteral("name"), job->name},
                       {QStringLiteral("version"), job->version},
                       {QStringLiteral("repositoryUrl"), QString()},
                       {QStringLiteral("localPath"), job->path}};
    if (replaces) params.insert(QStringLiteral("mode"),
                                upgradeMode(job->version, str(inspected, "installedVersion")));
    job->run = ++m_runs[job->name];
    emit asking(job->name);
    QPointer<Installer> self(this);
    m_consent->ask(QLatin1String(replaces ? Consent::kConfirmUpgrade : Consent::kConfirmInstall), params,
        [self, job](const Consent::Answer& answer) {
            if (!self) return;
            if (!answer.approved) {
                self->endUnapproved(job, answer.error);
                return;
            }
            if (!answer.data.contains(QStringLiteral("file"))) {
                self->failBefore(job, QStringLiteral("Basecamp did not approve this file, so nothing "
                                                     "was installed."));
                return;
            }
            const QString refused = refusal(answer.data, QFile::exists(job->path), job->name);
            if (!refused.isEmpty()) {
                self->failBefore(job, refused);
                return;
            }
            job->file = answer.data.value(QStringLiteral("file")).toMap();
            // Checked again right before installing: what Basecamp approved is
            // what it read, not the path.
            const QString sha256 = sha256Of(job->path);
            self->m_modules->inspectPackage(job->path, [self, job, sha256](const QVariantMap& now) {
                if (!self) return;
                const QString changed = fileChanged(now, job->file, job->name, job->version, sha256);
                if (!changed.isEmpty()) {
                    self->failBefore(job, changed);
                    return;
                }
                const QVariantMap row{{QStringLiteral("name"), job->name},
                                      {QStringLiteral("version"), job->version},
                                      {QStringLiteral("rootHash"), str(job->file, "rootHash")},
                                      {QStringLiteral("path"), job->path},
                                      {QStringLiteral("source"), QUrl::fromLocalFile(job->path).toString()}};
                self->installRows(job, {row}, 0);
            });
        });
}

// ── Uninstalling ────────────────────────────────────────────────────────

void Installer::askUninstall(const JobPtr& job)
{
    emit asking(job->name);
    QPointer<Installer> self(this);
    m_consent->ask(QLatin1String(Consent::kConfirmUninstall),
                   {{QStringLiteral("names"), job->names}},
        [self, job](const Consent::Answer& answer) {
            if (!self) return;
            if (!answer.approved) {
                self->endUnapproved(job, answer.error);
                return;
            }
            // What Basecamp approved: an app, and what only it needed.
            const QStringList approved = answer.data.value(QStringLiteral("names")).toStringList();
            if (approved.isEmpty() || approved.contains(QString())) {
                self->end(job, Outcome::Failed, QStringLiteral("Basecamp did not say what to remove, "
                                                               "so nothing was removed."));
                return;
            }
            job->names = approved;
            job->names.removeDuplicates();
            self->uninstallNext(job, 0);
        });
}

void Installer::uninstallNext(const JobPtr& job, int index)
{
    if (index >= job->names.size()) {
        end(job, Outcome::Done, {});
        return;
    }
    const QString name = job->names.at(index);
    QPointer<Installer> self(this);
    m_modules->uninstallPackage(name, [self, job, index, name](const QVariantMap& result) {
        if (!self) return;
        if (!result.value(QStringLiteral("success")).toBool()) {
            const QString error = str(result, "error");
            job->uninstallErrors.insert(name, error.isEmpty()
                ? QStringLiteral("Could not uninstall %1.").arg(name) : error);
        }
        self->uninstallNext(job, index + 1);
    });
}

// ── Endings ─────────────────────────────────────────────────────────────

void Installer::succeed(const JobPtr& job)
{
    const QString name = job->name;
    const int run = job->run;
    QPointer<Installer> self(this);
    QTimer::singleShot(m_clearDelayMs, this, [self, name, run] {
        // Not the rows of a request for the same package that started since.
        if (self && self->m_runs.value(name) == run && !(self->m_current && self->m_current->name == name))
            self->m_progress->clearByTopLevel(name);
    });
    end(job, Outcome::Done, {});
}

void Installer::failBefore(const JobPtr& job, const QString& why)
{
    // What was planned goes; the request's own row says why.
    m_progress->clearByTopLevel(job->name);
    m_progress->markFailed(job->name, why, job->repositoryUrl);
    end(job, Outcome::Failed, why);
}

void Installer::failDuring(const JobPtr& job, const QVariantMap& row, const QString& why)
{
    const QString name = str(row, "name");
    const QString message = name == job->name ? why : QStringLiteral("%1: %2").arg(name, why);
    // What installed is on disk, what didn't start goes; the failure stays.
    m_progress->clearByTopLevel(job->name);
    m_progress->markFailed(job->name, message, job->repositoryUrl);
    if (name != job->name) m_progress->markFailed(name, why, str(row, "repositoryUrl"));
    end(job, Outcome::Failed, message);
}

void Installer::drop(const JobPtr& job, const QString& error)
{
    m_queue.removeAll(job);
    endUnapproved(job, error);
}

void Installer::endUnapproved(const JobPtr& job, const QString& error)
{
    if (job->shownQueued && m_progress->stage(job->name) == InstallStage::Queued)
        m_progress->clear(job->name);
    if (error == QLatin1String("cancelled")) {
        end(job, Outcome::Cancelled, {});
        return;
    }
    end(job, Outcome::Failed, error.isEmpty()
        ? QStringLiteral("Basecamp did not answer, so nothing was changed.")
        : QStringLiteral("Basecamp did not approve this (%1), so nothing was changed.").arg(error));
}

void Installer::end(const JobPtr& job, Outcome outcome, const QString& error)
{
    if (job->keepFiles) {
        // m_staging, holding a local file's copy, lives as long as we do.
        if (job->copies) m_kept.push_back(std::move(job->copies));
    } else if (job->kind == Job::Kind::File && !job->path.isEmpty() && m_staging
               && QFileInfo(job->path).absolutePath() == QDir(m_staging->path()).absolutePath()) {
        QFile::remove(job->path);
    }
    job->copies.reset();
    if (m_current == job) m_current.reset();
    if (job->kind == Job::Kind::Uninstall && outcome == Outcome::Done) {
        for (const QString& name : job->names) {
            const auto failed = job->uninstallErrors.constFind(name);
            if (failed == job->uninstallErrors.cend()) emit finished(name, Outcome::Done, {});
            else emit finished(name, Outcome::Failed, *failed);
        }
    } else if (job->kind == Job::Kind::Uninstall) {
        for (const QString& name : job->names) emit finished(name, outcome, error);
    } else {
        emit finished(job->name.isEmpty() ? QFileInfo(job->path).fileName() : job->name, outcome, error);
    }
    emit busyChanged();
    QPointer<Installer> self(this);
    QTimer::singleShot(0, this, [self] {
        if (self) self->startNext();
    });
}

quint64 Installer::sizeOf(const QVariantMap& row) const
{
    if (!m_packages) return 0;
    const int at = m_packages->rowOf(str(row, "name"), str(row, "repositoryUrl"));
    if (at < 0) return 0;
    const QModelIndex idx = m_packages->index(at);
    for (const QVariant& v : idx.data(PackageModel::AvailableVersionsRole).toList()) {
        const QVariantMap version = v.toMap();
        if (str(version, "version") == str(row, "version") && str(version, "rootHash") == str(row, "rootHash"))
            return version.value(QStringLiteral("size")).toULongLong();
    }
    return 0;
}

}  // namespace logos::installer
