import QtCore

// Frigg's settings (ADR 0026), in ~/.config/mobarchy/frigg.ini.
Settings {
    location: StandardPaths.writableLocation(StandardPaths.GenericConfigLocation) + "/mobarchy/frigg.ini"
    // "Hei Frigg" starts a conversation: "always" | "charging" | "off"
    property string wake: "off"
}
