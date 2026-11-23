# DualMesh logo

D'et er en LoRa-stavantenne (stammen) med to mesh-net i buen:
grøn = Meshtastic (øverst), blå = MeshCore (nederst). De mødes i den fælles node yderst og midt på antennen.
På monokrome skærme er det grønne net fyldt og det blå hult.

## Mapper
- `svg/` – masterfiler. `dark` til mørk baggrund, `light` til lys, `mono` til 1-bit OLED,
  `small-*` forenklet til små størrelser, `wordmark-*` med "DualMesh" (tekst konverteret til kurver, Inter Bold).
- `vector/` – rene vektorfiler til tryk, Illustrator, Inkscape, Figma, laser/vinyl: SVG og PDF hvor alle streger er konverteret til flader, uden masker, fonte eller billeder. Én sti pr. farve (meshtastic-net, meshcore-net, antenne-og-tekst).
- `png/` – færdige PNG'er (gennemsigtig baggrund) + pixel-tegnede ikoner 16/24/32 px.
- `firmware/dualmesh_logo_tdeck.h` – T-Deck (320x240): splash, ikon 64 og 24 px i RGB565 + alpha.
- `firmware/dualmesh_logo_oled.h` – Heltec V3.x (128x64): splash, ikon 32 og 16 px i XBM.
- `preview/` – sådan ser det ud på de rigtige skærme.
- `tools/` – Python-scripts der genererer det hele (`python logo.py && python export.py && python vector.py`, kræver rsvg-convert, Pillow, fonttools, skia-python).

## Brug
T-Deck:
```cpp
#include "dualmesh_logo_tdeck.h"
lcd.pushImage(0, 0, DUALMESH_SPLASH_W, DUALMESH_SPLASH_H, dualmesh_splash); // LovyanGFX
// TFT_eSPI: tft.setSwapBytes(true); tft.pushImage(...)
// LVGL 9: #define DUALMESH_LOGO_LVGL før include -> lv_image_set_src(img, &dualmesh_splash_img);
```
Heltec:
```cpp
#include "dualmesh_logo_oled.h"
u8g2.drawXBMP(0, 0, DUALMESH_OLED_SPLASH_W, DUALMESH_OLED_SPLASH_H, dualmesh_oled_splash);
```
Headerne bruger `static const`, så inkludér dem kun i én .cpp-fil.

Farver: grøn #4ADE80, blå #38BDF8, neutral #E5E7EB (mørk baggrund) / #16A34A, #0284C7, #1F2937 (lys baggrund).
