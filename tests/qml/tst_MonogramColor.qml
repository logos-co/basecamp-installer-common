import QtQuick
import QtTest
import "../../qml/Installer/MonogramColor.js" as MonogramColor

TestCase {
    name: "MonogramColor"

    function test_the_same_app_gets_the_same_colour() {
        verify(Qt.colorEqual(MonogramColor.colorForApp("wallet_ui"),
                             MonogramColor.colorForApp("wallet_ui")))
        verify(!Qt.colorEqual(MonogramColor.colorForApp("wallet_ui"),
                              MonogramColor.colorForApp("chat_ui")))
    }

    // The value Basecamp's AppColors gave, so a tile keeps its colour.
    function test_unchanged_from_basecamp() {
        compare(MonogramColor.hash32("wallet_ui"), 3809081206)
        const c = MonogramColor.colorForApp("wallet_ui")
        fuzzyCompare(c.hslHue, 0.548, 0.005)
        fuzzyCompare(c.hslSaturation, 0.38, 0.01)
        fuzzyCompare(c.hslLightness, 0.22, 0.01)
    }

    function test_no_name_no_colour() {
        compare(MonogramColor.colorForApp(""), null)
    }
}
