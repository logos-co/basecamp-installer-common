#pragma once

#include <QString>
#include <QVariantMap>

#include <functional>

namespace logos::installer {

// Asking Basecamp, and through it the person, to approve a change. The
// backend raises the intent itself (intents_module's request, then its result
// once settled), so the answer, and the plan in it, comes back to the backend
// alone and never through its screen.
class Consent {
public:
    struct Answer {
        bool approved = false;
        // Approved: what may be done ({plan, optionalPackages} or {file}; for
        // an uninstall, {names}).
        QVariantMap data;
        // Not approved: the intent's error code ("cancelled", "failed",
        // "unavailable", "timeout", …).
        QString error;
    };

    static constexpr const char* kConfirmInstall = "basecamp.packages.confirm_install";
    static constexpr const char* kConfirmUpgrade = "basecamp.packages.confirm_upgrade";
    static constexpr const char* kConfirmUninstall = "basecamp.packages.confirm_uninstall";

    virtual ~Consent() = default;

    // Answers once.
    virtual void ask(const QString& intent, const QVariantMap& params,
                     std::function<void(const Answer&)> answered) = 0;
};

}  // namespace logos::installer
