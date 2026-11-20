# Installation and removal — Windows 11 x64

## Normal graphical setup

Download the validated installer from the official GitHub Release. The Windows 11 setup wizard installs to `%LOCALAPPDATA%\Programs\SagePreview`, without normally requiring administrator rights. The optional startup checkbox controls whether `SagePreviewListener.exe` runs automatically on sign-in. It does not alter default image-opening applications.

Explorer thumbnails are registered for `.clip`, `.kra`, and `.psd` only where no other effective handler already exists. AVIF normally uses Microsoft's installed WIC decoder and thumbnail handler.

The current v1.0 installer intentionally requires a **clean install**: uninstall any older SagePreview version first. A stale SagePreview COM class or preexisting installation directory causes setup to stop instead of overwriting it.

## Uninstall

Go to **Settings → Apps → Installed apps → SagePreview → Uninstall**. This stops SagePreview's listener, removes the startup entry if it matches the installation, and unregisters only SagePreview-owned thumbnail handlers. A DLL held open by a Windows thumbnail host might require signing out before complete file removal.

## Standalone portable testing

A staged ZIP can run without a registry installation. The viewer can be launched with an image file as its argument; the listener starts next to the viewer and is stopped using `SagePreviewListener.exe --stop`. Explorer thumbnails require the registered provider and should not be manually registered from temporary folders unless you understand their lifecycle.

## Optional formats and security

See the main README for AVIF/HEIF and WebP WIC codec requirements. A release marked UNSIGNED may trigger SmartScreen or be blocked by Smart App Control. Do not disable Windows security to use SagePreview. For direct public distribution, signing all executable files and the installer is recommended.
