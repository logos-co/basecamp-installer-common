// Basecamp's consent through intents_module: raised by the backend, and
// the answer pulled with the receipt once the request has settled.

#include "check.h"

#include "logos/installer/IntentConsent.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

using logos::installer::IntentConsent;
using logos::installer::Consent;

namespace {

QString json(const QVariantMap& m)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact));
}

QVariantMap parse(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object().toVariantMap();
}

// intents_module: hands out handles, and holds each request's result until
// settle() decides it. A result is given only for the right receipt.
struct Intents {
    struct Request { QString intent; QVariantMap params; };
    QList<Request> requests;
    QStringList reads;
    QString refuseWith;          // request answers {ok:false, error}
    bool dropRequestReply = false;
    int failReads = 0;           // result calls that fail, the next ones
    QHash<QString, QVariantMap> settled;
    int next = 0;

    IntentConsent::Call request()
    {
        return [this](const QString& intent, const QString& params,
                      std::function<void(const QString&)> done) {
            requests.append({intent, parse(params)});
            if (dropRequestReply) return done({});
            if (!refuseWith.isEmpty()) return done(json({{"ok", false}, {"error", refuseWith}}));
            const QString n = QString::number(++next);
            done(json({{"ok", true}, {"handle", "h" + n}, {"receipt", "r" + n}}));
        };
    }
    IntentConsent::Call result()
    {
        return [this](const QString& handle, const QString& receipt,
                      std::function<void(const QString&)> done) {
            reads.append(handle);
            if (failReads > 0) {
                --failReads;
                return done({});
            }
            if (receipt != "r" + handle.mid(1)) return done(json({{"ok", false}, {"error", "unknown_handle"}}));
            if (!settled.contains(handle)) return done(json({{"ok", true}, {"state", "pending"}}));
            done(json({{"ok", true}, {"state", "settled"}, {"result", settled.value(handle)}}));
        };
    }
};

struct Asked {
    bool answered = false;
    Consent::Answer answer;
    std::function<void(const Consent::Answer&)> callback()
    {
        return [this](const Consent::Answer& a) {
            answered = true;
            answer = a;
        };
    }
};

const QVariantMap kPlan{{"plan", QVariantList{QVariantMap{{"name", "wallet"}, {"version", "1.0.0"}}}}};

}  // namespace

TEST(asks_basecamp_with_the_intent_and_its_params) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {{"name", "wallet"}, {"version", "1.0.0"}}, asked.callback());
    CHECK_EQ(intents.requests.size(), 1);
    CHECK_EQ(intents.requests.first().intent, QString(Consent::kConfirmInstall));
    CHECK_EQ(intents.requests.first().params.value("name").toString(), QStringLiteral("wallet"));
    // Read once at once, in case it settled already; still pending.
    CHECK_EQ(intents.reads, QStringList{"h1"});
    CHECK(!asked.answered);
    CHECK_EQ(consent.waitingCount(), 1);
}

TEST(an_approval_comes_back_with_its_plan_once_settled) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {{"name", "wallet"}}, asked.callback());
    intents.settled.insert("h1", {{"ok", true}, {"data", kPlan}});
    consent.onSettled("h1");
    CHECK(asked.answered);
    CHECK(asked.answer.approved);
    CHECK_EQ(asked.answer.data, kPlan);
    CHECK_EQ(consent.waitingCount(), 0);
}

TEST(a_refusal_says_why) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmUninstall, {{"names", QStringList{"wallet"}}}, asked.callback());
    intents.settled.insert("h1", {{"ok", false}, {"error", "cancelled"}});
    consent.onSettled("h1");
    CHECK(asked.answered);
    CHECK(!asked.answer.approved);
    CHECK_EQ(asked.answer.error, QStringLiteral("cancelled"));
    CHECK(asked.answer.data.isEmpty());
}

TEST(a_request_intents_module_refuses_is_not_approved) {
    Intents intents;
    intents.refuseWith = "not_declared";
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    CHECK(asked.answered);
    CHECK(!asked.answer.approved);
    CHECK_EQ(asked.answer.error, QStringLiteral("not_declared"));
    CHECK(intents.reads.isEmpty());
}

TEST(a_request_with_no_reply_fails) {
    Intents intents;
    intents.dropRequestReply = true;
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    CHECK(asked.answered);
    CHECK(!asked.answer.approved);
    CHECK_EQ(asked.answer.error, QStringLiteral("failed"));
}

TEST(settled_before_asking_is_read_at_once) {
    Intents intents;
    intents.settled.insert("h1", {{"ok", true}, {"data", kPlan}});
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    CHECK(asked.answered);
    CHECK(asked.answer.approved);
}

