# SagePreview

**Small, free, native Windows 11 image thumbnails and Spacebar previews.**

SagePreview is a Windows 11 x64 utility by [4nim4ls](https://github.com/4nim4ls). It focuses on viewing artwork quickly in File Explorer, without a subscription, account, telemetry, tray menu, browser interface, or Explorer injection.

## Features

- **Spacebar preview** in Explorer: centered, borderless, always-on-top lightbox with a dimmed background.
- **Navigation:** Left/Right for adjacent supported images, Escape or Spacebar to close, mouse wheel to zoom, click-and-drag to pan.
- **Explorer thumbnails:** Clip Studio Paint (`.clip`), Krita (`.kra`), and Photoshop (`.psd`). Existing third-party thumbnail handlers are respected.
- **AVIF:** preview using an installed Windows WIC decoder; thumbnails normally remain with Windows' own handler.
- **Other images:** Windows Imaging Component (WIC) handles common formats including PNG, JPEG, BMP, GIF, and TIFF, plus WebP where a decoder is installed.

Preview extraction uses the saved image/composite inside creative documents. SagePreview **does not** rebuild painting layers or provide an image editor. HEIC/HEIF file support, SVG, video, OCR, and screenshot utilities are out of scope.

## Requirements

- **Windows 11 x64** (Windows 10 is not supported).
- AVIF support depends on Windows image codecs. On the tested Windows 11 machine, installing **Microsoft HEIF Image Extensions** was necessary even though **AV1 Video Extension** was already installed. See [codec notes](#optional-windows-codecs).

## Download and installation

Download the Windows installer from the project's **GitHub Releases** page once a release is published. Use the x64 setup executable; the source ZIP is for developers.

1. Close any running older SagePreview listener before replacing an installation.
2. Run the installer and optionally enable **Start SagePreview when I sign in**.
3. Select an image in File Explorer and press **Spacebar**.
4. To remove the app, use **Windows Settings → Apps → Installed apps → SagePreview → Uninstall**.

The installer works **per user**, does not require administrator privileges for its normal installation path, and refuses to overwrite another program's thumbnail registration. It also refuses to replace an existing SagePreview folder or stale SagePreview registration; uninstall an older version first. It does not change default image-opening associations.

**Signing note:** If the release executable is marked *UNSIGNED*, it does not have a trusted publisher signature; Windows SmartScreen or Smart App Control may warn or block it. **Do not disable Windows security features** to install it. Only obtain binaries from the project's official Releases page and verify the published checksums.

## Optional Windows codecs

**AVIF:** On some Windows 11 systems, both Microsoft's **HEIF Image Extensions** and **AV1 Video Extension** must be installed. The AV1 video extension alone was insufficient in real testing. Use the Microsoft Store, or these Store package identifiers with `winget`:

```powershell
winget install --id 9PMMSR1CGPWG --source msstore --accept-source-agreements --accept-package-agreements
winget install --id 9MVZQVXJBQ9V --source msstore --accept-source-agreements --accept-package-agreements
```

These packages provide image decoding to Windows; SagePreview does not install them or add standalone HEIC/HEIF support. The bundled `SageAvifCodecCheck.exe synthetic-avif.avif` can diagnose availability.

**WebP:** uses a Windows WIC decoder when installed. If `.webp` fails to preview, verify the Microsoft WebP Image Extension is available; WebP support is not guaranteed on every installation.

## Known limitations

- The PSD decoder reads the **saved composite**, not editable Photoshop layers. Very large PSDs exceeding the conservative 25-million-pixel limit are rejected rather than allocating excessive memory; PSD load times can be longer than `.clip`/`.kra`.
- `.clip` and `.kra` previews use their embedded saved images; this is intentional and usually sufficient for full-screen preview.
- Navigation uses natural filename order within the current folder, not every custom Explorer sort/filter mode.
- A tiny background listener uses a Windows low-level keyboard hook, guarded by Explorer file-list focus. The viewer and listener run separately from Explorer, and the listener can be stopped using `SagePreviewListener.exe --stop`.

## Build from source

Requires Visual Studio 2022 or 2026, **Desktop development with C++**, Windows SDK, C++ CMake tools for Windows, and Windows 11 x64. Install Inno Setup 6.3+ or 7 to compile the graphical installer.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Installer.ps1
```

The first script builds an optimized Release, runs CTest, and stages a portable distribution. The second builds the setup executable. **Neither build script installs anything or registers DLLs.** CMake downloads a SHA-256-pinned zlib source archive on first configuration; the application itself has no network dependency.

See [installation details](docs/INSTALLATION.md) and [release notes](CHANGELOG.md).

## License and development

SagePreview is released under the **MIT License** — Copyright (c) 2026 [4nim4ls](https://github.com/4nim4ls). See [LICENSE](LICENSE). Third-party components have separate notices in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

This project was developed with substantial help from **OpenAI's ChatGPT**, including C++ implementation, debugging, tests, and release tooling. The maintainer directed the project and validated it on Windows 11 with real artwork. SagePreview is an independent project, **not affiliated with or endorsed by OpenAI**, CELSYS, Adobe, Krita, or the original SageThumbs project.

SagePreview is provided as-is, without a commitment to ongoing maintenance, support, or accepting pull requests. Fork it, adapt it, and use it however the MIT License permits. A GitHub star is appreciated if you find it useful.

## Code signing policy

SagePreview v1.0.0 is currently **unsigned**. We intend to apply for free open-source signing through SignPath Foundation, but no approval has been granted and no signed downloads are available yet. See the [code signing policy](CODE_SIGNING.md) for the current status, privacy statement, and release-approval process.