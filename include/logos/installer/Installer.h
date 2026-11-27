#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <vector>

class QTemporaryDir;

namespace logos::installer {

class Consent;
class InstallModules;
class InstallProgress;
class PackageModel;

// Installs, upgrades and uninstalls what Basecamp approves, and nothing else.
// The backend passes on what the person asked for; the installer asks
// Basecamp (Consent) and does exactly what its answer approves.
//
// A catalog release: every release the approval names, downloaded pinned;
// all of it or none, and nothing outside it; copied where only this backend
// writes, and each copy checked to be the approved release, signed as
// approved; then installed in order. A copy the user installed earlier is
// removed only once its replacement is here and checked. Each install is
// checked on disk: the approved version and root hash, or it failed.
//
// Installing is not a transaction: a package that fails to install after
// its old copy was removed leaves neither, and what installed before it in
// the plan stays installed.
//
// A local .lgx: staged as a private copy, shown to Basecamp, and installed
// only while it is the file Basecamp approved.
//
// One request at a time, in the order asked. A catalog install or upgrade
// asked for while another runs is put to Basecamp at once, saying it can
// wait (`waits`): Basecamp shows it now, and may approve it for its turn with
// a ticket, which the request hands back when its turn comes (`ticket`) for
// the plan to install; Basecamp asks again if what it planned on changed. A
// local file and an uninstall are asked about at their turn. Progress goes to
// an InstallProgress; the backend forwards package_downloader's
// downloadProgress to it.
class Installer : public QObject {
    Q_OBJECT

public:
    enum class Outcome { Done, Cancelled, Failed };
    Q_ENUM(Outcome)

    Installer(InstallModules* modules, Consent* consent, InstallProgress* progress,
              QObject* parent = nullptr);
    ~Installer() override;

    // The catalog, for each download's size, so a tile's total is right from
    // the start.
    void setPackages(PackageModel* packages);
    // How long a finished install's rows stay Installed before they show what
    // is on disk.
    void setClearDelay(int ms) { m_clearDelayMs = ms; }
    // How often, and how long apart, the installed packages are read again
    // when an install can't be confirmed yet: the read failed, or the call
    // timed out and may still be running. The request holds its turn and its
    // files until a read answers; package_manager runs one call at a time, so
    // that read came after the install ended. Without one, the files stay
    // while the installer lives.
    void setConfirmRetries(int retries, int intervalMs)
    {
        m_confirmRetries = retries;
        m_confirmRetryMs = intervalMs;
    }

    // What the person asked for; none of them carries an approval. `choices`
    // ({versions: {name: version}, optional: {name: bool}}, each optional) go
    // to Basecamp with the request: the version picked for each package the
    // install brings, and which optional ones to take. Basecamp resolves the
    // plan with them and shows it; what it approves is what installs.
    void install(const QString& name, const QString& version, const QString& repositoryUrl,
                 const QVariantMap& choices = {});
    // `mode`: 0 upgrade, 1 downgrade, 2 the same version again.
    void upgrade(const QString& name, const QString& version, int mode, const QString& repositoryUrl,
                 const QVariantMap& choices = {});
    void installFile(const QString& path);
    // Removes what Basecamp's answer names: these, and what only they needed.
    void uninstall(const QStringList& names);

    bool isBusy() const { return bool(m_current); }
    // The packages of the requests waiting their turn.
    QStringList waiting() const;

signals:
    // Basecamp is asked about `name`.
    void asking(const QString& name);
    void finished(const QString& name, logos::installer::Installer::Outcome outcome,
                  const QString& error);
    // The change Basecamp approved with `change` in its answer is over, after
    // finished(). A request that ended unapproved, or approved only for its
    // turn, made no change and says nothing here.
    void changeEnded(const QString& change);
    void busyChanged();

private:
    struct Job;
    using JobPtr = std::shared_ptr<Job>;

    void enqueue(JobPtr job);
    void startNext();
    QVariantMap catalogParams(const JobPtr& job) const;
    void askCatalog(const JobPtr& job);
    // Its turn, with Basecamp's answer to askCatalog().
    void goOn(const JobPtr& job);
    void askGoAhead(const JobPtr& job, const QString& ticket);
    void runCatalog(const JobPtr& job, const QVariantMap& approved);
    // Not approved before its turn: out of the queue.
    void drop(const JobPtr& job, const QString& error);
    void stageFile(const JobPtr& job);
    void askFile(const JobPtr& job, const QVariantMap& inspected);
    void askUninstall(const JobPtr& job);
    void uninstallNext(const JobPtr& job, int index);
    void download(const JobPtr& job, int index);
    void hold(const JobPtr& job);
    void checkCopies(const JobPtr& job, const QVariantList& rows, int index);
    void installRows(const JobPtr& job, const QVariantList& rows, int index);
    void installOne(const JobPtr& job, const QVariantList& rows, int index);
    void confirmOnDisk(const JobPtr& job, const QVariantList& rows, int index,
                       const QString& callError, int attempt);
    void succeed(const JobPtr& job);
    void failBefore(const JobPtr& job, const QString& why);
    void failDuring(const JobPtr& job, const QVariantMap& row, const QString& why);
    void endUnapproved(const JobPtr& job, const QString& error);
    void end(const JobPtr& job, Outcome outcome, const QString& error);
    quint64 sizeOf(const QVariantMap& row) const;

    InstallModules* m_modules;
    Consent* m_consent;
    InstallProgress* m_progress;
    QPointer<PackageModel> m_packages;
    int m_clearDelayMs = 1500;
    int m_confirmRetries = 24;
    int m_confirmRetryMs = 5000;

    QHash<QString, int> m_runs;
    QList<JobPtr> m_queue;
    JobPtr m_current;
    std::unique_ptr<QTemporaryDir> m_staging;
    // Copies an install that never confirmed may still be reading.
    std::vector<std::shared_ptr<QTemporaryDir>> m_kept;
};

}  // namespace logos::installer
