# SagePreview 1.0.0 publication checklist

## Already validated by maintainer during RC2 testing

- [x] Visual Studio Release compilation and tests on Windows 11 x64
- [x] Graphical Inno Setup installer compilation, installation and removal
- [x] Explorer thumbnails, Spacebar lightbox, navigation, zoom/pan, and login startup
- [x] Reader tests for CLIP, KRA, PSD, navigation, and layout; portable sanitizer regression run

## Repeat for the exact final (version/notice-only) package

- [ ] Build `tools\Build-Release.ps1` with the final source; ensure CTest passes and the portable ZIP contains LICENSE, zlib notice, and all required binaries.
- [ ] Build `tools\Build-Installer.ps1`; inspect setup version/name and LICENSE file in the installation.
- [ ] In a clean Windows 11 test environment, install/uninstall final 1.0.0 GUI setup and verify COM registrations, startup, image preview, thumbnails, and other software remain unaffected.
- [ ] Review source provenance/ownership and sample-asset permission. Prior audit was preliminary, especially regarding `.clip` format research. MIT license only applies to legitimately licensable code.
- [ ] Decide code-signing/distribution strategy. If signed: sign and timestamp all delivered EXE/DLL binaries and the installer, verify signatures, then re-test. If unsigned: clearly label and explain SmartScreen/Smart App Control warnings without bypass instructions.
- [ ] Generate SHA-256 checksum for final install EXE and (if attached) portable ZIP, and record them in the release notes or checksum file.
- [ ] Create the public repository under `4nim4ls`, push the source, verify LICENSE is detected, and attach the actual Windows-built files to a GitHub release `v1.0.0`.

**Do not claim final Windows installer validation of the changed packaging until the exact final installer is tested.**
