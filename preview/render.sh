#!/bin/sh
# Rendert Screenshots der Vorschau (lokal im Container esphome-sdl, in CI direkt).
# Aufruf im Repo-Verzeichnis:  sh preview/render.sh mila.yml
set -e
ROOM=${1:-mila.yml}
NAME=$(basename "$ROOM" .yml)
OUT=preview/shots/$NAME
mkdir -p "$OUT"
# Vorschau-Konfiguration: Hardware durch SDL ersetzen, Beispielwerte dazu
sed -e 's#device: !include base/device.yml#device: !include preview/sdl.yml\n  demo: !include preview/demo.yml#' \
    -e 's#^\(  device_name: .*\)$#\1-preview\n  preview: true#' "$ROOM" > .preview.yml
esphome compile .preview.yml
DEV=$(sed -n 's/^  device_name: //p' "$ROOM")-preview
BIN=.esphome/build/$DEV/.pioenvs/$DEV/program
Xvfb :99 -screen 0 1024x600x24 >/dev/null 2>&1 &
XVFB=$!
export DISPLAY=:99
sleep 1
"$BIN" >/dev/null 2>&1 &
APP=$!
trap 'kill $APP $XVFB 2>/dev/null || true' EXIT
sleep 4
tap() { xdotool mousemove "$1" "$2" mousedown 1 sleep 0.2 mouseup 1; }
shot() { import -window root "$OUT/$1.png"; }
shot haus
tap 530 305   # Wohnzimmer antippen: Raumansicht rechts
sleep 1.2
shot raum
tap 692 92    # Zurück zur Übersicht
sleep 0.8

# Optional (GIF=1): kurzer Rundgang als animiertes GIF für das README.
# Rundgang: Haus, Wohnzimmer öffnen, Licht schalten, zurück.
if [ -n "$GIF" ]; then
  kill $APP 2>/dev/null || true
  sleep 0.5
  "$BIN" >/dev/null 2>&1 &
  APP=$!
  sleep 3
  ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -framerate 20 -video_size 1024x600 -i :99 -t 9 "$OUT/demo.mp4" &
  REC=$!
  sleep 1.5; tap 530 305   # Wohnzimmer öffnen
  sleep 2.0; tap 745 200   # Licht schalten
  sleep 2.5; tap 692 92    # zurück
  wait $REC
  ffmpeg -loglevel error -y -i "$OUT/demo.mp4" \
    -vf "fps=15,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    "$OUT/demo.gif"
  rm -f "$OUT/demo.mp4"
fi
