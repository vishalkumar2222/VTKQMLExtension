import QtQuick 2.9
import QtQuick.Window 2.2
import VTKRenderer 1.0

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")

    VTKRendererItem{
        id : vtk
        anchors.fill: parent
    }
}
