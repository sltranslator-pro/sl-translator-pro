# SL Translator Pro

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D6.svg)](https://github.com/sltranslator-pro/sl-translator-pro/releases/latest)
[![Latest release](https://img.shields.io/github/v/release/sltranslator-pro/sl-translator-pro)](https://github.com/sltranslator-pro/sl-translator-pro/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/sltranslator-pro/sl-translator-pro/total)](https://github.com/sltranslator-pro/sl-translator-pro/releases)

Real-time chat translation integration for the 64-bit Firestorm viewer used with Second Life.

## Status

- Official Windows installers are published on the Releases page.
- Translation is performed by a separately operated hosted service.
- The project is not affiliated with Linden Lab or the Firestorm Viewer project.

## Features

- Incoming chat translation through Firestorm's translation interface
- Outgoing local chat and instant-message translation
- Firestorm settings and menu integration
- Separate incoming and outgoing language selection
- Local enable/disable, status and manual licence verification commands
- 64-bit Windows support

## Installation

Download the official installer from the [Releases](https://github.com/sltranslator-pro/sl-translator-pro/releases/latest) page. Close Firestorm before running `SLTranslatorPro_Setup.exe`.

### What the installer changes

- Copies `dinput8.dll` into the Firestorm folder (an existing `dinput8.dll` is kept as `dinput8.dll.backup`)
- Adds the SL Translator settings panel and menu to Firestorm's UI files
- Registers an uninstaller under **Settings → Apps → Installed apps**

### Uninstall

Remove **SL Translator Pro** from **Settings → Apps → Installed apps**, or run `SLTranslator_Uninstall.exe` in the Firestorm folder. To stop translation without uninstalling, use `/tr off`.

## Privacy

The client does not store chat history or logs on your computer. A local diagnostic log is written only if you turn on `/tr debug` yourself. See [PRIVACY.md](PRIVACY.md) for details.

## Code signing policy

Official Windows releases are signed with an Authenticode code signing certificate issued by Certum (Asseco Data Systems S.A.) to the project maintainer.

- Signed artifacts: `dinput8.dll` and the installer
- Signatures use SHA-256 with an RFC 3161 timestamp
- Privacy policy: [PRIVACY.md](PRIVACY.md), shown during installation

See [CODE_SIGNING.md](CODE_SIGNING.md) for verification steps.

## Security

- Do not download binaries from unofficial mirrors.
- Official release checksums are published with releases when available.
- Report vulnerabilities privately to `info@sltranslate.com`.

See [SECURITY.md](SECURITY.md) for the security policy.

## Contact

- Maintainer and contact person: **Ulasilmaz Winslet** (Second Life username: `ulasilmaz.winslet`)
- Website: `https://sltranslate.com`
- Email: `info@sltranslate.com`
