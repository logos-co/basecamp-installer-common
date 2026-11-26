#include "logos/installer/RepositoryLabels.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>

namespace logos::installer {

namespace {

QString nameKey(const QVariantMap& r)
{
    return r.value(QStringLiteral("displayName")).toString().trimmed().toLower();
}

QString nameAndOwnerKey(const QVariantMap& r)
{
    return nameKey(r) + QChar(0x01)
         + r.value(QStringLiteral("sourceOwner")).toString().trimmed().toLower();
}

QString labelFor(const QVariantMap& r, int sharingName, int sharingOwner)
{
    QString label = r.value(QStringLiteral("displayName")).toString().trimmed();
    if (label.isEmpty()) label = r.value(QStringLiteral("name")).toString().trimmed();

    const QString owner = r.value(QStringLiteral("sourceOwner")).toString();
    const QString repo  = r.value(QStringLiteral("sourceRepo")).toString();
    // Off GitHub there is no owner, so the host says where it is; a
    // downloader older than the source* fields has neither, so the URL.
    QString shortSource = owner.isEmpty() ? r.value(QStringLiteral("sourceHost")).toString() : owner;
    QString longSource = owner.isEmpty() || repo.isEmpty()
        ? shortSource : owner + QLatin1Char('/') + repo;
    if (shortSource.isEmpty()) shortSource = r.value(QStringLiteral("url")).toString();
    if (longSource.isEmpty()) longSource = shortSource;

    if (label.isEmpty()) return longSource;
    if (sharingName <= 1 || shortSource.isEmpty()) return label;
    return QCoreApplication::translate("logos::installer", "%1 (%2)")
        .arg(label, sharingOwner > 1 ? longSource : shortSource);
}

}  // namespace

QVariantMap repositoryLabels(const QVariantList& repositories)
{
    // A label depends on how many other repositories share the name, so count
    // first.
    QHash<QString, int> byName, byNameAndOwner;
    for (const QVariant& v : repositories) {
        const QVariantMap r = v.toMap();
        byName[nameKey(r)]++;
        byNameAndOwner[nameAndOwnerKey(r)]++;
    }
    QVariantMap labels;
    for (const QVariant& v : repositories) {
        const QVariantMap r = v.toMap();
        const QString url = r.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) continue;
        labels.insert(url, labelFor(r, byName.value(nameKey(r)),
                                    byNameAndOwner.value(nameAndOwnerKey(r))));
    }
    return labels;
}

QString localRepositoryLabel()
{
    return QCoreApplication::translate("logos::installer", "local");
}

QStringList sourceOrder(const QVariantList& repositories)
{
    const QVariantMap labels = repositoryLabels(repositories);
    QStringList defaults, others;
    for (const QVariant& v : repositories) {
        const QVariantMap r = v.toMap();
        const QString url = r.value(QStringLiteral("url")).toString();
        if (url.isEmpty() || defaults.contains(url) || others.contains(url)) continue;
        (r.value(QStringLiteral("isDefault")).toBool() ? defaults : others).append(url);
    }
    std::stable_sort(others.begin(), others.end(), [&](const QString& a, const QString& b) {
        const int c = labels.value(a).toString().compare(labels.value(b).toString(),
                                                         Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return defaults + others + QStringList{QStringLiteral("local")};
}

}  // namespace logos::installer
