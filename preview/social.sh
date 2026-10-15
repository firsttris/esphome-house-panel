#!/bin/sh
# Social-Preview-Bild (1280 × 640) für GitHub aus dem Screenshot in docs/.
# Hochladen von Hand: Settings → General → Social preview.
set -e
F600=$(ls .esphome/font/Outfit@600@*.ttf | head -1)
F400=$(ls .esphome/font/Outfit@400@*.ttf | head -1)
convert -size 1280x640 gradient:'#141B2A-#0A0E16' \
  -font "$F600" -pointsize 52 -fill '#EEF2F8' -annotate +60+82 'ESPHome House Panel' \
  -font "$F400" -pointsize 24 -fill '#8892A8' -annotate +62+120 'Your whole home, alive: lights, shutters, heating and energy flow on a 7" ESP32-P4' \
  \( docs/haus.png -resize 853x500 \) -geometry +213+136 -composite \
  docs/social-preview.png
