# Public distribution and code signing

Signing is **not** included in the source build scripts. For public Windows 11 distribution:

1. Decide who the verified publisher will be (individual or organization) and obtain a trusted Windows Authenticode signing method such as Microsoft's Artifact Signing or a CA-issued code-signing certificate. A local self-signed certificate does not establish trust for public users.
2. Sign and timestamp **every shipped PE file**: `SagePreview.exe`, `SagePreviewListener.exe`, all four thumbnail DLLs, and any executable codec test tool. Sign the compiled setup EXE last. Inno Setup supports a signing tool configuration; don't place private keys/tokens in GitHub source.
3. Check signatures using Windows SDK `signtool verify /pa /v <path>` and inspect the publisher shown in Windows file properties. Verify the installer signs the exact files distributed.
4. Run a real clean install/uninstall and smoke test on a separate Windows 11 system after signing. Expect SmartScreen reputation to take time even with correctly signed builds.
5. Publish SHA-256 checksums for signed download artifacts and retain the build/signing logs privately.

Signing alone does not guarantee a security verdict or instant SmartScreen reputation. Do not instruct end users to disable Smart App Control, Defender, or other Windows security protections.

References: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options and https://learn.microsoft.com/en-us/windows/security/application-security/application-control/smart-app-control
