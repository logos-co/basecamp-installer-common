import QtQuick
import QtTest
import "../../qml/Installer/Types.js" as T
import "../../qml/Installer/Stages.js" as Stages

// The words for what an install is doing and what a row's action does.
TestCase {
    name: "Stages"

    function test_stage_words() {
        compare(Stages.stageWord(T.InstallStage.None), "")
        compare(Stages.stageWord(T.InstallStage.Downloading), "Downloading…")
        compare(Stages.stageWord(T.InstallStage.Downloaded), "Downloaded")
        compare(Stages.stageWord(T.InstallStage.Queued), "Queued")
        compare(Stages.stageWord(T.InstallStage.Installing), "Installing…")
        compare(Stages.stageWord(T.InstallStage.Installed), "Installed")
        compare(Stages.stageWord(T.InstallStage.Failed), "Failed")
    }

    function test_bytes_while_they_move() {
        compare(Stages.progressWord({ transport: "http", received: 1024, total: 4096 }),
                "1.0 / 4.0 KB")
    }

    function test_waiting_behind_another_install_is_queued() {
        compare(Stages.progressWord({ queued: true, transport: "http", received: 0, total: 0 }),
                "Queued")
        // Its own download hasn't started: no transport yet.
        compare(Stages.progressWord({ transport: "", received: 0, total: 0 }), "Queued")
    }

    function test_logos_with_no_byte_is_finding_peers() {
        compare(Stages.progressWord({ transport: "logos", received: 0, total: 4096 }),
                "Finding peers…")
    }

    function test_finding_peers_has_its_own_word() {
        compare(Stages.findingPeersWord(), "Finding peers…")
    }

    function test_http_with_no_byte_is_downloading() {
        compare(Stages.progressWord({ transport: "http", received: 0, total: 4096 }),
                "Downloading…")
    }

    // The bytes are all there; what's left has no byte count.
    function test_after_the_download_it_is_installing() {
        compare(Stages.progressWord({ transport: "logos", received: 4096, total: 4096 }),
                "Installing…")
    }

    function test_action_words() {
        compare(Stages.actionWord(T.RowAction.Install), "Install")
        compare(Stages.actionWord(T.RowAction.Upgrade), "Upgrade")
        compare(Stages.actionWord(T.RowAction.Downgrade), "Downgrade")
        compare(Stages.actionWord(T.RowAction.Reinstall), "Reinstall")
        compare(Stages.actionWord(T.RowAction.Retry), "Retry")
        compare(Stages.actionWord(T.RowAction.NotAvailable), "Not available")
        compare(Stages.actionWord(T.RowAction.NoOp), "Installed")
    }

    function test_transition_text() {
        compare(Stages.transitionText("", ""), "")
        compare(Stages.transitionText("", "1.2.0"), "Installs v1.2.0")
        compare(Stages.transitionText("1.2.0", "1.2.0"), "v1.2.0 installed")
        compare(Stages.transitionText("1.2.0", ""), "v1.2.0 installed")
        compare(Stages.transitionText("1.1.0", "1.2.0"), "v1.1.0 → v1.2.0")
    }

    // One pass: a version that reads like a placeholder stays as it is.
    function test_a_value_is_never_filled_into() {
        compare(Stages.transitionText("%2", "1.0"), "v%2 → v1.0")
        compare(Stages.fill("%1 and %2", "%2", "x"), "%2 and x")
    }
}
