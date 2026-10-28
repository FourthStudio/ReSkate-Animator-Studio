# Studio on ReSkate 2.1.3

This updates the published Studio 1.0.10 feature set to Studio 1.0.11 on the official ReSkate 2.1.3 release. The baseline is `9506a39e052e88a6cad2d50e624910f217e914a8` (tag `v2.1.3`). The downloaded official DLL contains the matching `9506a39e052e` build marker; its SHA256 is `139e932ad6328b898b2ff8c2aac3e0cd5ced6545c2f041aa8534239f97e59f9b` (42,401,280 bytes).

The supported game executable/build, Steam depot pins and game identity remain those of official 2.1.3. This does not adapt Studio to another Steam executable.

## Reconciliation

- Preserve official 2.1.3's Plugin Manager and approval/listing checks, world physics, menu-state observation, board scaling, viewmodels and offline plugin fallback.
- Preserve the Studio's independent animation/render/board registrations and outgoing filtering alongside the current plugin callbacks and post-submit pose restoration.
- Shared GPU descriptors now reserve 0..3 for common UI, 4..19 for plugin images, 20..22 for screen look, 23..27 for Studio, 28..91 for Mod Manager icons, and 92..115 for viewmodels. Heap capacity is 116; compile-time checks cover the boundaries. Studio loading and viewmodel rendering both keep the hidden overlay drawing.
- Keep the existing Studio 1.0.10 selection, stance, drop timing and grind-entry fade fixes. Pending Studio menu/FOV/launcher-channel PRs and the held-moves prototype were not merged as part of this compatibility update.
- The candidate build identifies itself as `AnimatorStudio 1.0.11+reskate.2.1.3`, with base version 2.1.3. Its launcher auto-update remains disabled to protect the modified runtime; installation uses the official 2.1.3 launcher and replaces its runtime DLL after disabling official updates.
- The manual test workflow builds all Studio/plugin test executables registered by 2.1.3, including physics and viewmodel suites. The native preview-library pin remains valid because the game identity/decoder did not change.

## Validation

Build runtime, launcher, dedicated server and all test targets with the Windows Release configuration, then run CTest and the Python native/packaging tests. Record actual results in the PR and alongside the generated DLL; do not treat compilation as visual acceptance.

For runtime acceptance, use an isolated ReSkate 2.1.3 install with updates off. Check opening/closing F8 and both menu buttons; an existing saved animation in preview/gameplay; normal/switch and pop variants; a long drop and grind handoff; and online playback with another player. Also open the Mod Manager/Plugin Manager and exercise a viewmodel plugin to check the shared descriptor layout. No game was launched or installation modified during the compatibility build.
