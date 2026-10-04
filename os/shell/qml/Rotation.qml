import QtCore
import QtQuick
import Mobarchy.Shell

// The screen follows how the device is held (ADR 0020): the device's rotation hook turns
// the panel in use, from the accelerometer's orientation (net.hadess.SensorProxy), unless
// the owner has locked that panel's rotation (quick settings; device.toml says which are
// locked until then).
QtObject {
    id: rotation

    required property Device device
    required property ScreenPower screen

    readonly property bool supported: device.canRotate()
    // the panel in use (a foldable has one on at a time)
    readonly property string connector: screen.inUse
    // the owner's choice per panel (connector -> locked); device.toml's default otherwise
    property var locks: ({})
    readonly property bool locked: connector in locks ? locks[connector]
                                                      : device.rotationLockedByDefault(connector)

    function setLocked(locked: bool) {
        const l = Object.assign({}, locks)
        l[connector] = locked
        locks = l
        settings.setValue("locks", JSON.stringify(l))
    }
    Component.onCompleted: locks = JSON.parse(settings.value("locks", "{}"))

    // the screen as turned now: quarter turns clockwise of the device (left-up: 1)
    property int turns: 0
    readonly property var orientationTurns: ({ "normal": 0, "left-up": 1, "bottom-up": 2, "right-up": 3 })

    function update() {
        if (!supported || connector === "")
            return
        const o = locked ? "normal" : sensor.orientation
        if (o !== "undefined") {    // (not known yet, or the screen is off: as it is)
            device.rotate(o)
            turns = orientationTurns[o]
        }
    }
    onLockedChanged: update()
    // (a fold turns the screen back upright first)
    onConnectorChanged: { turns = 0; update() }

    readonly property DeviceOrientation sensor: DeviceOrientation {
        // (the sensor runs only while the screen is on)
        active: rotation.supported && rotation.screen.on && !rotation.locked
        onOrientationChanged: rotation.update()
    }

    readonly property Settings settings: Settings {
        location: StandardPaths.writableLocation(StandardPaths.GenericStateLocation) + "/mobarchy/rotation.ini"
    }
}
