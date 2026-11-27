#include "logos/installer/PackageModel.h"

#include "logos/installer/Availability.h"
#include "logos/installer/InstallProgress.h"
#include "logos/installer/PackageRows.h"
#include "logos/installer/RowState.h"

#include <logos/semver.hpp>

#include <QSet>

#include <utility>

namespace logos::installer {

namespace {

QString keyOf(const QString& repositoryUrl, const QString& name)
{
    return repositoryUrl + QChar(0x01) + name;
}

QString releaseKeyOf(const QVariantMap& version)
{
    return version.value(QStringLiteral("version")).toString() + QChar(0x1f)
         + version.value(QStringLiteral("rootHash")).toString();
}

QString keyOf(const QVariantMap& row)
{
    return keyOf(row.value(QStringLiteral("repositoryUrl")).toString(),
                 row.value(QStringLiteral("name")).toString());
}

QString str(const QVariantMap& row, const char* key)
{
    return row.value(QLatin1String(key)).toString();
}

InstallStatus statusIn(const QVariantMap& row, const char* key)
{
    return static_cast<InstallStatus>(row.value(QLatin1String(key)).toInt());
}

bool installable(const QVariantMap& row)
{
    return row.value(QStringLiteral("isVariantAvailable")).toBool()
        && row.value(QStringLiteral("isSourceAvailable"), true).toBool();
}

QVariantMap versionAt(const QVariantMap& row, int version)
{
    return row.value(QStringLiteral("availableVersions")).toList().value(version).toMap();
}

// The dependencies `key` names in the manifest of `row`'s `version`; a row
// with no versions (installed from a file) has its own.
QVariantList dependenciesAt(const QVariantMap& row, int version, const QString& key)
{
    const QVariantMap picked = versionAt(row, version);
    if (picked.contains(QStringLiteral("manifest")))
        return dependencyEntries(picked.value(QStringLiteral("manifest")).toMap().value(key).toList());
    return key == QLatin1String("dependencies")
        ? row.value(QStringLiteral("dependencyEntries")).toList() : QVariantList{};
}

bool satisfiesAll(const QString& version, const QStringList& ranges)
{
    for (const QString& range : ranges)
        if (!logos::semver::satisfies(version.toStdString(), range.toStdString())) return false;
    return true;
}

// Whether this host can fetch `row`'s `version`.
bool fetchable(const QVariantMap& row, int version)
{
    return row.value(QStringLiteral("isVariantAvailable")).toBool() && sourceAvailable(versionAt(row, version));
}

// The newest of `row`'s versions this host can fetch that satisfies every
// range, else the newest that satisfies them, else (a conflict) the newest
// it can fetch.
int newestSatisfying(const QVariantMap& row, const QStringList& ranges)
{
    const QVariantList versions = row.value(QStringLiteral("availableVersions")).toList();
    if (versions.isEmpty()) return -1;
    int satisfying = -1, any = -1;
    for (int i = 0; i < versions.size(); ++i) {
        const bool fits = satisfiesAll(versions.at(i).toMap().value(QStringLiteral("version")).toString(), ranges);
        if (fits && fetchable(row, i)) return i;
        if (fits && satisfying < 0) satisfying = i;
        if (any < 0 && fetchable(row, i)) any = i;
    }
    return satisfying >= 0 ? satisfying : any >= 0 ? any : 0;
}

int indexOfVersion(const QVariantMap& row, const QString& version)
{
    const QVariantList versions = row.value(QStringLiteral("availableVersions")).toList();
    for (int i = 0; i < versions.size(); ++i)
        if (versions.at(i).toMap().value(QStringLiteral("version")).toString() == version) return i;
    return -1;
}

// A package installing a row brings, and at which version.
struct Brought {
    QString name;
    int row = -1;            // -1: no catalog has it
    int version = -1;        // into the row's availableVersions
    bool picked = false;     // the version the person picked
    bool conflict = false;   // it doesn't satisfy every range asked of it
    bool optional = false;
    bool selected = false;   // an optional package: it comes
    QString offeredBy;       // an optional package: the package offering it
    QString requiredFor;     // comes only with this optional package
};

// What installing a row brings, as Basecamp's resolver plans it for App
// Manager: each package at the newest version this host can fetch that
// satisfies every range asked of it, unless a version was picked; then the
// optional packages any of them offers, a ticked one with what it needs.
class Resolution {
public:
    Resolution(const QList<QVariantMap>& rows, const QHash<QString, QVariantMap>& installed)
        : m_rows(rows), m_installed(installed)
    {
        for (int i = 0; i < rows.size(); ++i) {
            m_byKey.insert(keyOf(rows.at(i)), i);
            const QString name = str(rows.at(i), "name");
            if (!m_byName.contains(name)) m_byName.insert(name, i);
        }
    }

