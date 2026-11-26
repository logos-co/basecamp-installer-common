#include "logos/installer/IntentConsent.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

namespace logos::installer {

namespace {

QVariantMap parse(const QString& reply)
{
    return QJsonDocument::fromJson(reply.toUtf8()).object().toVariantMap();
}

QString orFailed(const QString& error)
{
    return error.isEmpty() ? QStringLiteral("failed") : error;
}

}  // namespace

IntentConsent::IntentConsent(Call request, Call result, QObject* parent)
    : QObject(parent)
    , m_request(std::move(request))
    , m_result(std::move(result))
{
}

void IntentConsent::setReadRetries(int retries, int intervalMs)
{
    m_readRetries = retries;
    m_readRetryMs = intervalMs;
}

void IntentConsent::ask(const QString& intent, const QVariantMap& params,
                        std::function<void(const Answer&)> answered)
{
    const QString json = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(params)).toJson(QJsonDocument::Compact));
    QPointer<IntentConsent> self(this);
    m_request(intent, json, [self, answered](const QString& reply) {
        if (!self) return;
        const QVariantMap r = parse(reply);
        const QString handle = r.value(QStringLiteral("handle")).toString();
        const QString receipt = r.value(QStringLiteral("receipt")).toString();
        if (!r.value(QStringLiteral("ok")).toBool() || handle.isEmpty() || receipt.isEmpty()) {
            answered({false, {}, orFailed(r.value(QStringLiteral("error")).toString())});
            return;
        }
        self->m_waiting.insert(handle, Waiting{receipt, answered});
        // It may have settled before we were listening for it.
        self->read(handle);
    });
}

void IntentConsent::onSettled(const QString& handle)
{
    const auto it = m_waiting.find(handle);
    if (it == m_waiting.end()) return;
    if (it->reading) {
        it->settledSinceRead = true;
        return;
    }
    read(handle);
}

void IntentConsent::onRestarted()
{
    const QStringList handles = m_waiting.keys();
    for (const QString& handle : handles) end(handle, {false, {}, QStringLiteral("failed")});
}

void IntentConsent::read(const QString& handle)
{
    const auto it = m_waiting.find(handle);
    if (it == m_waiting.end() || it->reading) return;
    it->reading = true;
    it->settledSinceRead = false;
    QPointer<IntentConsent> self(this);
    m_result(handle, it->receipt, [self, handle](const QString& reply) {
        if (!self) return;
        const auto w = self->m_waiting.find(handle);
        if (w == self->m_waiting.end()) return;
        w->reading = false;

        if (reply.isEmpty()) {
            // The call failed, not the request: ask again, a few times.
            if (++w->failedReads > self->m_readRetries) {
                self->end(handle, {false, {}, QStringLiteral("failed")});
                return;
            }
            QTimer::singleShot(self->m_readRetryMs, self, [self, handle] {
                if (self) self->read(handle);
            });
            return;
        }
        w->failedReads = 0;

        const QVariantMap r = parse(reply);
        if (!r.value(QStringLiteral("ok")).toBool()) {
            // unknown_handle: intents_module started again.
            self->end(handle, {false, {}, orFailed(r.value(QStringLiteral("error")).toString())});
            return;
        }
        if (r.value(QStringLiteral("state")).toString() != QLatin1String("settled")) {
            if (w->settledSinceRead) {
                self->read(handle);
                return;
            }
            if (w->pollQueued) return;
            w->pollQueued = true;
            QTimer::singleShot(self->m_pendingPollMs, self, [self, handle] {
                if (!self) return;
                const auto still = self->m_waiting.find(handle);
                if (still == self->m_waiting.end()) return;
                still->pollQueued = false;
                self->read(handle);
            });
            return;
        }
        const QVariantMap result = r.value(QStringLiteral("result")).toMap();
        Answer answer;
        answer.approved = result.value(QStringLiteral("ok")).toBool();
        answer.data = result.value(QStringLiteral("data")).toMap();
        if (!answer.approved) answer.error = orFailed(result.value(QStringLiteral("error")).toString());
        self->end(handle, answer);
    });
}

void IntentConsent::end(const QString& handle, const Answer& answer)
{
    const auto it = m_waiting.find(handle);
    if (it == m_waiting.end()) return;
    const auto answered = it->answered;
    m_waiting.erase(it);
    if (answered) answered(answer);
}

}  // namespace logos::installer
