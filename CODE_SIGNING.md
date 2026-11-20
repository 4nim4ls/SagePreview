# Code signing policy

## Current status

SagePreview v1.0.0 is distributed **unsigned**. Its GitHub release page explicitly identifies the unsigned installer and portable archive. No SignPath certificate or signing pipeline has been approved or configured yet.

We intend to apply to SignPath Foundation's free open-source program. Approval is not guaranteed.

**Free code signing provided by SignPath.io, certificate by SignPath Foundation** — this attribution applies *only if and when* the project is accepted and SignPath signs a future release. It does not describe the currently published v1.0.0 files.

## Source, builds and distribution

- Official source: https://github.com/4nim4ls/SagePreview
- Official downloads: https://github.com/4nim4ls/SagePreview/releases
- License: MIT (see `LICENSE`). Third-party notices are in `docs/THIRD_PARTY.md`.
- The official Windows 11 x64 installer is created using CMake/MSVC and Inno Setup. The project also provides a portable build.
- The `.github/workflows/windows-build.yml` workflow builds and tests the unsigned portable package on a GitHub-hosted Windows runner. **It does not sign or publish releases.** Before any SignPath signing, the installer must also be built from a verifiable repository-based CI process, then reviewed and approved.
- A future signed release must be built from a reviewed, versioned repository commit. Signatures must be verified before publication. Every signing request will require deliberate manual approval.

## Roles

This is a single-maintainer project:

- Author/committer: [4nim4ls](https://github.com/4nim4ls)
- Reviewer for external changes: [4nim4ls](https://github.com/4nim4ls)
- Release-signing approver: [4nim4ls](https://github.com/4nim4ls)

External contributions, if accepted, are reviewed before merging. The maintainer does not promise ongoing support, acceptance of pull requests or a release cadence. Multi-factor authentication is required for accounts with repository and signing access.

## Privacy

SagePreview itself does not transmit user data to networked services, has no telemetry, and does not require an account or online activation. It reads local image files chosen through Windows Explorer and uses installed Windows codecs. Its optional background listener checks Explorer focus to respond to Spacebar. The installer registers thumbnail components and can configure startup at sign-in; its uninstaller removes these registrations.

Building SagePreview **from source** downloads a SHA-256-pinned zlib source archive during initial CMake configuration. Users who choose to install optional AVIF codecs obtain them separately from Microsoft. These are not background network activities performed by SagePreview during normal operation.

## User security

- Obtain downloads only from the official GitHub release page; compare the release's published SHA-256 sums.
- Unsigned v1.0.0 binaries may trigger Windows SmartScreen warnings or Smart App Control blocking. Do not disable Windows security protections to install SagePreview.
- Signatures, when available, attest to the stated publisher and integrity of the signed binaries, not to a guarantee of safety or lack of defects.
- No binary should be represented as signed unless its Authenticode signature verifies successfully.
