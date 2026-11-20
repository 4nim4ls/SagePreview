# Publishing SagePreview 1.0.0 on GitHub

This folder is GitHub-ready **source code**, not a signed Windows installer. Do not upload the source ZIP as though it were the program installer.

## 1. Final Windows build and verification

From 64-bit PowerShell in the repository root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Installer.ps1
```

The first script creates `dist\SagePreview-1.0.0-win11-x64-portable.zip`; the second creates `dist\SagePreview-1.0.0-win11-x64-UNSIGNED-setup.exe`. Run the final clean install/uninstall regression checklist, and do not publish a binary as signed unless it actually is. The `LICENSE` and `ZLIB-LICENSE.txt` should be present in the installed directory.

## 2. Repository setup

Create an empty public GitHub repository named `SagePreview` under [4nim4ls](https://github.com/4nim4ls), **without** generating a second README or license. Either upload the source files using GitHub's web UI, or from the project root use Git (replace `YOUR_REPO` with your actual repository URL):

```powershell
git init
git add .
git commit -m "SagePreview 1.0.0 source release"
git branch -M main
git remote add origin https://github.com/4nim4ls/SagePreview.git
git push -u origin main
```

No GitHub credentials or tokens belong in the source archive. Consider disabling Issues/Discussions in Repository Settings if you do not want support obligations; PRs are optional to review.

## 3. Release binaries and checksums

Build on Windows, then in PowerShell:

```powershell
$setup = '.\dist\SagePreview-1.0.0-win11-x64-UNSIGNED-setup.exe'
$portable = '.\dist\SagePreview-1.0.0-win11-x64-portable.zip'
Get-FileHash $setup -Algorithm SHA256
Get-FileHash $portable -Algorithm SHA256
```

Create a GitHub **draft release** with tag `v1.0.0`. Paste `docs/RELEASE_NOTES.md` and attach the actual setup EXE, portable ZIP (optional), and checksums. Verify both artifacts before **Publish release**. GitHub automatically provides source archives; there is no need to upload a second manually generated source ZIP.

For GitHub CLI users (once authenticated):

```powershell
gh release create v1.0.0 --draft --title "SagePreview 1.0.0" --notes-file .\docs\RELEASE_NOTES.md .\dist\SagePreview-1.0.0-win11-x64-UNSIGNED-setup.exe .\dist\SagePreview-1.0.0-win11-x64-portable.zip
```

**Never** disable security protection or misrepresent an unsigned setup as signed to make the release easier to install.
