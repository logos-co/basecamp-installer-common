#include "logos/installer/InstallProgress.h"

namespace logos::installer {

namespace {

using Ops = QHash<QString, InstallProgress::Entry>;

quint64 sumOverPlan(const Ops& ops, const QString& topLevel,
                    quint64 (*pick)(const InstallProgress::Entry&))
{
    quint64 sum = 0;
    bool matched = false;
    for (auto it = ops.cbegin(); it != ops.cend(); ++it) {
        if (!it->topLevels.contains(topLevel)) continue;
        matched = true;
        sum += pick(*it);
    }
    if (matched) return sum;
    const auto own = ops.constFind(topLevel);
    return own == ops.cend() ? 0 : pick(*own);
}

const InstallProgress::Entry* onTheWire(const Ops& ops, const QString& topLevel)
{
    for (auto it = ops.cbegin(); it != ops.cend(); ++it) {
        if (it->topLevels.contains(topLevel) && it->stage == InstallStage::Downloading)
            return &*it;
    }
    const auto own = ops.constFind(topLevel);
    return own == ops.cend() ? nullptr : &*own;
}

}  // namespace

InstallProgress::InstallProgress(QObject* parent) : QObject(parent) {}

bool InstallProgress::belongsTo(const QString& name, const QString& repositoryUrl) const
{
    const auto it = m_ops.constFind(name);
    if (it == m_ops.cend() || it->repositoryUrl.isEmpty()) return true;
    return it->repositoryUrl == repositoryUrl;
}

InstallStage InstallProgress::stage(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? InstallStage::None : it->stage;
}

bool InstallProgress::isInFlight(const QString& name) const
{
    switch (stage(name)) {
    case InstallStage::Downloading:
    case InstallStage::Downloaded:
    case InstallStage::Queued:
    case InstallStage::Installing:
        return true;
    default:
        return false;
    }
}

QString InstallProgress::error(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? QString() : it->error;
}

QString InstallProgress::targetVersion(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? QString() : it->targetVersion;
}

QString InstallProgress::targetHash(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? QString() : it->targetHash;
}

quint64 InstallProgress::downloadReceived(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? 0 : it->downloadReceived;
}

quint64 InstallProgress::downloadTotal(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? 0 : it->downloadTotal;
}

QString InstallProgress::downloadTransport(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it == m_ops.cend() ? QString() : it->transport;
}

bool InstallProgress::downloadFellBack(const QString& name) const
{
    const auto it = m_ops.constFind(name);
    return it != m_ops.cend() && it->fellBackFromStorage;
}

quint64 InstallProgress::planDownloadReceived(const QString& name) const
{
    return sumOverPlan(m_ops, name, [](const Entry& e) { return e.downloadReceived; });
}

quint64 InstallProgress::planDownloadTotal(const QString& name) const
{
    return sumOverPlan(m_ops, name, [](const Entry& e) { return e.downloadTotal; });
}

InstallStage InstallProgress::planStage(const QString& name) const
{
    bool matched = false, failed = false, installing = false, onWire = false,
         fetched = false, installed = false, waiting = false;
    int count = 0, installedCount = 0;
    for (auto it = m_ops.cbegin(); it != m_ops.cend(); ++it) {
        if (!it->topLevels.contains(name)) continue;
        matched = true;
        ++count;
        switch (it->stage) {
        case InstallStage::Failed:      failed = true;     break;
        case InstallStage::Installing:  installing = true; break;
        case InstallStage::Queued:
            // Its turn hasn't come: nothing of it is on its way.
            if (it->waiting) waiting = true;
            else onWire = true;
            break;
        case InstallStage::Downloading: onWire = true;     break;
        case InstallStage::Downloaded:  fetched = true;    break;
        case InstallStage::Installed:   installed = true; ++installedCount; break;
        case InstallStage::None:                           break;
        }
    }
    if (!matched) return stage(name);
    if (failed) return InstallStage::Failed;
    if (onWire) return InstallStage::Downloading;
    if (waiting && !installing && !installed && !fetched) return InstallStage::Queued;
    // Between two packages nothing is on the wire: still one install.
    if (installing || installed)
        return installedCount == count ? InstallStage::Installed : InstallStage::Installing;
    if (fetched) return InstallStage::Downloading;
    return InstallStage::None;
}

QString InstallProgress::planDownloadTransport(const QString& name) const
{
    const Entry* e = onTheWire(m_ops, name);
    return e ? e->transport : QString();
}

bool InstallProgress::planDownloadFellBack(const QString& name) const
{
    const Entry* e = onTheWire(m_ops, name);
    return e && e->fellBackFromStorage;
}

void InstallProgress::announceStage(const Entry& e)
{
    emit stageChanged(e.name);
    for (const QString& t : e.topLevels) emit planStageChanged(t);
}

void InstallProgress::begin(const QString& name, const QString& targetVersion,
                            const QString& targetHash, const QString& startedByTopLevel,
                            const QString& repositoryUrl)
{
    if (name.isEmpty()) return;
    const bool added = !m_ops.contains(name);
    Entry& e = m_ops[name];
    e.name              = name;
    e.repositoryUrl     = repositoryUrl;
    e.targetVersion     = targetVersion;
    e.targetHash        = targetHash;
    e.stage             = InstallStage::Downloading;
    e.waiting           = false;
    e.error.clear();
    e.startedByTopLevel = startedByTopLevel;
    if (!startedByTopLevel.isEmpty()) e.topLevels.insert(startedByTopLevel);
    announceStage(e);
    if (added) emit activeNamesChanged();
}

void InstallProgress::beginOrTrack(const QString& name, const QString& targetVersion,
                                   const QString& targetHash,
                                   const QString& startedByTopLevel,
                                   const QString& repositoryUrl)
{
    if (name.isEmpty()) return;
    if (!m_ops.contains(name)) {
        begin(name, targetVersion, targetHash, startedByTopLevel, repositoryUrl);
        return;
    }
    Entry& e = m_ops[name];
    // Fill in what a plan's seed lacked; never re-point an attributed entry.
    if (e.repositoryUrl.isEmpty()) e.repositoryUrl = repositoryUrl;
    if (!targetVersion.isEmpty()) e.targetVersion = targetVersion;
    if (!targetHash.isEmpty()) e.targetHash = targetHash;
    if (!startedByTopLevel.isEmpty() && !e.topLevels.contains(startedByTopLevel)) {
        e.topLevels.insert(startedByTopLevel);
        emit downloadProgressChanged(startedByTopLevel);
    }
}

void InstallProgress::beginPlan(const QString& topLevel, const QList<PlannedPackage>& plan)
{
    if (topLevel.isEmpty()) return;
    bool added = false;
    for (const PlannedPackage& p : plan) {
        if (p.name.isEmpty()) continue;
        if (!m_ops.contains(p.name)) {
            added = true;
            Entry& e = m_ops[p.name];
            e.name              = p.name;
            e.repositoryUrl     = p.repositoryUrl;
            e.targetVersion     = p.version;
            e.targetHash        = p.rootHash;
            e.stage             = InstallStage::Queued;
            e.startedByTopLevel = topLevel;
            e.downloadTotal     = p.size;
            e.topLevels.insert(topLevel);
            emit stageChanged(p.name);
        } else {
            // Joining an install already running must not rewind it.
            Entry& e = m_ops[p.name];
            e.topLevels.insert(topLevel);
            // An approved plan brings it now.
            e.waiting = false;
            if (e.downloadTotal == 0 && p.size > 0) e.downloadTotal = p.size;
        }
        emit downloadProgressChanged(p.name);
    }
    if (added) emit activeNamesChanged();
    emit downloadProgressChanged(topLevel);
    emit planStageChanged(topLevel);
}

void InstallProgress::setStage(const QString& name, InstallStage stage)
{
    const auto it = m_ops.find(name);
    if (it == m_ops.end() || it->stage == stage) return;
    it->stage = stage;
    it->waiting = false;
    announceStage(*it);
}

void InstallProgress::beginWaiting(const QString& name, const QString& version,
                                   const QString& repositoryUrl)
{
    if (name.isEmpty() || m_ops.contains(name)) return;
    Entry& e = m_ops[name];
    e.name              = name;
    e.repositoryUrl     = repositoryUrl;
    e.targetVersion     = version;
    e.stage             = InstallStage::Queued;
    e.startedByTopLevel = name;
    e.waiting           = true;
    e.topLevels.insert(name);
    emit stageChanged(name);
    emit activeNamesChanged();
    emit downloadProgressChanged(name);
    emit planStageChanged(name);
}

void InstallProgress::setDownloadProgress(const QString& name, quint64 received,
                                          quint64 total, const QString& transport)
{
    const auto it = m_ops.find(name);
    if (it == m_ops.end()) return;
    // A queued package starts downloading with its first sample; one already
    // past its download ignores a late one.
    if (it->stage == InstallStage::Queued && !it->downloadComplete) {
        it->stage = InstallStage::Downloading;
        it->waiting = false;
        announceStage(*it);
    }
    if (it->stage != InstallStage::Downloading) return;

    if (!transport.isEmpty() && transport != it->transport) {
        if (it->transport == QLatin1String("logos") && transport == QLatin1String("http"))
            it->fellBackFromStorage = true;
        it->transport = transport;
    }
    it->downloadReceived = received;
    if (total > 0) it->downloadTotal = total;
    if (it->downloadTotal > 0 && it->downloadReceived >= it->downloadTotal) {
        it->downloadComplete = true;
        it->stage = InstallStage::Downloaded;
        announceStage(*it);
    }

    emit downloadProgressChanged(name);
    for (const QString& t : it->topLevels) {
        if (t != name) emit downloadProgressChanged(t);
    }
}

void InstallProgress::fail(const QString& name, const QString& error)
{
    const auto it = m_ops.find(name);
    if (it == m_ops.end()) return;
    const bool stageMoved = it->stage != InstallStage::Failed;
    it->stage = InstallStage::Failed;
    it->error = error;
    if (stageMoved) announceStage(*it);
    emit errorChanged(name);
}

void InstallProgress::markFailed(const QString& name, const QString& error,
                                 const QString& repositoryUrl)
{
    if (name.isEmpty()) return;
    const bool added = !m_ops.contains(name);
    Entry& e = m_ops[name];
    e = Entry{};
    e.name = name;
    e.repositoryUrl = repositoryUrl;
    e.stage = InstallStage::Failed;
    e.error = error;
    e.startedByTopLevel = name;
    e.topLevels.insert(name);
    announceStage(e);
    emit errorChanged(name);
    emit downloadProgressChanged(name);
    if (added) emit activeNamesChanged();
}

void InstallProgress::finish(const QString& name)
{
    setStage(name, InstallStage::Installed);
}

void InstallProgress::clear(const QString& name)
{
    const auto it = m_ops.find(name);
    if (it == m_ops.end()) return;
    const QSet<QString> topLevels = it->topLevels;
    m_ops.erase(it);
    emit stageChanged(name);
    emit errorChanged(name);
    emit downloadProgressChanged(name);
    for (const QString& t : topLevels) {
        if (t != name) {
            emit downloadProgressChanged(t);
            emit planStageChanged(t);
        }
    }
    emit activeNamesChanged();
}

void InstallProgress::clearByTopLevel(const QString& topLevel)
{
    if (topLevel.isEmpty()) return;
    bool touched = false;
    QStringList removed;
    for (auto it = m_ops.begin(); it != m_ops.end();) {
        if (!it->topLevels.contains(topLevel)) { ++it; continue; }
        touched = true;
        it->topLevels.remove(topLevel);
        // Another install still needs it.
        if (!it->topLevels.isEmpty()) {
            if (it->startedByTopLevel == topLevel)
                it->startedByTopLevel = *it->topLevels.cbegin();
            ++it;
            continue;
        }
        removed.append(it.key());
        it = m_ops.erase(it);
    }
    if (!touched) return;
    for (const QString& name : removed) {
        emit stageChanged(name);
        emit errorChanged(name);
        if (name != topLevel) emit downloadProgressChanged(name);
    }
    emit downloadProgressChanged(topLevel);
    emit planStageChanged(topLevel);
    if (!removed.isEmpty()) emit activeNamesChanged();
}

}  // namespace logos::installer
