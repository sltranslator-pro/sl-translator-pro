# SL Translator Pro HUD v2 - Setup

For the UUID-licensed client. Licence status comes from the server, matched by
the avatar wearing the HUD. The HUD never holds a licence key.

## Files
| File | What it is |
|---|---|
| `slt_hud_panel.png` | Main bar. 4 states stacked: ON/OFF x DUAL on/off. Upload once. |
| `slt_hud_langs.png` | 32 languages in their own script (4 x 8 grid). Upload once. |
| `slt_hud.lsl` | The script. |
| `make_hud_v2.py` | Regenerates both textures: `uv run --with pillow python make_hud_v2.py` |

## Build in Second Life (about 3 minutes)
1. Upload both PNGs (Build > Upload > Image). Keep the names `slt_hud_panel` and `slt_hud_langs`.
2. Rez **4 boxes** on the ground. Size does not matter; the script resizes them.
3. Pick one as the main box. Put `slt_hud_panel`, `slt_hud_langs` and a new script
   with `slt_hud.lsl` into its **Content**.
4. Select the 3 other boxes first, then the main box **last**, press **Ctrl+L** (link).
   The script names them `slt_my`, `slt_send`, `slt_pick` and places them.
5. **Touch the panel once** on the big side you want to see. It says "panel ready".
6. Right click > **Attach to HUD > Bottom** (or Top). Rotate in Edit if you see the back side.

If the two language windows end up over the wrong labels, set `integer FLIP = TRUE;`
in the script and save.

## Buttons
| Button | Action |
|---|---|
| **POWER** | Translation on/off (`/tr on`, `/tr off`) |
| **MY LANGUAGE** | Opens the language list: the language you read |
| **SEND LANGUAGE** | Opens the language list: the language your messages are sent in |
| **⇄ SWAP** | Swaps the two languages |
| **DUAL** | Sends original + translation together (`/tr src on/off`) |
| **STATUS** | Asks the server: licence active/expired, days left, plan, client version, viewer connected |

The language list opens under the bar (above it when the HUD is at the bottom of the
screen) and closes when you pick a language or touch the same window again.

## Server
`GET https://api.sltranslate.com/api/hud/status`. Second Life adds the
`X-SecondLife-Owner-Key` header. The server fills `avatar_uuid` on the licence when the
UUID client verifies, so STATUS answers "no license linked" until that avatar has logged
in once with the UUID client.
