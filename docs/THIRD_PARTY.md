# Third-party components and platform facilities

- **zlib 1.4** — Copyright (C) Jean-loup Gailly and Mark Adler; zlib license. Downloaded as pinned source by CMake and linked statically for Krita ZIP/PSD composite support. Preserve the [zlib notice](../licenses/ZLIB-LICENSE.txt) when distributing source or binaries. Upstream: https://zlib.net/
- **Inno Setup** — Build-time tool used for Windows installation packages, developed by Jordan Russell and Martijn Laan under its own terms. Inno Setup's software and installer components are not relicensed under SagePreview's MIT license. See https://jrsoftware.org/inno-setup and its current license/terms.
- **Microsoft Windows 11 APIs/codecs** — The application uses OS-provided Windows Imaging Component, Direct2D, Windows Shell/COM, and Windows SQLite. Windows codecs (including optional AVIF/WebP support) are supplied separately by Microsoft or other installed providers and are not included with SagePreview.

No ImageMagick, Rust runtime, cloud login, subscription or telemetry service is bundled. The MIT license applies to the SagePreview project code, not to third-party components under their separate terms.
