#pragma once

#include "Types.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace logos::installer {

// The installs in flight, one entry per package name. The install side
// writes it; the package list reads it, so a row shows its own download and
// an app's tile the whole install it started (its plan).
//
// A name is not unique across repositories, so an entry records the
// repository it installs from, and a row shows it only when belongsTo() says
// so. An entry with no repository matches every row of that name.
class InstallProgress : public QObject {
    Q_OBJECT

public:
    struct Entry {
        QString       name;
        QString       repositoryUrl;
        QString       targetVersion;
        QString       targetHash;
        InstallStage  stage = InstallStage::None;
        QString       error;
        QString       startedByTopLevel;
        QSet<QString> topLevels;
        quint64       downloadReceived = 0;
        quint64       downloadTotal    = 0;
        bool          downloadComplete = false;
        QString       transport;
        bool          fellBackFromStorage = false;
        // A request behind another, before Basecamp is asked about it.
        bool          waiting = false;
    };

    // Initialised positionally ({name, version, rootHash, size}): add fields
    // at the end.
    struct PlannedPackage {
        QString name;
        QString version;
        QString rootHash;
        quint64 size = 0;
        QString repositoryUrl;
    };

    explicit InstallProgress(QObject* parent = nullptr);

    bool has(const QString& name) const { return m_ops.contains(name); }
    // No entry, an entry with no repository, or the same repository.
    bool belongsTo(const QString& name, const QString& repositoryUrl) const;
    InstallStage stage(const QString& name) const;
    // Downloading, Downloaded, Queued or Installing.
    bool isInFlight(const QString& name) const;
    QString error(const QString& name) const;
    QString targetVersion(const QString& name) const;
    QString targetHash(const QString& name) const;
    quint64 downloadReceived(const QString& name) const;
    quint64 downloadTotal(const QString& name) const;
    QString downloadTransport(const QString& name) const;
    bool    downloadFellBack(const QString& name) const;
    // Summed over every package whose plan `name` started; a package that
    // started none reports its own.
    quint64 planDownloadReceived(const QString& name) const;
    quint64 planDownloadTotal(const QString& name) const;
    // One phase for the whole plan: Queued while it waits its turn
    // (beginWaiting()), Downloading until something installs, then
    // Installing, then Installed; Failed if any package failed.
    InstallStage planStage(const QString& name) const;
    // The plan's package on the wire now, else `name`'s own.
    QString planDownloadTransport(const QString& name) const;
    bool    planDownloadFellBack(const QString& name) const;
    QStringList activeNames() const { return m_ops.keys(); }

    void begin(const QString& name, const QString& targetVersion, const QString& targetHash,
               const QString& startedByTopLevel, const QString& repositoryUrl = {});
    // begin(), or join an entry already there without resetting it.
    void beginOrTrack(const QString& name, const QString& targetVersion,
                      const QString& targetHash, const QString& startedByTopLevel,
                      const QString& repositoryUrl = {});
    // Every package the install of `topLevel` brings, each Queued until its
    // first bytes; its size seeds the plan's total. One in flight already is
    // joined; what an earlier attempt left of it, or of `topLevel`'s own row
    // (failed, or installed), starts afresh.
    void beginPlan(const QString& topLevel, const QList<PlannedPackage>& plan);
    // A request for `name` waiting its turn behind another, before Basecamp
    // is asked about it: Queued, and so is its plan, until clear() or its
    // approved plan (beginPlan()). Nothing if `name` is in flight already;
    // what an earlier attempt left goes.
    void beginWaiting(const QString& name, const QString& version, const QString& repositoryUrl);
    void setStage(const QString& name, InstallStage stage);
    // Only while Queued or Downloading. A total of 0 keeps the known one; an
    // empty transport keeps the last; "http" after "logos" is a fallback.
    void setDownloadProgress(const QString& name, quint64 received, quint64 total,
                             const QString& transport = {});
    void fail(const QString& name, const QString& error);
    // An entry of its own for `name`, Failed with `error`: for a request that
    // failed before anything began, or the one package whose install failed.
    void markFailed(const QString& name, const QString& error, const QString& repositoryUrl = {});
    void finish(const QString& name);
    void clear(const QString& name);
    // Drops `topLevel` from every entry, and the entries no other install
    // still needs.
    void clearByTopLevel(const QString& topLevel);

signals:
    void activeNamesChanged();
    void stageChanged(const QString& name);
    void errorChanged(const QString& name);
    void downloadProgressChanged(const QString& name);
    void planStageChanged(const QString& topLevel);

private:
    void announceStage(const Entry& e);

    QHash<QString, Entry> m_ops;
};

}  // namespace logos::installer
