# Privacy and Data Processing

Last updated: October 22, 2026

This document describes the data processed by the official hosted SL Translator Pro service at `api.sltranslate.com`. Self-hosted or modified clients may behave differently.

## Data sent for translation

To translate chat, the client or Firestorm sends the service:

- Chat text that needs to be translated
- Requested source and/or target language
- License key
- Client version
- A temporary session identifier
- Translation enabled/disabled status
- A device identifier derived from the Windows MachineGuid for license binding

Incoming translation requests are normally sent through Firestorm's translation interface. Outgoing translation requests are sent by the SL Translator Pro client.

## Account and access-control data

The service may store:

- License key and current license status
- Device binding identifier
- Second Life avatar UUID and avatar name when supplied during activation or support
- Session and last-activity information
- Selected language settings and client version

This information is used to validate free access keys, prevent accidental simultaneous use and provide support.

## Translation and diagnostic logs

Operational translation logs may contain:

- Source text and translated text
- Language pair and direction
- Processing time, result status and error/skip reason
- License key associated with the request
- Client and request metadata

For records created under the current retention policy, message text is scheduled to be removed after up to 7 days. Non-text diagnostic metadata may be retained longer for service reliability, abuse prevention and translation-quality analysis.

Some historical records created before the current retention policy may have been retained longer for model-quality research. Contact us if you want us to review or remove records associated with your license key where technically possible.

## Network logs

The website, API server and network protection provider may process connection metadata such as IP address, request time, path, user agent and security events. This information is used for security, rate limiting and incident diagnosis.

## Translation providers

The official service uses self-hosted translation models. Chat text is not sent to Google Translate, Microsoft Translator, OpenAI or DeepL by the official service.

Cloudflare may process network traffic as the website/API protection and delivery layer. The client also contacts GitHub to check for official software updates.

## Open-source client and hosted service

This repository contains the Windows client and installer. It does not contain the separately operated translation server, model weights, training data or operational databases.

## Your choices

- Disable translation in Firestorm or with the client controls to stop translation requests.
- Remove SL Translator Pro to stop the client from loading.
- Contact us to request support or deletion review associated with a license key.
- Do not send sensitive information through chat if you do not want it processed by the translation service.

## Contact

- In-world: Ulasilmaz Winslet
- Email: info@sltranslate.com
- Website: https://sltranslate.com