    // `versions`: name -> the version picked; `optional`: name -> ticked.
    // An optional package not named comes when it is on disk, or the app
    // isn't: an installed app takes none it doesn't have unasked.
    QList<Brought> of(int subject, const QVariantMap& versions, const QVariantMap& optional) const
    {
        const QVariantMap& app = m_rows.at(subject);
        const QString repo = str(app, "repositoryUrl");
        const bool appOnDisk = isInstalledRow(app);
        // A version chosen depends on every range asked of it, which depends
        // on the versions chosen: walk again until they hold.
        QHash<QString, int> chosen;
        QList<Brought> out;
        QHash<QString, QStringList> asked;
        for (int pass = 0; pass < kPasses; ++pass) {
            out.clear();
            asked.clear();
            QHash<QString, int> at;
            // At its version: the subject at its own.
            const auto add = [&](Brought b) {
                if (b.row >= 0 && b.row != subject) {
                    const QString pick = versions.value(b.name).toString();
                    const int picked = pick.isEmpty() ? -1 : indexOfVersion(m_rows.at(b.row), pick);
                    b.picked = picked >= 0;
                    b.version = b.picked ? picked
                                         : chosen.value(b.name, newestSatisfying(m_rows.at(b.row), asked.value(b.name)));
                }
                at.insert(b.name, out.size());
                out.append(b);
                return int(out.size() - 1);
            };
            // What `from` needs, and theirs, that isn't listed yet.
            const auto bring = [&](int from, const QString& requiredFor) {
                QList<int> queue{from};
                while (!queue.isEmpty()) {
                    const Brought b = out.at(queue.takeFirst());
                    if (b.row < 0) continue;
                    for (const QVariant& v : dependenciesAt(m_rows.at(b.row), b.version,
                                                            QStringLiteral("dependencies"))) {
                        const QVariantMap dep = v.toMap();
                        const QString name = dep.value(QStringLiteral("name")).toString();
                        asked[name].append(dep.value(QStringLiteral("version")).toString());
                        if (at.contains(name)) continue;
                        Brought next;
                        next.name = name;
                        next.row = find(repo, name);
                        next.optional = !requiredFor.isEmpty();
                        next.requiredFor = requiredFor;
                        queue.append(add(next));
                    }
                }
            };

            Brought self;
            self.name = str(app, "name");
            self.row = subject;
            self.version = app.value(QStringLiteral("selectedVersionIndex")).toInt();
            bring(add(self), {});
            for (int i = 0; i < out.size(); ++i) {
                const Brought from = out.at(i);
                // An optional package left out offers nothing.
                if (from.row < 0 || (!from.offeredBy.isEmpty() && !from.selected)) continue;
                for (const QVariant& v : dependenciesAt(m_rows.at(from.row), from.version,
                                                        QStringLiteral("optional_dependencies"))) {
                    const QVariantMap offer = v.toMap();
                    const QString name = offer.value(QStringLiteral("name")).toString();
                    if (at.contains(name)) continue;
                    asked[name].append(offer.value(QStringLiteral("version")).toString());
                    Brought next;
                    next.name = name;
                    next.row = find(repo, name);
                    next.optional = true;
                    next.offeredBy = from.name;
                    const int added = add(next);
                    Brought& b = out[added];
                    const bool onDisk = m_installed.contains(name);
                    // On disk with nothing in a catalog: kept, nothing to choose.
                    const bool kept = b.row < 0 && onDisk;
                    const bool fits = b.row >= 0 && fetchable(m_rows.at(b.row), b.version)
                        && satisfiesAll(versionOf(b), asked.value(name));
                    b.selected = kept || (fits && optional.value(name, onDisk || !appOnDisk).toBool());
                    if (b.selected && b.row >= 0) bring(added, name);
                }
            }

            QHash<QString, int> next;
            for (const Brought& b : std::as_const(out))
                if (b.row >= 0 && b.row != subject && !b.picked)
                    next.insert(b.name, newestSatisfying(m_rows.at(b.row), asked.value(b.name)));
            if (next == chosen) break;
            chosen = next;
        }
        for (Brought& b : out)
            b.conflict = b.row >= 0 && b.row != subject && !satisfiesAll(versionOf(b), asked.value(b.name));
        return out;
    }

