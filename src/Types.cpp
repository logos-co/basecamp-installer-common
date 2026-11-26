#include "logos/installer/Types.h"

namespace logos::installer {

QString stageName(InstallStage stage)
{
    switch (stage) {
    case InstallStage::Downloading: return QStringLiteral("downloading");
    case InstallStage::Downloaded:  return QStringLiteral("downloaded");
    case InstallStage::Queued:      return QStringLiteral("queued");
    case InstallStage::Installing:  return QStringLiteral("installing");
    case InstallStage::Installed:   return QStringLiteral("installed");
    case InstallStage::Failed:      return QStringLiteral("failed");
    case InstallStage::None:        return QString();
    }
    return QString();
}

InstallStage stageFromName(const QString& name)
{
    for (InstallStage s : {InstallStage::Downloading, InstallStage::Downloaded,
                           InstallStage::Queued, InstallStage::Installing,
                           InstallStage::Installed, InstallStage::Failed}) {
        if (stageName(s) == name) return s;
    }
    return InstallStage::None;
}

}  // namespace logos::installer
