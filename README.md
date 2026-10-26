# SL Translator Pro

[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D6.svg)](#building)
[![Latest release](https://img.shields.io/github/v/release/sltranslator-pro/sl-translator-pro)](https://github.com/sltranslator-pro/sl-translator-pro/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/sltranslator-pro/sl-translator-pro/total)](https://github.com/sltranslator-pro/sl-translator-pro/releases)

Free and open-source real-time chat translation integration for the 64-bit Firestorm viewer used with Second Life.

## Status

- The client, installer and Firestorm UI integration in this repository are free software.
- Official license keys are provided free of charge. Contact **Ulasilmaz Winslet** in Second Life.
- The project is not affiliated with Linden Lab or the Firestorm Viewer project.

## Features

- Incoming chat translation through Firestorm's translation interface
- Outgoing local chat and instant-message translation
- Firestorm settings and menu integration
- Separate incoming and outgoing language selection
- Local enable/disable and status commands
- Automatic update notification
- 64-bit Windows support

## Repository layout

```text
src/dinput8.cpp                 Windows client DLL
installer/installer.nsi        NSIS installer definition
installer/patch.ps1            Firestorm UI integration helper
installer/skins/               Firestorm XUI overrides
third_party/minhook/            MinHook dependency (BSD-2-Clause)
build.bat                       Reproducible MSVC x64 build
PRIVACY.md                      Data-processing disclosure
```

## Building

### Requirements

- Windows 10 or Windows 11, 64-bit
- Visual Studio 2022 with **Desktop development with C++**
- Windows 10/11 SDK
- NSIS 3.x, only if building the installer

### Build the DLL

Run from a regular command prompt:

```bat
build.bat
```

The script locates the Visual Studio x64 toolchain and writes:

```text
dist\dinput8.dll
```

### Build the installer

After building the DLL:

```bat
"C:\Program Files (x86)\NSIS\makensis.exe" installer\installer.nsi
```

The installer is written to `dist\SLTranslatorPro_Setup_vX.Y.Z.exe`.

## Installation

For normal use, download an official package from the [Releases](https://github.com/sltranslator-pro/sl-translator-pro/releases/latest) page. Close Firestorm before running the installer.

This project installs a proxy `dinput8.dll` next to the Firestorm executable. Windows security policies may block unsigned builds. Prefer official, digitally signed releases when available.

### What the installer changes

- Copies `dinput8.dll` into the Firestorm folder (an existing `dinput8.dll` is kept as `dinput8.dll.backup`)
- Adds the SL Translator settings panel and menu to Firestorm's UI files
- Registers an uninstaller under **Settings → Apps → Installed apps**

### Uninstall

Remove **SL Translator Pro** from **Settings → Apps → Installed apps**, or run `SLTranslator_Uninstall.exe` in the Firestorm folder. The uninstaller removes the DLL and restores any backed-up `dinput8.dll`. To stop translation without uninstalling, use `/tr off`.

## Privacy

The client does not store chat history or logs on your computer. A local diagnostic log is written only if you turn on `/tr debug` yourself. See [PRIVACY.md](PRIVACY.md) for details.

## Code signing policy

Official Windows releases are built from this repository by GitHub Actions and signed after manual approval.

- Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org) *(pending approval; current releases are unsigned)*
- Committers and reviewers: [repository collaborators](https://github.com/sltranslator-pro/sl-translator-pro/graphs/contributors)
- Approvers: [sltranslator-pro](https://github.com/sltranslator-pro)
- Privacy policy: [PRIVACY.md](PRIVACY.md), shown during installation

See [CODE_SIGNING.md](CODE_SIGNING.md) for verification steps.

## Security

- Do not download binaries from unofficial mirrors.
- Official release checksums are published with releases when available.
- Report vulnerabilities privately to `info@sltranslate.com`.

See [SECURITY.md](SECURITY.md) for the security policy.

## License

SL Translator Pro client code is licensed under the **GNU General Public License, version 3 or later**. See [LICENSE](LICENSE).

MinHook is distributed under its own BSD-2-Clause license. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contact

- In-world: **Ulasilmaz Winslet**
- Website: `https://sltranslate.com`
- Email: `info@sltranslate.com`
