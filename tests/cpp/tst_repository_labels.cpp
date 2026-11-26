// The label each repository is shown under.

#include "check.h"

#include <logos/installer/RepositoryLabels.h>

using namespace logos::installer;

namespace {

QVariantMap repo(const QString& url, const QString& name, const QString& owner = {},
                 const QString& repoName = {}, const QString& host = {})
{
    return {{"url", url}, {"displayName", name}, {"sourceOwner", owner},
            {"sourceRepo", repoName}, {"sourceHost", host}};
}

}  // namespace

TEST(a_unique_name_is_its_own_label) {
    const QVariantMap labels = repositoryLabels({repo("u1", "Logos Official", "logos-co", "r")});
    CHECK_EQ(labels.value("u1").toString(), QStringLiteral("Logos Official"));
}

TEST(a_shared_name_is_qualified_by_its_owner) {
    const QVariantMap labels = repositoryLabels({repo("u1", "Apps", "alice", "r"),
                                                 repo("u2", "apps ", "bob", "r")});
    CHECK_EQ(labels.value("u1").toString(), QStringLiteral("Apps (alice)"));
    CHECK_EQ(labels.value("u2").toString(), QStringLiteral("apps (bob)"));
}

TEST(one_owner_with_two_of_a_name_is_qualified_by_repository) {
    const QVariantMap labels = repositoryLabels({repo("u1", "Apps", "Alice", "one"),
                                                 repo("u2", "Apps", "alice", "two")});
    CHECK_EQ(labels.value("u1").toString(), QStringLiteral("Apps (Alice/one)"));
    CHECK_EQ(labels.value("u2").toString(), QStringLiteral("Apps (alice/two)"));
}

TEST(off_github_the_host_qualifies) {
    const QVariantMap labels = repositoryLabels({repo("u1", "Apps", {}, {}, "a.example"),
                                                 repo("u2", "Apps", {}, {}, "b.example")});
    CHECK_EQ(labels.value("u1").toString(), QStringLiteral("Apps (a.example)"));
}

TEST(without_source_fields_the_url_qualifies) {
    const QVariantMap labels = repositoryLabels({repo("https://a", "Apps"), repo("https://b", "Apps")});
    CHECK_EQ(labels.value("https://a").toString(), QStringLiteral("Apps (https://a)"));
}

TEST(a_nameless_repository_is_labelled_by_its_source) {
    const QVariantMap labels = repositoryLabels({repo("u1", "", "alice", "apps")});
    CHECK_EQ(labels.value("u1").toString(), QStringLiteral("alice/apps"));
}

TEST(a_repository_without_a_url_is_left_out) {
    CHECK(repositoryLabels({repo("", "Apps")}).isEmpty());
}

TEST(sections_list_the_default_first_then_by_label_then_local) {
    QVariantMap def = repo("https://z", "Zeta Official", "logos-co", "z");
    def["isDefault"] = true;
    const QStringList order = sourceOrder({repo("https://b", "beta", "o", "b"), def,
                                           repo("https://a", "Alpha", "o", "a")});
    CHECK_EQ(order, (QStringList{"https://z", "https://a", "https://b", "local"}));
}

// The default is the registry's flag: a repository named like it isn't.
TEST(a_repository_named_like_the_default_is_not_the_default) {
    const QStringList order = sourceOrder({repo("https://fake", "Logos Official", "x", "y"),
                                           repo("https://a", "Alpha", "o", "a")});
    CHECK_EQ(order, (QStringList{"https://a", "https://fake", "local"}));
}

CHECK_MAIN()
