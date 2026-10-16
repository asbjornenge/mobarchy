# 0031: Telephony — calls and SMS through ofono

- Status: in progress (branch; the modem reads the SIM, network registration is being
  debugged)
- Date: 2026-10-16

## Context

The phone's SIM reader was a hardware fault until it was repaired (2026-10-16); telephony
was never built because there was nothing to talk to. Now the modem reads the SIM: ofono
(ofono-binder-plugin over the Android container's rild, ADR 0014) sees the SIM
(`SimManager`, carrier Talkmore), and exposes `VoiceCallManager`, `MessageManager`,
`NetworkRegistration` and `ConnectionManager` on `/ril_0`. What is missing is everything the
owner sees and uses: unlocking the SIM's PIN, showing the operator and signal, placing and
answering calls, and sending and reading SMS.

The owner's bar: "it MUST work before the phones are usable for anything beyond a toy."

## Decision

**A Phone app and a Messages app in the shell, over ofono on the system bus (`org.ofono`),
the same D-Bus-client pattern as Bluetooth (BlueZ).** ofono already does the hard part (the
modem, the RIL, the protocol); the shell is a client.

- **`Telephony` (`src/telephony.{h,cpp}`)**: one object, the active modem (`/ril_0`). It
  follows `org.ofono` with the ObjectManager/PropertiesChanged pattern and exposes to QML:
  - **SIM**: `simPresent`, `pinRequired` ("none"/"pin"/"puk"/…), `enterPin(pin)` /
    `resetPin(puk, newPin)` (`org.ofono.SimManager`). The lock screen shows the SIM-PIN
    prompt when `pinRequired` is "pin" (as it shows the owner's own PIN).
  - **Network**: `online` (`Modem.Online`, turned on once the SIM is unlocked),
    `operatorName`, `registration` ("registered"/"searching"/"denied"/…), `signal` (0–100)
    (`org.ofono.NetworkRegistration`).
  - **Calls** (`org.ofono.VoiceCallManager` and each `org.ofono.VoiceCall`): `calls`
    ([{path, state, number, name}]), `dial(number)`, `answer(path)`, `hangup(path)`,
    `hangupAll()`, `sendTones(dtmf)`. An incoming call raises the Phone app (or an
    in-call screen on the lock screen), like an alarm.
  - **SMS** (`org.ofono.MessageManager`): `sendMessage(to, text)`; incoming messages arrive
    on the `IncomingMessage` signal. Messages are kept by the shell in
    `~/.local/share/mobarchy/messages.json` (threads by number), the same file-owned pattern
    as the alarms (ADR 0028) and themes (ADR 0025), so the agent can read and write them too.
- **`qml/PhoneApp.qml`** (`mobarchy-shell --phone`, `org.mobarchy.Phone`): a dialer (keypad
  + recents), the registration/operator line, the in-call screen (mute, speaker, keypad,
  hang up), and the SIM-PIN prompt. **`qml/MessagesApp.qml`** (`--messages`,
  `org.mobarchy.Messages`): a conversation list and a thread view with a composer.
- **The agent** gets MCP tools (`mobarchy-mcp`): `call`, `hangup`, `sms_send`, `sms_list` —
  "ring mamma", "send en melding til …", and reading an SMS code aloud.
- **Incoming calls and messages** show as notifications and, for a call, a full-screen
  ringing screen that works over the lock screen (the alarm's pattern, ADR 0028); the power
  key silences the ringer.

## Open / to debug

- **Network registration.** The modem is `Online` and the SIM is read, but it does not yet
  register (`Status: unregistered`). A vendor modem service in the container,
  `vendor.modem_ml_svc_sit`, crash-loops (exit 1); it is the likely cause. This is the one
  real blocker to end-to-end calls/SMS and is tracked separately (it is a modem/RIL matter,
  not the UI).

## Consequences

- Telephony is the shell's, over ofono; no Android dialer/UI is involved (the container runs
  only vendor services, no framework — ADR 0014).
- SIM-PIN handling lives with the lock screen's PIN (`pinlock`), so the two PIN prompts look
  and feel the same.
