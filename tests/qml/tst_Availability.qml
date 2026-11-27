import QtQuick
import QtTest
import "../../qml/Installer/Types.js" as T
import "../../qml/Installer/Availability.js" as Availability

TestCase {
    name: "Availability"

    function test_each_reason_has_its_own_words() {
        const R = T.NotAvailableReason
        const seen = {}
        for (const r of [R.NoVariantsPublished, R.BuildFlavorMismatch, R.PlatformMismatch,
                         R.NotOnLogosStorage, R.NotOverHttp]) {
            const text = Availability.notAvailableText(r)
            verify(text.length > 0)
            verify(!seen[text], text)
            seen[text] = true
        }
        compare(Availability.notAvailableText(R.Available), "Not available")
    }

    function test_nothing_for_this_platform_names_it() {
        compare(Availability.nothingForThisPlatform("linux-x86_64").subtitle,
                "No configured repository publishes a build for linux-x86_64.")
        compare(Availability.nothingForThisPlatform("").subtitle,
                "No configured repository publishes a build for this platform.")
    }

    function test_dev_build_is_told_by_its_suffix() {
        verify(Availability.isDevBuild("linux-x86_64-dev"))
        verify(Availability.isDevBuild("darwin-arm64-dev"))
        verify(!Availability.isDevBuild("linux-x86_64"))
        verify(!Availability.isDevBuild(""))
        verify(!Availability.isDevBuild(undefined))
    }

    // logos-basecamp#328: a dev build blamed the package or the repositories.
    function test_dev_build_names_itself_not_the_package() {
        const R = T.NotAvailableReason
        compare(Availability.notAvailableText(R.BuildFlavorMismatch, "linux-x86_64-dev"),
                "Needs a portable build of Basecamp.")
        compare(Availability.notAvailableText(R.BuildFlavorMismatch, "linux-x86_64"),
                "Not available for this build flavor (dev / portable / release).")
        compare(Availability.notAvailableText(R.PlatformMismatch, "linux-x86_64-dev"),
                "Not available for this platform.")

        const empty = Availability.nothingForThisPlatform("linux-x86_64-dev")
        compare(empty.title, "Nothing installable on a dev build")
        verify(empty.subtitle.indexOf("repository") < 0, empty.subtitle)
        verify(empty.subtitle.indexOf("x86_64") < 0, empty.subtitle)
    }

    function test_dev_build_notice_only_on_a_dev_build() {
        compare(Availability.devBuildNotice("linux-x86_64"), null)
        compare(Availability.devBuildNotice(""), null)
        const notice = Availability.devBuildNotice("linux-x86_64-dev")
        compare(notice.severity, "info")
        verify(notice.title.length > 0)
        verify(notice.message.indexOf(Availability.portableBuildsUrl) >= 0, notice.message)
    }
}
