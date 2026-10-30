# SL Translator Pro HUD - Kurulum

## Dosyalar
- `hud_panel_on.png`, `hud_panel_off.png`: panel görselleri (1024x512)
- `hud_sltranslate.lsl`: HUD scripti
- `make_hud_texture.py`: görselleri yeniden üretir (`uv run --with pillow python make_hud_texture.py`)

## Second Life'ta kurulum
1. **Görselleri yükleyin:** Build > Upload > Image ile iki PNG'yi yükleyin (her biri 10 L$).
   Envanterdeki adları `hud_panel_on` ve `hud_panel_off` olarak kalsın.
2. **Prim oluşturun:** Yere bir küp (Box) koyun. Edit > Object sekmesinde:
   - Size: X = 0.010, Y = 0.400, Z = 0.200 (ince panel, 2:1 oran)
3. **Görseli uygulayın:** Edit > Texture, prim seçiliyken önce "Select Face" ile
   ön yüzü seçin, `hud_panel_on` görselini sürükleyin. Repeats 1.0 / 1.0, Offset 0, Rotation 0.
   Diğer yüzleri koyu lacivert renge boyayın.
4. **Hangi yüz?** Ön yüzün numarası script içindeki `FACE` değeriyle aynı olmalı.
   Varsayılan küp için ön yüz genellikle 0 değil; hızlı yol: Firestorm'da
   Ctrl+Alt+Shift+T ile yüz numarasını görün, ya da yüzü seçip Edit penceresinde bakın.
   Scriptteki `integer FACE = 0;` satırını bu numarayla değiştirin.
5. **Görselleri prime koyun:** İki görseli prim içeriğine (Content sekmesi) sürükleyin.
6. **Scripti ekleyin:** Content > New Script, içini `hud_sltranslate.lsl` ile değiştirin, kaydedin.
7. **Takın:** Prime sağ tık > Attach to HUD > Top Right (veya istediğiniz konum).
   Görsel yanlış yöne bakıyorsa Edit ile döndürün (HUD'da ön yüz size bakmalı).

## Kullanım
- Görseldeki her butona dokunmak doğrudan o işlemi yapar.
- AÇ/KAPAT ile panel yeşil (AÇIK) ve kırmızı (KAPALI) görsel arasında değişir.
- Butonların dışına (başlığa) dokunmak tam menüyü açar.
