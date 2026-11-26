import QtQuick
import QtTest
import "../../qml/Installer/Downloads.js" as Downloads

// How a download is described, and the notice above a screen that downloads.
TestCase {
    name: "Downloads"

    function test_bytes() {
        compare(Downloads.bytes(0), "0 B")
        compare(Downloads.bytes(NaN), "0 B")
        compare(Downloads.bytes(512), "512 B")
        compare(Downloads.bytes(1536), "1.5 KB")
        compare(Downloads.bytes(5 * 1024 * 1024), "5.0 MB")
        compare(Downloads.bytes(3 * 1024 * 1024 * 1024), "3.0 GB")
    }

    // Both sides in the total's unit: the unit prints once.
    function test_progress_label() {
        compare(Downloads.progressLabel(12.3 * 1024 * 1024, 45.2 * 1024 * 1024), "12.3 / 45.2 MB")
        compare(Downloads.progressLabel(500, 900), "500 / 900 B")
        compare(Downloads.progressLabel(2048, 0), "2.0 KB")
    }

    function test_fraction_never_nan() {
        compare(Downloads.fraction(5, 0), 0)
        compare(Downloads.fraction(5, 10), 0.5)
        compare(Downloads.fraction(20, 10), 1)
        compare(Downloads.percent(1, 3), "33%")
    }

    function test_transferring_only_while_bytes_move() {
        verify(!Downloads.transferring(0, 10))
        verify(Downloads.transferring(5, 10))
        verify(!Downloads.transferring(10, 10))
        verify(!Downloads.transferring(5, 0))
    }

    function test_the_widest_label_is_at_least_as_wide_as_a_real_one() {
        verify(Downloads.widestProgressLabel.length
               >= Downloads.progressLabel(999.9 * 1024 * 1024, 999.9 * 1024 * 1024).length)
    }

    function test_logos_explains_it_may_be_slower() {
        compare(Downloads.transportCaption("logos", false),
                "via Logos Storage · peer-to-peer, may be slower")
    }

    function test_github() {
        compare(Downloads.transportCaption("http", false), "via GitHub")
    }

    function test_a_fallback_says_why_it_is_on_github() {
        compare(Downloads.transportCaption("http", true),
                "via GitHub · Logos Storage didn't have it")
    }

    function test_unknown_transport_has_no_caption_or_icon() {
        compare(Downloads.transportCaption("", false), "")
        compare(Downloads.transportCaption(undefined, false), "")
        compare(Downloads.transportIconName(""), "")
        compare(Downloads.transportIconName("logos"), "logosStorage")
        compare(Downloads.transportIconName("http"), "github")
    }

    function test_logos_without_a_byte_is_finding_peers() {
        verify(Downloads.findingPeers("logos", 0))
        verify(!Downloads.findingPeers("logos", 1024))
        verify(!Downloads.findingPeers("http", 0))
        verify(!Downloads.findingPeers("", 0))
    }

    function notice(online, source, storage, showsFallback) {
        return Downloads.downloadNotice(online, source, storage, showsFallback)
    }

    function test_offline_says_nothing_here() {
        for (const source of ["any", "logos", "http"])
            compare(notice(false, source, "failed", true), null, source)
    }

    function test_github_only_never_mentions_storage() {
        compare(notice(true, "http", "failed", true), null)
        compare(notice(true, "http", "off", true), null)
    }

    function test_storage_only_and_down_says_why_and_where_to_fix_it() {
        const n = notice(true, "logos", "failed", false)
        compare(n.severity, "error")
        compare(n.title, "Logos Storage isn't running")
        verify(n.message.indexOf("Settings → Repositories") >= 0)
    }

    function test_storage_only_while_starting() {
        compare(notice(true, "logos", "starting", false).title, "Starting Logos Storage…")
        compare(notice(true, "logos", "waiting", false).title, "Starting Logos Storage…")
    }

    // GitHub covers for it: news only where each download's source is shown.
    function test_both_sources_with_storage_down() {
        compare(notice(true, "any", "failed", false), null)
        const n = notice(true, "any", "failed", true)
        compare(n.severity, "info")
        compare(n.title, "Logos Storage isn't running")
    }

    function test_nothing_to_say_when_storage_runs_or_is_idle() {
        compare(notice(true, "logos", "running", true), null)
        compare(notice(true, "any", "running", true), null)
        compare(notice(true, "logos", "idle", true), null)
    }

    function test_unknown_storage_state_says_nothing() {
        compare(notice(true, "logos", "", true), null)
    }
}
