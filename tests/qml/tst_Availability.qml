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
}
