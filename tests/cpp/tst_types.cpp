// Types.js says what Types.h says, and stage names round-trip.

#include "check.h"

#include <logos/installer/Types.h>

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>

using namespace logos::installer;

namespace {

QJSValue typesJs()
{
    // Gone with the application, not after it: a static engine outlives
    // QCoreApplication and aborts at exit on macOS.
    static QJSEngine* engine = new QJSEngine(QCoreApplication::instance());
    QFile f(QStringLiteral(INSTALLER_QML_DIR "/Types.js"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QString code = QString::fromUtf8(f.readAll());
    code.replace(QStringLiteral(".pragma library"), QString());
    engine->evaluate(code);
    return engine->globalObject();
}

int js(const char* group, const char* name)
{
    const QJSValue v = typesJs().property(QLatin1String(group)).property(QLatin1String(name));
    return v.isNumber() ? v.toInt() : -1;
}

int jsCount(const char* group)
{
    const QJSValue keys = typesJs().property(QStringLiteral("Object")).property(QStringLiteral("keys"))
        .call({typesJs().property(QLatin1String(group))});
    return keys.property(QStringLiteral("length")).toInt();
}

}  // namespace

TEST(install_status_matches) {
    CHECK_EQ(jsCount("InstallStatus"), 7);
    CHECK_EQ(js("InstallStatus", "NotInstalled"), toInt(InstallStatus::NotInstalled));
    CHECK_EQ(js("InstallStatus", "Installing"), toInt(InstallStatus::Installing));
    CHECK_EQ(js("InstallStatus", "Installed"), toInt(InstallStatus::Installed));
    CHECK_EQ(js("InstallStatus", "Failed"), toInt(InstallStatus::Failed));
    CHECK_EQ(js("InstallStatus", "UpgradeAvailable"), toInt(InstallStatus::UpgradeAvailable));
    CHECK_EQ(js("InstallStatus", "DowngradeAvailable"), toInt(InstallStatus::DowngradeAvailable));
    CHECK_EQ(js("InstallStatus", "DifferentHash"), toInt(InstallStatus::DifferentHash));
}

TEST(install_stage_matches) {
    CHECK_EQ(jsCount("InstallStage"), 7);
    CHECK_EQ(js("InstallStage", "None"), toInt(InstallStage::None));
    CHECK_EQ(js("InstallStage", "Downloading"), toInt(InstallStage::Downloading));
    CHECK_EQ(js("InstallStage", "Queued"), toInt(InstallStage::Queued));
    CHECK_EQ(js("InstallStage", "Installing"), toInt(InstallStage::Installing));
    CHECK_EQ(js("InstallStage", "Installed"), toInt(InstallStage::Installed));
    CHECK_EQ(js("InstallStage", "Failed"), toInt(InstallStage::Failed));
    CHECK_EQ(js("InstallStage", "Downloaded"), toInt(InstallStage::Downloaded));
}

TEST(row_action_matches) {
    CHECK_EQ(jsCount("RowAction"), 7);
    CHECK_EQ(js("RowAction", "Install"), toInt(RowAction::Install));
    CHECK_EQ(js("RowAction", "Upgrade"), toInt(RowAction::Upgrade));
    CHECK_EQ(js("RowAction", "Downgrade"), toInt(RowAction::Downgrade));
    CHECK_EQ(js("RowAction", "Reinstall"), toInt(RowAction::Reinstall));
    CHECK_EQ(js("RowAction", "Retry"), toInt(RowAction::Retry));
    CHECK_EQ(js("RowAction", "NotAvailable"), toInt(RowAction::NotAvailable));
    CHECK_EQ(js("RowAction", "NoOp"), toInt(RowAction::NoOp));
}

TEST(not_available_reason_matches) {
    CHECK_EQ(jsCount("NotAvailableReason"), 6);
    CHECK_EQ(js("NotAvailableReason", "Available"), toInt(NotAvailableReason::Available));
    CHECK_EQ(js("NotAvailableReason", "NoVariantsPublished"), toInt(NotAvailableReason::NoVariantsPublished));
    CHECK_EQ(js("NotAvailableReason", "BuildFlavorMismatch"), toInt(NotAvailableReason::BuildFlavorMismatch));
    CHECK_EQ(js("NotAvailableReason", "PlatformMismatch"), toInt(NotAvailableReason::PlatformMismatch));
    CHECK_EQ(js("NotAvailableReason", "NotOnLogosStorage"), toInt(NotAvailableReason::NotOnLogosStorage));
    CHECK_EQ(js("NotAvailableReason", "NotOverHttp"), toInt(NotAvailableReason::NotOverHttp));
}

TEST(download_source_matches) {
    CHECK_EQ(jsCount("DownloadSource"), 5);
    CHECK_EQ(js("DownloadSource", "NoSource"), toInt(DownloadSource::NoSource));
    CHECK_EQ(js("DownloadSource", "Builtin"), toInt(DownloadSource::Builtin));
    CHECK_EQ(js("DownloadSource", "GitHub"), toInt(DownloadSource::GitHub));
    CHECK_EQ(js("DownloadSource", "Storage"), toInt(DownloadSource::Storage));
    CHECK_EQ(js("DownloadSource", "LocalFile"), toInt(DownloadSource::LocalFile));
}

TEST(stage_names_round_trip) {
    for (int i = 0; i <= toInt(InstallStage::Downloaded); ++i) {
        const auto s = static_cast<InstallStage>(i);
        CHECK_EQ(stageFromName(stageName(s)), s);
    }
    CHECK_EQ(stageName(InstallStage::Downloading), QStringLiteral("downloading"));
    CHECK_EQ(stageFromName(QStringLiteral("nonsense")), InstallStage::None);
}

CHECK_MAIN()
