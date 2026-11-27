import QtQuick
import QtTest
import "../../qml/Installer/Trust.js" as Trust

// Who signed a package, in words and a mark (Trust.js), for each verdict
// Trust.h's summary() gives.
TestCase {
    name: "Trust"

    function test_no_verdict_says_nothing() {
        compare(Trust.description(undefined), "")
        compare(Trust.description({}), "")
        compare(Trust.tone(undefined), "")
        verify(!Trust.isCheck(undefined))
    }

    function test_signers_in_words() {
        compare(Trust.description({ status: "trusted", publisher: "Logos" }), "Signed by Logos")
        compare(Trust.description({ status: "trusted" }), "Signed by a publisher you trust")
        compare(Trust.description({ status: "untrusted", publisher: "Acme" }),
                "Signed by a publisher you don't trust yet")
        compare(Trust.description({ status: "untrusted", impersonates: "Logos" }),
                "Claims to be Logos, but isn't signed with its key")
        compare(Trust.description({ status: "unsigned" }), "Not signed: nobody vouches for it")
        compare(Trust.description({ status: "invalid" }), "Damaged, or its signature doesn't hold")
        compare(Trust.description({ status: "unknown" }), "Couldn't check who signed it")
        compare(Trust.description({ status: "bundled" }), "Ships with Basecamp")
    }

    // What is installed already asks nothing: it only says what it is.
    function test_what_is_installed_says_so() {
        compare(Trust.description({ status: "trusted", publisher: "Logos", installed: true }),
                "Already installed, signed by Logos")
        compare(Trust.description({ status: "unsigned", installed: true }), "Already installed, not signed")
        compare(Trust.description({ status: "unknown", installed: true }),
                "Already installed; couldn't check who signed it")
        compare(Trust.tone({ status: "trusted", installed: true }), "muted")
    }

    // Basecamp's own rules come first: they decide even a trusted signer.
    function test_basecamps_rules_come_first() {
        compare(Trust.description({ status: "invalid", builtIn: true }),
                "Comes with Basecamp and updates only with it")
        compare(Trust.description({ status: "invalid", reserved: true }),
                "Comes with Basecamp, so only Logos can update it")
        // A check that didn't answer is not a damaged file.
        compare(Trust.description({ status: "invalid", checkFailed: true }),
                "Couldn't check who signed it. Try again")
        compare(Trust.description({ status: "trusted", publisher: "Acme", replaces: "Logos" }),
                "Not signed by Logos, who signed the version you have")
        compare(Trust.tone({ status: "trusted", replaces: "Logos" }), "danger")
        verify(!Trust.isCheck({ status: "trusted", replaces: "Logos" }))
    }

    function test_marks() {
        compare(Trust.tone({ status: "trusted" }), "fine")
        compare(Trust.tone({ status: "untrusted" }), "caution")
        compare(Trust.tone({ status: "untrusted", impersonates: "Logos" }), "danger")
        compare(Trust.tone({ status: "invalid" }), "danger")
        compare(Trust.tone({ status: "unsigned" }), "caution")
        verify(Trust.isCheck({ status: "trusted" }))
        verify(Trust.isCheck({ status: "bundled" }))
        verify(!Trust.isCheck({ status: "unsigned" }))
    }
}