    QString versionOf(const Brought& b) const
    {
        return b.row < 0 ? QString()
                         : versionAt(m_rows.at(b.row), b.version).value(QStringLiteral("version")).toString();
    }

private:
    static constexpr int kPasses = 8;

    // The app's repository first, then any.
    int find(const QString& repo, const QString& name) const
    {
        const int same = repo.isEmpty() ? -1 : m_byKey.value(keyOf(repo, name), -1);
        return same >= 0 ? same : m_byName.value(name, -1);
    }

    const QList<QVariantMap>& m_rows;
    const QHash<QString, QVariantMap>& m_installed;
    QHash<QString, int> m_byKey;
    QHash<QString, int> m_byName;
};

const QHash<int, QByteArray>& roles()
{
    using M = PackageModel;
    static const QHash<int, QByteArray> names{
        {M::NameRole,                        "name"},
        {M::ModuleNameRole,                  "moduleName"},
        {M::DisplayNameRole,                 "displayName"},
        {M::DescriptionRole,                 "description"},
        {M::TypeRole,                        "type"},
        {M::CategoryRole,                    "category"},
        {M::IconUrlRole,                     "iconUrl"},
        {M::CatalogIconUrlRole,              "catalogIconUrl"},
        {M::SupportsFullBleedIconRole,       "supportsFullBleedIcon"},
        {M::ProvidesRole,                    "provides"},
        {M::WebProvidesRole,                 "webProvides"},
        {M::RepositoryUrlRole,               "repositoryUrl"},
        {M::RepositoryNameRole,              "repositoryName"},
        {M::RepositoryDisplayNameRole,       "repositoryDisplayName"},
        {M::OriginRepositoryUrlRole,         "originRepositoryUrl"},
        {M::OriginRepositoryNameRole,        "originRepositoryName"},
        {M::OriginRepositoryDisplayNameRole, "originRepositoryDisplayName"},
        {M::SourceKeyRole,                   "sourceKey"},
        {M::AvailableVersionsRole,           "availableVersions"},
        {M::SelectedVersionIndexRole,        "selectedVersionIndex"},
        {M::VersionRole,                     "version"},
        {M::HashRole,                        "hash"},
        {M::LatestVersionRole,               "latestVersion"},
        {M::SizeRole,                        "size"},
        {M::DateUpdatedRole,                 "dateUpdated"},
        {M::DependenciesRole,                "dependencies"},
        {M::DependencyEntriesRole,           "dependencyEntries"},
        {M::InstalledVersionRole,            "installedVersion"},
        {M::InstalledHashRole,               "installedHash"},
        {M::InstallTypeRole,                 "installType"},
        {M::DownloadSourceRole,              "downloadSource"},
        {M::IsInstalledRole,                 "isInstalled"},
        {M::MissingDependenciesRole,         "missingDependencies"},
        {M::InstallStatusRole,               "installStatus"},
        {M::RowActionRole,                   "rowAction"},
        {M::UpdateAvailableRole,             "updateAvailable"},
        {M::IsVariantAvailableRole,          "isVariantAvailable"},
        {M::IsSourceAvailableRole,           "isSourceAvailable"},
        {M::NotAvailableReasonRole,          "notAvailableReason"},
        {M::HasInstallableVersionRole,       "hasInstallableVersion"},
        {M::IsSelectedRole,                  "isSelected"},
        {M::InstallStageRole,                "installStage"},
        {M::ErrorMessageRole,                "errorMessage"},
        {M::QueuedRole,                      "queued"},
        {M::DownloadReceivedRole,            "downloadReceived"},
        {M::DownloadTotalRole,               "downloadTotal"},
        {M::DownloadTransportRole,           "downloadTransport"},
        {M::DownloadFellBackRole,            "downloadFellBack"},
        {M::PlanInstallStageRole,            "planInstallStage"},
        {M::PlanDownloadReceivedRole,        "planDownloadReceived"},
        {M::PlanDownloadTotalRole,           "planDownloadTotal"},
        {M::PlanDownloadTransportRole,       "planDownloadTransport"},
        {M::PlanDownloadFellBackRole,        "planDownloadFellBack"},
        {M::CatalogIconSha256Role,           "catalogIconSha256"},
    };
    return names;
}

}  // namespace

PackageModel::PackageModel(QObject* parent) : QAbstractListModel(parent) {}

int PackageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> PackageModel::roleNames() const
{
    return roles();
}

bool PackageModel::belongs(const QVariantMap& row) const
{
    const QString name = str(row, "name");
    return m_progress && m_progress->has(name)
        && m_progress->belongsTo(name, str(row, "repositoryUrl"));
}

InstallStatus PackageModel::effectiveStatus(const QVariantMap& row) const
{
    if (belongs(row)) {
        const QString name = str(row, "name");
        if (m_progress->isInFlight(name)) return InstallStatus::Installing;
        if (m_progress->stage(name) == InstallStage::Failed) return InstallStatus::Failed;
    }
    return statusIn(row, "installStatus");
}

RowAction PackageModel::effectiveAction(const QVariantMap& row) const
{
    const InstallStatus status = effectiveStatus(row);
    const bool canInstall = installable(row);
    if (!m_statusIncludesDependencies) {
        return resolveRowAction(isInstalledRow(row), canInstall, status,
                                str(row, "installedVersion"), str(row, "installedHash"),
                                str(row, "version"), str(row, "hash"));
    }
    switch (status) {
    case InstallStatus::Failed:       return RowAction::Retry;
    case InstallStatus::Installing:   return RowAction::NoOp;
    case InstallStatus::NotInstalled: return canInstall ? RowAction::Install : RowAction::NotAvailable;
    case InstallStatus::Installed:    return RowAction::NoOp;
    default: break;
    }
    // Nothing to download for this host: no version change.
    if (!canInstall) return RowAction::NoOp;
    if (status == InstallStatus::UpgradeAvailable) return RowAction::Upgrade;
    if (status == InstallStatus::DowngradeAvailable) return RowAction::Downgrade;
    return RowAction::Reinstall;
}

QVariant PackageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const QVariantMap& r = m_rows.at(index.row());
    const QString name = str(r, "name");
    const bool tracked = belongs(r);

