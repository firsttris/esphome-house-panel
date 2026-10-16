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
# Wartet, bis die App etwas gezeichnet hat (Bildschirm nicht mehr einfarbig), höchstens 60 s
ready() {
  for _ in $(seq 1 120); do
    [ "$(import -window root -format '%k' info:)" -gt 1 ] && { sleep 2; return; }
    sleep 0.5
  done
  echo "Vorschau zeichnet nichts" >&2
  exit 1
}
stdbuf -oL -eL "$BIN" >"$OUT/app.log" 2>&1 &
APP=$!
trap 'kill $APP $XVFB 2>/dev/null || true' EXIT
ready
tap() { xdotool mousemove "$1" "$2" mousedown 1 sleep 0.2 mouseup 1; }
shot() { import -window root "$OUT/$1.png"; }
shot haus
tap 530 305   # Wohnzimmer antippen: Raumansicht rechts
sleep 1.2
shot raum
tap 692 92    # Zurück zur Übersicht
sleep 0.8
tap 405 210   # Schlafzimmer: Karten Heizung und Klima
sleep 1.2
shot schlafzimmer
tap 740 243   # Karte Klima: Drehregler
sleep 1.2
shot klima
tap 692 92    # zurück zum Raum
sleep 0.8
tap 740 167   # Karte Heizung: Drehregler
sleep 1.2
shot heizung
tap 692 92    # zurück zum Raum
sleep 0.8
tap 692 92    # zurück zur Übersicht
sleep 0.8

# Leistung: 15 s Ruhe in der Übersicht. Die App schreibt jede Sekunde
# "fps · ms pro Bild (davon Kopieren) · Anteil des Bildschirms, der neu gezeichnet wird".
# Der Anteil hängt nicht vom Rechner ab und taugt zum Vergleich zweier Stände.
sleep 3
FROM=$(($(wc -l <"$OUT/app.log") + 1))
sleep 15
tail -n +"$FROM" "$OUT/app.log" | sed 's/\x1b\[[0-9;]*m//g' \
  | sed -nE 's/.* ([0-9]+) fps · ([0-9.]+) ms \(Kopie ([0-9.]+)\) · ([0-9]+) %.*/\1 \2 \3 \4/p' \
  | awk '{ f += $1; m += $2; k += $3; a += $4; n++ }
         END { if (n) printf "Leistung (Mittel über %d s): %.1f fps · %.1f ms pro Bild (Kopie %.1f) · %.1f %% neu gezeichnet\n", n, f/n, m/n, k/n, a/n;
               else print "Leistung: keine Messwerte" }' | tee "$OUT/perf.txt"

# Optional (GIF=1): kurzer Rundgang als animiertes GIF für das README.
# Rundgang: Haus, Wohnzimmer öffnen, Licht schalten, zurück.
if [ -n "$GIF" ]; then
  kill $APP 2>/dev/null || true
  sleep 0.5
  "$BIN" >/dev/null 2>&1 &
  APP=$!
  ready
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
