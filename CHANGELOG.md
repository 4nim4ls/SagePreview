# Changelog

## 1.0.0 — public release

- Native Windows 11 x64 lightbox with Explorer-only Spacebar activation, Left/Right navigation, Escape, zoom, and bounded panning.
- Explorer thumbnails and Spacebar previews for Clip Studio Paint `.clip`, Krita `.kra`, and Photoshop `.psd` saved composites.
- AVIF support via installed Windows WIC codec; optional diagnostic tool. WebP via installed WIC codec where available.
- Small per-user graphical installer/uninstaller with optional login startup and conservative thumbnail registration.
- Explicit no-telemetry, no-subscription, no-tray design; Windows runtime and static zlib only.
- MIT license under 4nim4ls and third-party notices.

**Known limitation:** Some very large PSD images exceed the safety limit; the app intentionally does not reconstruct proprietary layers.

Version 1.0.0 retains the functionality validated through RC2. Packaging and copyright notices were updated; no new image features were added.
