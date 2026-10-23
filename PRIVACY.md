# Privacy

Last updated: October 23, 2026

This repository contains only the SL Translator Pro Windows client (the `dinput8.dll` proxy, its installer and Firestorm UI files). The translation server is operated separately and is not part of this repository.

## What the client stores on your computer

Nothing, during normal use.

The client keeps no chat history, no message log and no local database. The only exception is an optional diagnostic log that you turn on yourself with the `/tr debug` command. While debug is on, the client writes to `%TEMP%\slt_debug.log`, and that file can include messages you translate. Turn it off with `/tr debug` again and delete the file whenever you like.

## What the client sends

To translate your outgoing chat and instant messages, the client sends the following to the translation service at `api.sltranslate.com` over HTTPS:

- The message text to translate and the target language
- Your license key, the client version and a temporary session identifier
- Whether translation is turned on or off
- A device identifier derived from the Windows MachineGuid, used to bind a free license key to one computer

Incoming messages are translated by Firestorm's own translation feature, which sends them to the same service.

The client also contacts GitHub to check for new official releases.

## The translation service

The service needs the message text to translate it. The service does not forward your messages to Google, Microsoft, OpenAI or DeepL.

## Your choices

- Turn off translation in Firestorm or with `/tr off`.
- Uninstall SL Translator Pro to stop the client from loading.
- Do not send anything through chat that you would not want translated by an online service.

## Contact

- In Second Life: Ulasilmaz Winslet
- Email: info@sltranslate.com