    switch (role) {
    case InstallStatusRole: return toInt(effectiveStatus(r));
    case RowActionRole:     return toInt(effectiveAction(r));
    case ErrorMessageRole:
        return tracked && m_progress->stage(name) == InstallStage::Failed
            ? m_progress->error(name) : r.value(QStringLiteral("errorMessage")).toString();
    case InstallStageRole:
        return toInt(tracked ? m_progress->stage(name) : InstallStage::None);
    case QueuedRole:
        return tracked && m_progress->stage(name) == InstallStage::Queued;
    case DownloadReceivedRole:
        return QVariant::fromValue(tracked ? m_progress->downloadReceived(name) : quint64(0));
    case DownloadTotalRole:
        return QVariant::fromValue(tracked ? m_progress->downloadTotal(name) : quint64(0));
    case DownloadTransportRole:
        return tracked ? m_progress->downloadTransport(name) : QString();
    case DownloadFellBackRole:
        return tracked && m_progress->downloadFellBack(name);
    case PlanInstallStageRole:
        return toInt(tracked ? m_progress->planStage(name) : InstallStage::None);
    case PlanDownloadReceivedRole:
        return QVariant::fromValue(tracked ? m_progress->planDownloadReceived(name) : quint64(0));
    case PlanDownloadTotalRole:
        return QVariant::fromValue(tracked ? m_progress->planDownloadTotal(name) : quint64(0));
    case PlanDownloadTransportRole:
        return tracked ? m_progress->planDownloadTransport(name) : QString();
    case PlanDownloadFellBackRole:
        return tracked && m_progress->planDownloadFellBack(name);
    default: {
        const QByteArray key = roles().value(role);
        return key.isEmpty() ? QVariant() : r.value(QString::fromLatin1(key));
    }
    }
}

void PackageModel::update(const QVariantList& catalogRows, const QVariantList& installedPackages,
                          const QStringList& validVariants)
{
    m_catalog = catalogRows;
    takeInstalled(installedPackages);
    takeValidVariants(validVariants);
    rebuild();
}

void PackageModel::setCatalog(const QVariantList& catalogRows)
{
    m_catalog = catalogRows;
    rebuild();
}

