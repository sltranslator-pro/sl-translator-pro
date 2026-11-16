# Security Policy

## Supported versions

Security fixes are applied to the latest public release. Users should update to the newest release before reporting an issue.

## Reporting a vulnerability

Please do not open a public issue for a vulnerability that could expose chat content, license data, device identifiers, update mechanisms or code-signing infrastructure.

Report it privately to `info@sltranslate.com` and include:

- A clear description
- Affected version
- Reproduction steps
- Expected and observed behavior
- Any proof of concept that is safe to share

We will acknowledge a valid report as soon as practical and coordinate disclosure after a fix is available.

## Official binaries

Download releases only from this GitHub repository or `https://sltranslate.com`. Official builds are intended to be Authenticode-signed once the open-source code-signing enrollment is complete.

The built-in updater will verify the installer's Authenticode signature before running it once signed releases are available. See [CODE_SIGNING.md](CODE_SIGNING.md).

Never commit signing credentials, private keys, SimplySign access material, license databases or production secrets to this repository.