TEST(another_requesters_settled_is_ignored) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    consent.onSettled("h99");
    CHECK_EQ(intents.reads, QStringList{"h1"});
    CHECK(!asked.answered);
}

TEST(each_answer_reaches_its_own_request) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    Asked first, second;
    consent.ask(Consent::kConfirmInstall, {{"name", "a"}}, first.callback());
    consent.ask(Consent::kConfirmInstall, {{"name", "b"}}, second.callback());
    intents.settled.insert("h2", {{"ok", false}, {"error", "cancelled"}});
    consent.onSettled("h2");
    CHECK(!first.answered);
    CHECK(second.answered);
    CHECK_EQ(second.answer.error, QStringLiteral("cancelled"));
    intents.settled.insert("h1", {{"ok", true}, {"data", kPlan}});
    consent.onSettled("h1");
    CHECK(first.answered);
    CHECK(first.answer.approved);
}

TEST(a_restart_fails_what_was_waiting) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    Asked first, second;
    consent.ask(Consent::kConfirmInstall, {}, first.callback());
    consent.ask(Consent::kConfirmUpgrade, {}, second.callback());
    consent.onRestarted();
    CHECK(first.answered && second.answered);
    CHECK(!first.answer.approved && !second.answer.approved);
    CHECK_EQ(first.answer.error, QStringLiteral("failed"));
    CHECK_EQ(consent.waitingCount(), 0);
    // A late settled finds nothing to answer.
    consent.onSettled("h1");
    CHECK_EQ(intents.reads.size(), 2);
}

TEST(an_unknown_handle_fails) {
    Intents intents;
    IntentConsent consent(intents.request(), [&intents](const QString& handle, const QString&,
                                                        std::function<void(const QString&)> done) {
        intents.reads.append(handle);
        done(json({{"ok", false}, {"error", "unknown_handle"}}));
    });
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    CHECK(asked.answered);
    CHECK_EQ(asked.answer.error, QStringLiteral("unknown_handle"));
}

TEST(a_read_that_fails_is_tried_again_then_fails) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    consent.setReadRetries(2, 5);
    Asked asked;
    intents.failReads = 1;
    intents.settled.insert("h1", {{"ok", true}, {"data", kPlan}});
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    CHECK(!asked.answered);
    SPIN(50);
    CHECK(asked.answered);
    CHECK(asked.answer.approved);

    Asked again;
    intents.failReads = 10;
    consent.ask(Consent::kConfirmInstall, {}, again.callback());
    SPIN(100);
    CHECK(again.answered);
    CHECK(!again.answer.approved);
    CHECK_EQ(again.answer.error, QStringLiteral("failed"));
}

TEST(settled_while_reading_is_read_again) {
    Intents intents;
    std::function<void(const QString&)> held;
    IntentConsent consent(intents.request(), [&](const QString& handle, const QString& receipt,
                                                 std::function<void(const QString&)> done) {
        if (!held) {
            intents.reads.append(handle);
            held = done;  // the first read is slow
            return;
        }
        intents.result()(handle, receipt, done);
    });
    Asked asked;
    consent.ask(Consent::kConfirmInstall, {}, asked.callback());
    intents.settled.insert("h1", {{"ok", true}, {"data", kPlan}});
    consent.onSettled("h1");                     // while the first read is out
    CHECK_EQ(intents.reads.size(), 1);
    held(json({{"ok", true}, {"state", "pending"}}));  // it read before the settle
    CHECK_EQ(intents.reads.size(), 2);
    CHECK(asked.answered);
    CHECK(asked.answer.approved);
}

// A settled event that never came: a pending request is read again, slowly.
TEST(a_lost_settled_event_is_caught_by_reading_again) {
    Intents intents;
    IntentConsent consent(intents.request(), intents.result());
    consent.setPendingPollInterval(10);
    Asked asked;
    consent.ask("basecamp.packages.confirm_install", {}, asked.callback());
    CHECK(!asked.answered);
    SPIN(25);
    CHECK(!asked.answered);
    const int readsWhilePending = intents.reads.size();
    CHECK(readsWhilePending >= 2);
    CHECK(readsWhilePending <= 4);
    intents.settled.insert("h1", {{"ok", true}, {"data", kPlan}});
    SPIN(25);
    CHECK(asked.answered);
    CHECK(asked.answer.approved);
    CHECK_EQ(consent.waitingCount(), 0);
    const int reads = intents.reads.size();
    SPIN(30);
    CHECK_EQ(intents.reads.size(), reads);
}

CHECK_MAIN()