void PackageModel::setInstalled(const QVariantList& installedPackages)
{
    takeInstalled(installedPackages);
    rebuild();
}

void PackageModel::setValidVariants(const QStringList& variants)
{
    if (variants == m_validVariants) return;
    takeValidVariants(variants);
    rebuild();
}

void PackageModel::takeInstalled(const QVariantList& installedPackages)
{
    m_installedByName.clear();
    m_installedOrder.clear();
    for (const QVariant& v : installedPackages) {
        const QVariantMap pkg = v.toMap();
        const QString name = pkg.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || m_installedByName.contains(name)) continue;
        m_installedByName.insert(name, pkg);
        m_installedOrder.append(name);
    }
}

void PackageModel::takeValidVariants(const QStringList& variants)
{
    const QString before = hostVariant();
    m_validVariants = variants;
    if (hostVariant() != before) emit hostVariantChanged();
}

void PackageModel::setProgress(InstallProgress* progress)
{
    if (m_progress == progress) return;
    if (m_progress) m_progress->disconnect(this);
    m_progress = progress;
    if (m_progress) {
        const QList<int> stageRoles{InstallStatusRole, RowActionRole, ErrorMessageRole,
                                    InstallStageRole, QueuedRole, PlanInstallStageRole};
        const auto stage = [this, stageRoles](const QString& name) {
            emitRowsNamed(name, stageRoles);
        };
        connect(m_progress, &InstallProgress::stageChanged, this, stage);
        connect(m_progress, &InstallProgress::errorChanged, this, stage);
        // Narrow: this fires several times a second per download.
        connect(m_progress, &InstallProgress::downloadProgressChanged, this,
                [this](const QString& name) {
                    emitRowsNamed(name, {DownloadReceivedRole, DownloadTotalRole,
                                         DownloadTransportRole, DownloadFellBackRole,
                                         PlanDownloadReceivedRole, PlanDownloadTotalRole,
                                         PlanDownloadTransportRole, PlanDownloadFellBackRole,
                                         PlanInstallStageRole});
                });
        connect(m_progress, &InstallProgress::planStageChanged, this,
                [this](const QString& topLevel) {
                    emitRowsNamed(topLevel, {PlanInstallStageRole});
                });
    }
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1));
}

void PackageModel::setStatusIncludesDependencies(bool on)
{
    if (on == m_statusIncludesDependencies) return;
    m_statusIncludesDependencies = on;
    rebuild();
}

void PackageModel::setMissingDependencies(const QString& name, const QStringList& dependencies)
{
    if (m_missingByName.value(name) == dependencies) return;
    if (dependencies.isEmpty()) m_missingByName.remove(name);
    else m_missingByName.insert(name, dependencies);
    rebuild();
}

void PackageModel::setIcon(const QString& repositoryUrl, const QString& name, const QString& url,
                           bool fullBleed)
{
    const QString key = keyOf(repositoryUrl, name);
    const auto it = m_iconByKey.constFind(key);
    if (it != m_iconByKey.cend() && it->url == url && it->fullBleed == fullBleed) return;
    if (url.isEmpty()) m_iconByKey.remove(key);
    else m_iconByKey.insert(key, Icon{url, fullBleed});
    rebuild();
}

void PackageModel::selectVersion(int row, int versionIndex)
{
    if (row < 0 || row >= m_rows.size()) return;
    QVariantMap& r = m_rows[row];
    const int count = r.value(QStringLiteral("availableVersions")).toList().size();
    if (versionIndex < 0 || versionIndex >= count) versionIndex = 0;
    if (r.value(QStringLiteral("selectedVersionIndex")).toInt() == versionIndex) return;

    // The release, not its place: a newer one moves the rest down. The
    // newest is no pick, and follows what is newest.
    const QString key = keyOf(r);
    if (versionIndex == 0) m_selectedVersionByKey.remove(key);
    else m_selectedVersionByKey.insert(key, releaseKeyOf(r.value(QStringLiteral("availableVersions"))
                                                             .toList().at(versionIndex).toMap()));
    logos::installer::selectVersion(r, versionIndex);
    emit dataChanged(index(row), index(row),
                     {SelectedVersionIndexRole, VersionRole, HashRole, SizeRole, DateUpdatedRole,
                      OriginRepositoryUrlRole, OriginRepositoryNameRole, IsSourceAvailableRole,
                      NotAvailableReasonRole, RowActionRole});
}

