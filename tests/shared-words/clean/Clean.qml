import QtQuick
import "../../../qml/Installer/Stages.js" as Stages

Item {
    property string busy: Stages.stageWord(1)
    property string other: qsTr("Finding peers is slow on a new node")
}
