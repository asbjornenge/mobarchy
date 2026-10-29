pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The weather card, tapped: the forecast for its place over home, hour by hour for the rest
// of the day and the night, and day by day for a week (Open-Meteo, as the card). A tap
// anywhere puts it away. (It takes no keyboard focus: the on-screen keyboard came with it.)
Rectangle {
    id: view

    // {name, latitude, longitude}
    property var place: null
    property bool here: false       // the place is where the phone is
    // the card's: WMO code, day -> [icon, word]
    property var look: null

    property var hours: []      // [{time, temp, code, day, rain}]
    property var days: []       // [{date, code, high, low, rain}]
    property string failure

    visible: opacity > 0
    opacity: 0
    color: Qt.alpha(Theme.bg, 0.97)
    Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    function open() {
        opacity = 1
        load()
    }
    function close() { opacity = 0 }

    function load() {
        if (!place)
            return
        failure = ""
        const r = new XMLHttpRequest()
        r.onreadystatechange = () => {
            if (r.readyState !== XMLHttpRequest.DONE)
                return
            try {
                if (r.status !== 200)
                    throw new Error()
                const d = JSON.parse(r.responseText)
                const now = Date.now()
                const hours = []
                for (let i = 0; i < d.hourly.time.length && hours.length < 25; i++) {
                    const t = new Date(d.hourly.time[i])
                    if (t.getTime() + 3600 * 1000 <= now)
                        continue
                    hours.push({ time: t, temp: Math.round(d.hourly.temperature_2m[i]), code: d.hourly.weather_code[i],
                                 day: d.hourly.is_day[i] === 1, rain: d.hourly.precipitation[i] })
                }
                const days = []
                for (let i = 0; i < d.daily.time.length; i++)
                    days.push({ date: new Date(d.daily.time[i] + "T12:00"), code: d.daily.weather_code[i],
                                high: Math.round(d.daily.temperature_2m_max[i]), low: Math.round(d.daily.temperature_2m_min[i]),
                                rain: d.daily.precipitation_sum[i] })
                view.hours = hours
                view.days = days
            } catch (e) {
                view.failure = qsTr("No forecast right now")
            }
        }
        // (the hours as the place's own clock has them: timezone=auto, read as local times)
        r.open("GET", "https://api.open-meteo.com/v1/forecast?latitude=" + Math.round(place.latitude * 100) / 100
               + "&longitude=" + Math.round(place.longitude * 100) / 100
               + "&hourly=temperature_2m,weather_code,precipitation,is_day"
               + "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum"
               + "&forecast_days=7&timezone=auto")
        r.send()
    }
    function rain(mm) { return mm >= 0.1 ? (Math.round(mm * 10) / 10).toLocaleString(Qt.locale(), "f", 1) + " mm" : "" }

    // (home below takes no touches)
    MouseArea { anchors.fill: parent; onClicked: view.close() }

    Flickable {
        id: flick
        anchors { fill: parent; topMargin: 56; bottomMargin: 24 }
        contentHeight: content.height
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        boundsBehavior: Theme.boundsBehavior

        // (a tap on the forecast too: the Flickable keeps it from the one behind)
        MouseArea {
            width: flick.width
            height: Math.max(flick.height, content.height)
            onClicked: view.close()
        }
        Column {
            id: content
            x: 20
            width: parent.width - 40
            spacing: 18

            Text {
                text: view.place ? (view.here ? "󰍎 " : "") + view.place.name : ""
                color: Theme.fg
                font { family: Theme.font; pixelSize: 26; bold: true }
            }
            Text {
                visible: view.failure !== "" || view.hours.length === 0
                text: view.failure || qsTr("Getting the forecast …")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }

            // --- hour by hour ---
            Text {
                visible: view.hours.length > 0
                text: qsTr("NEXT 24 HOURS")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 11; bold: true; letterSpacing: 1.2 }
            }
            ListView {
                visible: view.hours.length > 0
                width: parent.width
                height: 118
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: view.hours
                delegate: Rectangle {
                    id: hour
                    required property var modelData
                    required property int index
                    width: 64; height: 118
                    radius: 18
                    color: Theme.panel
                    border.color: Theme.line
                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: hour.index === 0 ? qsTr("Now") : hour.modelData.time.toLocaleTimeString(Qt.locale(), "HH")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 12 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: view.look ? view.look(hour.modelData.code, hour.modelData.day)[0] : ""
                            color: Theme.accent
                            font { family: Theme.font; pixelSize: 24 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: hour.modelData.temp + "°"
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 16 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: view.rain(hour.modelData.rain) || " "
                            color: Theme.accent
                            font { family: Theme.font; pixelSize: 10 }
                        }
                    }
                }
            }

            // --- day by day ---
            Text {
                visible: view.days.length > 0
                text: qsTr("7 DAYS")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 11; bold: true; letterSpacing: 1.2 }
            }
            Rectangle {
                visible: view.days.length > 0
                width: parent.width
                height: daysColumn.height + 16
                radius: 20
                color: Theme.panel
                border.color: Theme.line
                Column {
                    id: daysColumn
                    x: 16; y: 8
                    width: parent.width - 32
                    Repeater {
                        model: view.days
                        delegate: Item {
                            id: day
                            required property var modelData
                            required property int index
                            width: daysColumn.width
                            height: 48
                            Text {
                                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                                width: parent.width * 0.32
                                elide: Text.ElideRight
                                text: day.index === 0 ? qsTr("Today") : day.modelData.date.toLocaleDateString(Qt.locale(), "dddd")
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 15 }
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: parent.width * 0.34; verticalCenter: parent.verticalCenter }
                                text: view.look ? view.look(day.modelData.code, true)[0] : ""
                                color: Theme.accent
                                font { family: Theme.font; pixelSize: 22 }
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: parent.width * 0.46; verticalCenter: parent.verticalCenter }
                                text: view.rain(day.modelData.rain)
                                color: Theme.accent
                                font { family: Theme.font; pixelSize: 12 }
                            }
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: day.modelData.high + "°  <font color=\"" + Theme.muted + "\">" + day.modelData.low + "°</font>"
                                textFormat: Text.StyledText
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 15 }
                            }
                        }
                    }
                }
            }
        }
    }
}