void PackageModel::setSelected(int row, bool selected)
{
    if (row < 0 || row >= m_rows.size()) return;
    QVariantMap& r = m_rows[row];
    if (r.value(QStringLiteral("isSelected")).toBool() == selected) return;
    const QString key = keyOf(r);
    if (selected) m_selectedKeys.insert(key);
    else m_selectedKeys.remove(key);
    r.insert(QStringLiteral("isSelected"), selected);
    emit dataChanged(index(row), index(row), {IsSelectedRole});
    emit selectionChanged();
}

void PackageModel::clearSelection()
{
    m_selectedKeys.clear();
    bool any = false;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!m_rows[i].value(QStringLiteral("isSelected")).toBool()) continue;
        m_rows[i].insert(QStringLiteral("isSelected"), false);
        emit dataChanged(index(i), index(i), {IsSelectedRole});
        any = true;
    }
    if (any) emit selectionChanged();
}

QList<int> PackageModel::selectedRows() const
{
    QList<int> out;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].value(QStringLiteral("isSelected")).toBool()) out.append(i);
    }
    return out;
}

int PackageModel::rowOf(const QString& name, const QString& repositoryUrl) const
{
    if (!repositoryUrl.isEmpty()) return m_rowByKey.value(keyOf(repositoryUrl, name), -1);
    for (int i = 0; i < m_rows.size(); ++i) {
        if (str(m_rows[i], "name") == name) return i;
    }
    return -1;
}

QVariantList PackageModel::packagesBroughtBy(int row, const QVariantMap& optional,
                                             const QVariantMap& versions) const
{
    QVariantList out;
    if (row < 0 || row >= m_rows.size()) return out;

    // A package at `version`: its own state then, and its download.
    const auto entryAt = [&](int at, int version) {
        const QVariantMap& r = m_rows.at(at);
        const QString name = str(r, "name");
        const bool tracked = belongs(r);
        const QVariantList available = r.value(QStringLiteral("availableVersions")).toList();
        const QVariantMap v = versionAt(r, version);
        const QString to = v.isEmpty() ? str(r, "version") : v.value(QStringLiteral("version")).toString();
        const QString toHash = v.isEmpty() ? str(r, "hash") : v.value(QStringLiteral("rootHash")).toString();
        QStringList all;
        QVariantList served;
        for (int i = 0; i < available.size(); ++i) {
            all << available.at(i).toMap().value(QStringLiteral("version")).toString();
            served << fetchable(r, i);
        }
        const bool onDisk = isInstalledRow(r);
        InstallStatus status = installStatusOf(onDisk, str(r, "installedVersion"), str(r, "installedHash"),
                                               to, toHash);
        if (tracked && m_progress->isInFlight(name)) status = InstallStatus::Installing;
        else if (tracked && m_progress->stage(name) == InstallStage::Failed) status = InstallStatus::Failed;
        return QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("repositoryUrl"), str(r, "repositoryUrl")},
            {QStringLiteral("displayName"), str(r, "displayName")},
            {QStringLiteral("description"), str(r, "description")},
            {QStringLiteral("versions"), all},
            {QStringLiteral("versionsServed"), served},
            {QStringLiteral("selectedVersionIndex"), qMax(version, 0)},
            {QStringLiteral("version"), to},
            {QStringLiteral("installedVersion"), str(r, "installedVersion")},
            {QStringLiteral("size"), v.isEmpty() ? r.value(QStringLiteral("size")) : v.value(QStringLiteral("size"))},
            {QStringLiteral("rowAction"), toInt(resolveRowAction(
                onDisk, v.isEmpty() ? installable(r) : fetchable(r, version), status,
                str(r, "installedVersion"), str(r, "installedHash"), to, toHash))},
            {QStringLiteral("installing"), tracked && m_progress->isInFlight(name)},
            {QStringLiteral("installStage"), toInt(tracked ? m_progress->stage(name) : InstallStage::None)},
            {QStringLiteral("queued"), tracked && m_progress->stage(name) == InstallStage::Queued},
            {QStringLiteral("downloadReceived"),
             QVariant::fromValue(tracked ? m_progress->downloadReceived(name) : quint64(0))},
            {QStringLiteral("downloadTotal"),
             QVariant::fromValue(tracked ? m_progress->downloadTotal(name) : quint64(0))},
            {QStringLiteral("downloadTransport"), tracked ? m_progress->downloadTransport(name) : QString()},
            {QStringLiteral("downloadFellBack"), tracked && m_progress->downloadFellBack(name)},
            {QStringLiteral("found"), true},
        };
    };
    // In no catalog: what is installed, a module that came with Basecamp,
    // say; else nothing can bring it.
    const auto elsewhere = [&](const QString& name) {
        QVariantMap e{{QStringLiteral("name"), name}, {QStringLiteral("displayName"), name},
                      {QStringLiteral("found"), true}};
        const auto installed = m_installedByName.constFind(name);
        if (installed != m_installedByName.cend()) {
            e.insert(QStringLiteral("installedVersion"), installed->value(QStringLiteral("version")));
            e.insert(QStringLiteral("rowAction"), toInt(RowAction::NoOp));
            e.insert(QStringLiteral("installedOnly"), true);
        } else {
            e.insert(QStringLiteral("found"), false);
            e.insert(QStringLiteral("rowAction"), toInt(RowAction::NotAvailable));
        }
        return e;
    };

    const Resolution resolution(m_rows, m_installedByName);
    for (const Brought& b : resolution.of(row, versions, optional)) {
        QVariantMap e = b.row >= 0 ? entryAt(b.row, b.version) : elsewhere(b.name);
        e.insert(QStringLiteral("optional"), b.optional);
        if (b.row == row) e.insert(QStringLiteral("subject"), true);
        if (b.picked) e.insert(QStringLiteral("picked"), true);
        if (b.conflict) e.insert(QStringLiteral("conflict"), true);
        if (!b.offeredBy.isEmpty()) {
            e.insert(QStringLiteral("offeredBy"), b.offeredBy);
            e.insert(QStringLiteral("selected"), b.selected);
        }
        if (!b.requiredFor.isEmpty()) e.insert(QStringLiteral("requiredFor"), b.requiredFor);
        out.append(e);
    }
    return out;
}

