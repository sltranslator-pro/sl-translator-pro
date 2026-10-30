# SL Translator Pro HUD v2 - Kurulum

UUID lisanslı sürüm içindir. Lisans bilgisi sunucudan, HUD'u takan avatara göre gelir.
HUD içinde lisans anahtarı yoktur.

## Dosyalar
| Dosya | Ne işe yarar |
|---|---|
| `slt_hud_panel.png` | Ana panel görseli (AÇIK/KAPALI ve DUAL açık/kapalı halleri tek dosyada) |
| `slt_hud_langs.png` | 32 dilin listesi (4 sütun x 8 satır) |
| `slt_hud.lsl` | HUD scripti |
| `make_hud_v2.py` | Görselleri yeniden üretir: `uv run --with pillow python make_hud_v2.py` |

## Second Life'ta kurulum (yaklaşık 3 dakika)
1. **Görselleri yükleyin:** Build > Upload > Image ile iki PNG'yi yükleyin.
   Envanterdeki adları `slt_hud_panel` ve `slt_hud_langs` olarak kalsın.
2. **4 küp oluşturun:** Yere 4 küp koyun. Boyutları önemli değil, script kendisi ayarlar.
3. **Ana küpü hazırlayın:** Birini ana küp seçin. İçine (Content sekmesi) iki görseli sürükleyin.
   Content > New Script deyin, açılan scriptin içini silip `slt_hud.lsl` içeriğini yapıştırın ve kaydedin.
4. **Birleştirin:** Önce diğer 3 küpü seçin, **en son ana küpü** seçin (Shift ile), **Ctrl+L** basın.
   Script küpleri kendisi adlandırır ve yerleştirir: iki dil penceresi ve açılan dil listesi.
5. **Panele bir kez dokunun:** Görmek istediğiniz geniş yüzüne dokunun. Sohbette "panel ready" yazar.
6. **HUD olarak takın:** Sağ tık > **Attach to HUD > Bottom** (veya Top).
   Panelin arkası görünüyorsa Edit ile 180 derece çevirin.

**Dil pencereleri yanlış yazıların üstüne geldiyse:** Scriptte `integer FLIP = FALSE;`
satırını `integer FLIP = TRUE;` yapıp kaydedin.

## Butonlar
| Buton | Ne yapar |
|---|---|
| **POWER** | Çeviriyi açar / kapatır |
| **MY LANGUAGE** | Dil listesini açar: okuduğunuz dil |
| **SEND LANGUAGE** | Dil listesini açar: mesajlarınızın gideceği dil |
| **⇄ SWAP** | İki dili yer değiştirir |
| **DUAL** | Mesajı orijinal + çeviri birlikte gönderir |
| **STATUS** | Sunucuya sorar: lisans aktif mi, kaç gün kaldı, plan, istemci sürümü, viewer bağlı mı |

Dil listesi panelin altında açılır (HUD ekranın altındaysa yukarı açılır).
Bir dil seçince ya da aynı pencereye tekrar dokununca kapanır.

## Bilinmesi gerekenler
- STATUS ilk başta "no license linked" der. UUID'li DLL ile Firestorm'a bir kez girince
  sunucu avatarınızı lisansa bağlar ve STATUS çalışmaya başlar.
- Dil seçimi sadece UUID'li DLL ile çalışır. Yayındaki 1.5.1 DLL dil komutunu tanımaz.
