# 0030: Tap-to-pay — off the phone, on a wearable

- Status: accepted (a recommendation; nothing to build on the phone)
- Date: 2026-10-13

## Context

The owner wants to pay in shops by tapping, and to use the Norwegian payment and identity
apps (Vipps, BankID). Mobarchy runs on a Pixel 9 Pro Fold with an **unlocked bootloader**,
and that is the root of the problem for every on-device payment path. The Android apps
experiment (ADR 0029, Waydroid) confirmed it in practice on 2026-10-13: Vipps installs,
signs in through BankID, and then stops at "Confirm your identity" with "your phone has
been modified" — its Play Integrity / app-attestation check.

Three on-device paths were looked at and ruled out:

- **A payment app of our own, with NFC card emulation (HCE).** The NFC plumbing is
  buildable on AOSP: an app can emulate a contactless card over HCE with no secure
  element. But a contactless payment is not a card number; for every transaction the card
  produces a one-time cryptographic signature (an EMV cryptogram) with a key the issuer
  provisions. Those keys are only given to **certified wallet providers** (Google/Apple
  Wallet, banks' own apps) through a card network's token service (Visa VTS, Mastercard
  MDES), which requires certification and device attestation. A private party cannot get
  them; putting a raw card number into an HCE app is rejected by today's terminals. The
  wall is the card networks' closed credential system, not the phone.

- **Google Wallet / a fintech app (Revolut, Wise, …) on the phone.** Fintech apps have no
  NFC of their own; their tap-to-pay runs through Google/Apple Wallet underneath, so they
  hit the same attestation wall. Google Wallet tap-to-pay is confirmed to refuse even on
  GrapheneOS ("This phone can't be set up to tap to pay… may be rooted or running
  uncertified software"); it requires a Google-certified OS. Revolut actively blocks custom
  ROMs.

- **Relocking the bootloader with our own keys** (the GrapheneOS approach). This is the one
  path with real hope for the **apps** (not NFC): GrapheneOS relocks with its own keys, and
  several banks added hardware-attestation support (the Titan M2 vouching for a verified,
  locked boot) in 2024–2025 via GrapheneOS's attestation guide — BankID is reported to work.
  But banks typically recognise GrapheneOS *specifically*; Mobarchy is not GrapheneOS, so
  recognition is uncertain, Vipps is fragile even there, and it needs all of Mobarchy's boot
  to fit Android Verified Boot. Relocking is also the most dangerous operation on the device
  (a mistake bricks it). Kept as a possible future project, behind the owner's sign-off, not
  a near-term answer. Tap-to-pay (Google Wallet) would still never work, even relocked.

## Decision

**Mobarchy does not try to pay by NFC on the phone, and does not fake certification.** We
build no HCE wallet and no integrity/attestation spoofing. The phone keeps its unlocked
bootloader and the freedom that comes with it; paying is moved to something that is already
certified.

Our recommendation, in order:

1. **Tap-at-the-till: a payment wearable.** A watch or ring carries its own secure element
   and is a certified wallet, so it pays independently of the phone's state.
   - A passive **payment ring** (Fidesmo-based, e.g. Tapster, CNICK) links the owner's own
     card, needs no battery or charging, and is slim — the simplest thing that works today.
     No sensors, nothing for Mobarchy to read.
   - A **smart ring that also pays** is a 2026 category: the Circular Ring 3 (early 2027)
     combines Visa/Mastercard payment with health (sleep, HR, HRV, SpO₂, temperature, ECG)
     and a 10–12 day battery over Bluetooth; Ultrahuman's payment is promised during 2026.
     These could feed battery and health into Mobarchy over BLE — see "Later".

2. **In-app payments and identity (Vipps, BankID, transfers): a certified phone at home,
   remote-controlled.** A cheap certified Android at home, on power and Wi-Fi, runs the apps
   on real certified hardware (nothing faked). Mobarchy reaches it over ZeroTier (already in
   use) with scrcpy or a VNC/RustDesk client, shown as a small Mobarchy app. The one risk is
   FLAG_SECURE blanking a payment screen in the remote view — to be tested. This is the
   cleanest answer because the integrity check is genuinely satisfied.

3. **Fallback:** the owner's iPhone, or a physical card, for anything the above doesn't
   cover.

## Consequences

- No payment code ships in Mobarchy now; this ADR records why, so the question doesn't
  reopen from scratch.
- **Later, if the owner buys a connected wearable:** a Mobarchy integration that reads
  battery and health over BLE. Closed rings (Circular, Ultrahuman) use proprietary BLE and
  would need their protocol reverse-engineered with the device in hand; cheap health rings
  (Colmi-type) already expose the standard BLE Battery Service and are readable directly,
  but don't pay. That work would get its own ADR.
- **Later, maybe:** the "remote certified phone" app (scrcpy/VNC over ZeroTier) is small and
  buildable now; it would get its own ADR if the owner wants it.
- Relocking the bootloader with our own keys stays on the table as a separate, sign-off-gated
  project for the apps only — never for NFC tap-to-pay.