QVariantMap PackageModel::installedRecord(const QString& name) const
{
    return m_installedByName.value(name);
}

QVariantMap PackageModel::rowAt(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size()) return out;
    const QModelIndex mi = index(row);
    for (auto it = roles().cbegin(); it != roles().cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(mi, it.key()));
    return out;
}

QList<QVariantMap> PackageModel::buildRows() const
{
    QList<QVariantMap> rows;
    QSet<QString> keys;
    QSet<QString> catalogModules;
    for (const QVariant& v : m_catalog) {
        const QVariantMap row = buildPackageRow(v.toMap(), m_installedByName, m_validVariants);
        if (str(row, "name").isEmpty()) continue;
        const QString key = keyOf(row);
        if (keys.contains(key)) continue;
        keys.insert(key);
        catalogModules.insert(str(row, "moduleName"));
        rows.append(row);
    }
    // Embedded packages ship with Basecamp and are listed with it, not here.
    for (const QString& name : m_installedOrder) {
        const QVariantMap& installed = m_installedByName[name];
        if (installed.value(QStringLiteral("installType")).toString() != QLatin1String("user"))
            continue;
        const QString moduleName = installed.value(QStringLiteral("moduleName")).toString();
        if (catalogModules.contains(moduleName.isEmpty() ? name : moduleName)) continue;
        const QVariantMap row = buildLocalPackageRow(installed);
        if (keys.contains(keyOf(row))) continue;
        keys.insert(keyOf(row));
        rows.append(row);
    }

    for (QVariantMap& row : rows) {
        const QString key = keyOf(row);
        const QString name = str(row, "name");
        const auto picked = m_selectedVersionByKey.constFind(key);
        if (picked != m_selectedVersionByKey.cend()) {
            const QVariantList versions = row.value(QStringLiteral("availableVersions")).toList();
            int pick = -1;
            for (int i = 0; i < versions.size() && pick < 0; ++i)
                if (releaseKeyOf(versions.at(i).toMap()) == *picked) pick = i;
            // A release that left the catalog: the newest, until it's back.
            if (pick > 0) logos::installer::selectVersion(row, pick);
        }
        const auto icon = m_iconByKey.constFind(key);
        row.insert(QStringLiteral("iconUrl"), icon == m_iconByKey.cend() ? QString() : icon->url);
        if (icon != m_iconByKey.cend())
            row.insert(QStringLiteral("supportsFullBleedIcon"), icon->fullBleed);
        row.insert(QStringLiteral("missingDependencies"), m_missingByName.value(name));
        row.insert(QStringLiteral("isSelected"), installable(row) && m_selectedKeys.contains(key));
        row.insert(QStringLiteral("ownInstallStatus"), row.value(QStringLiteral("installStatus")));
    }

    // A current app counts what installing it would change: each package it
    // brings that is on disk, against the version its plan would bring, not
    // the newest (a range may hold one back).
    QList<InstallStatus> statuses;
    const Resolution resolution(rows, m_installedByName);
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap& row = rows.at(i);
        const bool missing = !row.value(QStringLiteral("missingDependencies")).toStringList().isEmpty();
        InstallStatus status = statusIn(row, "ownInstallStatus");
        if (m_statusIncludesDependencies) {
            if (isInstalledRow(row) && missing) {
                status = InstallStatus::NotInstalled;
            } else if (status == InstallStatus::Installed) {
                for (const Brought& b : resolution.of(i, {}, {})) {
                    if (b.row < 0 || b.row == i || b.optional || b.conflict) continue;
                    const QVariantMap& dep = rows.at(b.row);
                    if (!isInstalledRow(dep) || dep.value(QStringLiteral("availableVersions")).toList().isEmpty())
                        continue;
                    const QVariantMap v = versionAt(dep, b.version);
                    status = worseOf(status, installStatusOf(true, str(dep, "installedVersion"),
                                                             str(dep, "installedHash"),
                                                             v.value(QStringLiteral("version")).toString(),
                                                             v.value(QStringLiteral("rootHash")).toString()));
                }
            }
        }
        statuses.append(status);
    }
    for (int i = 0; i < rows.size(); ++i) {
        QVariantMap& row = rows[i];
        const bool missing = !row.value(QStringLiteral("missingDependencies")).toStringList().isEmpty();
        row.insert(QStringLiteral("installStatus"), toInt(statuses.at(i)));
        row.insert(QStringLiteral("isInstalled"),
                   isInstalledRow(row) && !(m_statusIncludesDependencies && missing));
    }
    return rows;
}

