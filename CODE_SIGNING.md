# Code Signing Policy

Official Windows release artifacts are built from this public repository.

## Artifacts

The following files are signed when a code-signing identity is available:

1. `dinput8.dll`
2. `SLTranslatorPro_Setup_vX.Y.Z.exe`

Signatures use SHA-256 file digests and RFC 3161 timestamps. Signing keys and cloud-signing credentials are never stored in this repository.

## Verification

On Windows, verify an official file with:

```powershell
Get-AuthenticodeSignature .\dinput8.dll | Format-List
```

or:

```bat
signtool verify /pa /v dinput8.dll
```

A valid signature proves publisher identity and file integrity. It does not replace source review or antivirus scanning.

## Auto-update verification (planned)

The client's update check currently asks for explicit user consent and downloads the installer over HTTPS from this repository's official GitHub release.

After the code-signing certificate is issued, the updater will additionally verify the downloaded installer with `WinVerifyTrust` and require the official publisher identity before running it. Unsigned or mismatched installers will be rejected. This work is tracked by the `TODO(code-signing)` marker in `src/dinput8.cpp`.

## Reproducible source relationship

The public build workflow compiles the same client sources and uploads unsigned artifacts for review. Release signing is a separate controlled step because the signing identity requires protected authentication.

Modified community builds must not be represented as official SL Translator Pro releases.
