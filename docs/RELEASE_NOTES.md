# SagePreview 1.0.0 — release notes

SagePreview is a free, native Windows 11 image preview utility. Select an image in File Explorer and press **Spacebar** to open a centered, borderless lightbox; use Left/Right to navigate, the mouse wheel to zoom, and drag to pan. Escape or Spacebar closes the preview.

Adds native Explorer thumbnails for Clip Studio Paint `.clip`, Krita `.kra`, and Photoshop `.psd` files, using the saved embedded preview/composite. Supports AVIF and other images through installed Windows WIC codecs. No subscriptions, account, telemetry, tray clutter, or Explorer injection.

### Requirements and limitations

- Windows 11 x64 only.
- AVIF may require both Microsoft HEIF Image Extensions and AV1 Video Extension. On the test machine, AV1 Video Extension alone was insufficient. WebP depends on an installed WIC codec.
- Exceptionally large PSDs exceeding 25 million pixels are rejected for safety. SagePreview does not reconstruct editable artwork layers, and does not support SVG or HEIC/HEIF files as dedicated formats.
- Installs per-user, optionally starts at sign-in, and does not overwrite third-party thumbnail registrations. Uninstall an old SagePreview before installing this version.
- **Unsigned builds:** Windows SmartScreen/Smart App Control may warn or block them. Do not disable Windows protections. Prefer a signed release when available.

Developed by [4nim4ls](https://github.com/4nim4ls) with substantial assistance from OpenAI's ChatGPT. MIT licensed; no OpenAI endorsement or affiliation.
