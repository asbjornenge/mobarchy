import QtQuick
import Mobarchy.Shell

// The weather where the owner is: Open-Meteo (free, no key, nothing about the owner but the
// place). The place is in home.json ("weather": {name, latitude, longitude}); until there is
// one, the card asks for it.
BoardCard {
    id: card

    required property HomeConfig config
    readonly property var place: config.config.weather ?? null
    property var now: null      // {temp, code, day, high, low}
    property string failure

    title: place ? place.name : qsTr("Weather")
    icon: now ? look(now.code, now.day)[0] : "󰖐"
    compactValue: now ? now.temp + "°" : "—"
    compactCaption: now ? qsTr("%1 · H %2° L %3°").arg(place.name).arg(now.high).arg(now.low)
                        : qsTr("Getting the weather …")
    contentWhenCompact: !place || failure !== ""

    // WMO weather codes -> an icon (day, night) and a word
    function look(code, day) {
        if (code === 0) return [day ? "󰖙" : "󰖔", qsTr("Clear")]
        if (code <= 2) return [day ? "󰖕" : "󰼱", qsTr("Partly cloudy")]
        if (code === 3) return ["󰖐", qsTr("Cloudy")]
        if (code <= 48) return ["󰖑", qsTr("Fog")]
        if (code <= 57) return ["󰖗", qsTr("Drizzle")]
        if (code <= 67) return ["󰖖", qsTr("Rain")]
        if (code <= 77) return ["󰖘", qsTr("Snow")]
        if (code <= 82) return ["󰖖", qsTr("Showers")]
        if (code <= 86) return ["󰖘", qsTr("Snow showers")]
        return ["󰖓", qsTr("Thunderstorm")]
    }

    function get(url, done) {
        const r = new XMLHttpRequest()
        r.onreadystatechange = () => {
            if (r.readyState !== XMLHttpRequest.DONE)
                return
            if (r.status !== 200) {
                card.failure = qsTr("No weather right now")
                return
            }
            try {
                done(JSON.parse(r.responseText))
                card.failure = ""
            } catch (e) {
                card.failure = qsTr("No weather right now")
            }
        }
        r.open("GET", url)
        r.send()
    }
    function refresh() {
        if (!place)
            return
        get("https://api.open-meteo.com/v1/forecast?latitude=" + place.latitude + "&longitude=" + place.longitude
            + "&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min"
            + "&forecast_days=1&timezone=auto", (d) => {
            card.now = { temp: Math.round(d.current.temperature_2m), code: d.current.weather_code,
                         day: d.current.is_day === 1, high: Math.round(d.daily.temperature_2m_max[0]),
                         low: Math.round(d.daily.temperature_2m_min[0]) }
        })
    }
    function choose(name) {
        if (!name.trim())
            return
        get("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + encodeURIComponent(name.trim()), (d) => {
            if (!d.results || d.results.length === 0) {
                card.failure = qsTr("No place called %1").arg(name.trim())
                return
            }
            const p = d.results[0]
            card.config.set("weather", { name: p.name, latitude: p.latitude, longitude: p.longitude })
        })
    }

    onPlaceChanged: { now = null; refresh() }
    Component.onCompleted: refresh()
    Timer { interval: 30 * 60 * 1000; running: card.place !== null; repeat: true; onTriggered: card.refresh() }

    // --- the weather ---
    Text {
        id: temp
        visible: card.now !== null
        anchors { left: parent.left; top: parent.top; topMargin: -4 }
        text: card.now ? card.now.temp + "°" : ""
        color: Theme.fg
        font { family: Theme.font; pixelSize: 44; weight: Font.Light }
    }
    Text {
        visible: card.now !== null
        anchors { right: parent.right; top: parent.top }
        text: card.now ? card.look(card.now.code, card.now.day)[0] : ""
        color: Theme.accent
        font { family: Theme.font; pixelSize: 30 }
    }
    Column {
        visible: card.now !== null
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 2
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.now ? card.look(card.now.code, card.now.day)[1] : ""
            color: Theme.fg
            font { family: Theme.font; pixelSize: 13 }
        }
        Text {
            text: card.now ? qsTr("H %1°  L %2°").arg(card.now.high).arg(card.now.low) : ""
            color: Theme.muted
            font { family: Theme.font; pixelSize: 12 }
        }
    }

    // --- no place yet, or no answer ---
    Column {
        visible: card.now === null
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 8
        Text {
            visible: !card.compact || card.failure !== ""
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: card.compact ? 1 : 3
            elide: Text.ElideRight
            text: card.failure || (card.place ? qsTr("Getting the weather …") : qsTr("Where are you?"))
            color: Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
        Rectangle {
            visible: !card.place || card.failure !== ""
            width: parent.width
            height: 36
            radius: 18
            color: Theme.bg
            border.color: placeInput.activeFocus ? Theme.accent : Theme.line
            TextInput {
                id: placeInput
                anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: Theme.fg
                font { family: Theme.font; pixelSize: 13 }
                onAccepted: { card.choose(text); focus = false }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !placeInput.text && !placeInput.activeFocus
                    text: card.compact ? qsTr("Your city, for the weather") : qsTr("City")
                    color: Theme.muted
                    font: placeInput.font
                }
            }
        }
    }
}
