import QtQuick

Item {
    property string busy: qsTr("Finding peers…")
    property string why: qsTr("Downloads are set to Logos Storage only, so apps can't be "
                              + "downloaded. You can enable GitHub downloads in Settings → "
                              + "Repositories.")
}
