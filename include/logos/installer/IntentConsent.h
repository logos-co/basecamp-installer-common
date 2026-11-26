#pragma once

#include "Consent.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

namespace logos::installer {

// Basecamp's consent, asked through intents_module: the backend raises
// basecamp.packages.confirm_* itself, and once the request has settled pulls
// the answer with the receipt only it holds. The answer, and the plan in it,
// reach no one else: the view never sees it.
class IntentConsent : public QObject, public Consent {
    Q_OBJECT

public:
    // intents_module's request(intent, params_json) and result(handle,
    // receipt): each answers once with its JSON reply, "" when the call
    // itself failed.
    using Call = std::function<void(const QString& first, const QString& second,
                                    std::function<void(const QString& reply)> done)>;

    IntentConsent(Call request, Call result, QObject* parent = nullptr);

    void ask(const QString& intent, const QVariantMap& params,
             std::function<void(const Answer&)> answered) override;

    // intents_module's settled(handle): any requester's, so only ours count.
    void onSettled(const QString& handle);
    // intents_module started again: what the old one held is gone.
    void onRestarted();

    int waitingCount() const { return m_waiting.size(); }
    // How often a result that couldn't be read is asked for again, and how
    // long apart, before the request counts as failed.
    void setReadRetries(int retries, int intervalMs);

private:
    struct Waiting {
        QString receipt;
        std::function<void(const Answer&)> answered;
        bool reading = false;
        bool settledSinceRead = false;
        int failedReads = 0;
    };

    void read(const QString& handle);
    void end(const QString& handle, const Answer& answer);

    Call m_request;
    Call m_result;
    QHash<QString, Waiting> m_waiting;
    int m_readRetries = 3;
    int m_readRetryMs = 1000;
};

}  // namespace logos::installer
