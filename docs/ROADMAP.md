# Roadmap

Each phase ends with a tagged image you can flash.

1. **Phase 0, before the device arrives (this PR).** Shell, launcher and 9 apps in a simulator;
   hardware layer with simulated and Linux backends; image builder on M5's pi-gen; CI with
   screenshots and an arm64 build.
2. **Phase 1, bring-up on the device.** Flash, confirm display, keyboard, battery, Wi-Fi, audio, RTC,
   IMU, IR. Fix device paths. Measure boot time and idle power. Move the Linux hardware code into
   `zerod` (D-Bus). Key LEDs follow Shift/Fn/Sym. Low-battery warning and safe shutdown.
3. **Phase 2, core apps.** Terminal, music player, voice recorder, alarms, camera with QR scanner,
   Bluetooth pairing, IR remote profiles.
4. **Phase 3, power-user features.** USB gadget modes (serial, Ethernet, mass storage, webcam),
   HDMI desktop mode, Grove and EXT module auto-detect (LoRa/GPS, Sub-GHz, NFC, RFID caps),
   A/B over-the-air updates.
5. **Phase 4, polish.** Themes, Python app SDK, store integration, docs.
