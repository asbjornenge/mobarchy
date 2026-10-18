import QtQuick
import Mobarchy.Shell

// The weather where the owner is: by default where the phone is (GeoClue: the phone's GNSS,
// ADR 0033), or a place the owner chose (home.json "weather": {name, latitude, longitude}).
// Open-Meteo has the weather (free, no key) and OpenStreetMap's Nominatim the place's name;
// both are told only the place, to about a kilometre. Tapped, the card offers to choose.
BoardCard {
    id: card

    required property HomeConfig config
    readonly property var chosen: config.config.weather && config.config.weather.latitude !== undefined
                                  ? config.config.weather : null
    // where the phone was last (kept, for the weather at once after a restart)
    readonly property var here: config.config.weather_here ?? null
    readonly property var place: chosen ?? here
    property var now: null      // {temp, code, day, high, low}
    property string failure
    property bool asking: false     // choosing a place

    GeoLocation {
        id: geo
        // (exact: GeoClue does not ask an NMEA source for less; the weather rounds it anyway)
        // A first fix may take minutes (a cold start): longer then.
        accuracyLevel: 8
        onChanged: card.located()
        onFailed: (reason) => { if (!card.place) card.failure = qsTr("No position yet") }
    }
    function round(x) { return Math.round(x * 100) / 100 }
    // a fix: if it is somewhere else (5 km or so), the place's name, then the weather there
    function located() {
        locatedAt = Date.now()
        const lat = round(geo.latitude), lon = round(geo.longitude)
        if (here && Math.abs(here.latitude - lat) < 0.05 && Math.abs(here.longitude - lon) < 0.05)
            return refresh()
        const r = new XMLHttpRequest()
        r.onreadystatechange = () => {
            if (r.readyState !== XMLHttpRequest.DONE)
                return
            let name = qsTr("Here")
            try {
                const a = JSON.parse(r.responseText).address
                name = a.city || a.town || a.village || a.municipality || a.county || name
            } catch (e) {}
            card.config.set("weather_here", { name: name, latitude: lat, longitude: lon })
        }
        r.open("GET", "https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=10&lat=" + lat + "&lon=" + lon)
        r.setRequestHeader("User-Agent", "Mobarchy (phone OS; weather)")
        r.send()
    }
    // where the phone is, again: when the board comes into sight and the last fix is old
    property real locatedAt: 0
    onLiveChanged: if (live && !chosen && Date.now() - locatedAt > 15 * 60 * 1000) geo.refresh(here ? 120 : 600)
    function useHere() {
        config.set("weather", null)
        asking = false
        geo.refresh(card.here ? 120 : 600)
    }
    onTapped: asking = !asking

    title: place ? (chosen ? "" : "󰍎 ") + place.name : qsTr("Weather")
    icon: now ? look(now.code, now.day)[0] : "󰖐"
    compactIcon: ""     // (the scene says it)
    compactValue: now ? now.temp + "°" : "—"
    compactCaption: now ? qsTr("%1 · H %2° L %3°").arg((chosen ? "" : "󰍎 ") + place.name).arg(now.high).arg(now.low)
                        : qsTr("Getting the weather …")
    contentWhenCompact: !place || failure !== "" || asking

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
        get("https://api.open-meteo.com/v1/forecast?latitude=" + round(place.latitude) + "&longitude=" + round(place.longitude)
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
            card.asking = false
        })
    }

    onPlaceChanged: { now = null; refresh() }
    Component.onCompleted: { refresh(); if (!chosen) geo.refresh(card.here ? 120 : 600) }
    // every half hour: where the phone is now (when that is the place), and the weather
    Timer {
        interval: 30 * 60 * 1000; repeat: true
        running: card.place !== null || !card.chosen
        onTriggered: card.chosen ? card.refresh() : geo.refresh(card.here ? 120 : 600)
    }

    // --- the weather ---
    Text {
        id: temp
        visible: card.now !== null
        anchors { left: parent.left; top: parent.top; topMargin: -4 }
        text: card.now ? card.now.temp + "°" : ""
        color: Theme.fg
        font { family: Theme.font; pixelSize: 44; weight: Font.Light }
    }
    // the weather, alive: at the top right
    WeatherScene {
        parent: card
        visible: card.now !== null && !card.editing
        anchors { right: parent.right; rightMargin: card.compact ? 12 : 14
                  top: parent.top; topMargin: card.compact ? 8 : 12 }
        width: card.compact ? Math.min(40, card.height - 30) : 52
        height: width
        code: card.now ? card.now.code : 0
        day: card.now ? card.now.day : true
        running: card.live && visible
        ground: card.outlined ? Theme.bg : card.color  // (an outlined card: the home's ground)
    }
    Column {
        visible: card.now !== null && !card.asking
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

    // --- no place yet, no answer, or choosing one ---
    Column {
        visible: card.now === null || card.asking
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 8
        Text {
            visible: !card.compact || card.failure !== ""
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: card.compact ? 1 : 3
            elide: Text.ElideRight
            text: card.failure || (card.asking ? qsTr("The weather for …")
                                   : card.place ? qsTr("Getting the weather …") : qsTr("Finding where you are …"))
            color: Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
        Row {
            visible: !card.place || card.failure !== "" || card.asking
            width: parent.width
            spacing: 8
            // where the phone is
            Rectangle {
                width: 36; height: 36; radius: 18
                color: hereTap.pressed ? Theme.line : Theme.bg
                border.color: !card.chosen ? Theme.accent : Theme.line
                Text {
                    anchors.centerIn: parent
                    text: geo.busy ? "󰔟" : "󰍎"
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 16 }
                }
                MouseArea { id: hereTap; anchors.fill: parent; onClicked: card.useHere() }
            }
            // or a place by its name
            Rectangle {
                width: parent.width - 44
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
                        text: qsTr("City")
                        color: Theme.muted
                        font: placeInput.font
                    }
                }
            }
        }
    }
}
