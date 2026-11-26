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
// checked on disk.
//
// A local .lgx: staged as a private copy, shown to Basecamp, and installed
// only while it is the file Basecamp approved.
//
// One request at a time, in the order asked: the next is asked about only
// once the last has finished. Progress goes to an InstallProgress; the
// backend forwards package_downloader's downloadProgress to it.
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
    void busyChanged();

private:
    struct Job;
    using JobPtr = std::shared_ptr<Job>;

    void enqueue(JobPtr job);
    void startNext();
    void askCatalog(const JobPtr& job);
    void stageFile(const JobPtr& job);
    void askFile(const JobPtr& job, const QVariantMap& inspected);
    void askUninstall(const JobPtr& job);
    void uninstallNext(const JobPtr& job, int index);
    void download(const JobPtr& job, int index);
    void hold(const JobPtr& job);
    void checkCopies(const JobPtr& job, const QVariantList& rows, int index);
    void installRows(const JobPtr& job, const QVariantList& rows, int index);
    void installOne(const JobPtr& job, const QVariantList& rows, int index);
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

    QHash<QString, int> m_runs;
    QList<JobPtr> m_queue;
    JobPtr m_current;
    std::unique_ptr<QTemporaryDir> m_staging;
};

}  // namespace logos::installer