void PackageModel::rebuild()
{
    applyRows(buildRows());
}

void PackageModel::applyRows(const QList<QVariantMap>& fresh)
{
    const QList<int> selectedBefore = selectedRows();
    QStringList selectedKeysBefore;
    for (int i : selectedBefore) selectedKeysBefore.append(keyOf(m_rows[i]));

    QHash<QString, int> freshIndex;
    for (int i = 0; i < fresh.size(); ++i) freshIndex.insert(keyOf(fresh[i]), i);

    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (freshIndex.contains(keyOf(m_rows[i]))) continue;
        beginRemoveRows({}, i, i);
        m_rows.removeAt(i);
        reindex();
        endRemoveRows();
    }

    QSet<QString> present;
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString key = keyOf(m_rows[i]);
        present.insert(key);
        const QVariantMap& now = fresh.at(freshIndex.value(key));
        if (now == m_rows[i]) continue;
        m_rows[i] = now;
        emit dataChanged(index(i), index(i));
    }

    QList<QVariantMap> added;
    for (const QVariantMap& row : fresh) {
        if (!present.contains(keyOf(row))) added.append(row);
    }
    if (!added.isEmpty()) {
        beginInsertRows({}, m_rows.size(), m_rows.size() + added.size() - 1);
        m_rows.append(added);
        reindex();
        endInsertRows();
    }

    QStringList selectedKeysAfter;
    for (int i : selectedRows()) selectedKeysAfter.append(keyOf(m_rows[i]));
    if (selectedKeysAfter != selectedKeysBefore) emit selectionChanged();
}

void PackageModel::reindex()
{
    m_rowByKey.clear();
    m_rowsByName.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rowByKey.insert(keyOf(m_rows[i]), i);
        const QString name = str(m_rows[i], "name");
        const QString moduleName = str(m_rows[i], "moduleName");
        m_rowsByName.insert(name, i);
        if (!moduleName.isEmpty() && moduleName != name) m_rowsByName.insert(moduleName, i);
    }
}

void PackageModel::emitRowsNamed(const QString& name, const QList<int>& changed)
{
    for (int i : m_rowsByName.values(name)) emit dataChanged(index(i), index(i), changed);
}

}  // namespace logos::installer
