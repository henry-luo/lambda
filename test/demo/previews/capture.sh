#!/bin/sh
# Recapture the app screenshots the demo index shows in its tiles.
# Run from the repository root: sh test/demo/previews/capture.sh [name ...]
# Each demo opens headless at 1280×800 and runs 3 s of its virtual clock, so the
# content it loads asynchronously (a sample photo, a deck, a mailbox) is in
# the shot. A one-image page then renders just the crop the tile shows, as JPEG.
set -e
WORK=temp/demo_previews
OUT=test/demo/previews
mkdir -p "$WORK"
# a fresh reading state, so Book's shelves show none of this machine's history
rm -rf "$WORK/state"
mkdir -p "$WORK/state"
export LAMBDA_EBOOK_STATE_DIR="$PWD/$WORK/state"

# name, crop x y w h (about 1.86:1, the tile window), then an optional click
# that brings out content. Finder crops start below its breadcrumb and stop
# above the info panel's "Where", which spell out the checkout's local path.
SHOTS="chart 0 0 1000 537
scene3d 0 0 980 526
marble-drop 0 40 1280 687
sentence-game 0 0 1100 590
photo 0 0 1280 687
photos 0 0 1100 590
draw 0 0 1280 687
greetings 0 0 1040 558
word 0 0 1040 558
note 0 0 960 516
slides 0 0 1040 558
finder 200 130 1080 580 765 210
music 0 0 960 516
soundflow 0 0 1280 687 640 510
mail 0 0 1280 687 420 110
archive 0 0 960 516
readability 0 0 1000 537
book 0 242 1040 558"

echo "$SHOTS" | while read -r name x y w h cx cy; do
    if [ $# -gt 0 ] && ! echo " $* " | grep -q " $name "; then continue; fi
    entry=test/demo/$name/$name.ls
    [ -f "$entry" ] || entry=test/demo/$name/index.html
    click=""
    [ -n "$cx" ] && click="{\"type\":\"click\",\"x\":$cx,\"y\":$cy},{\"type\":\"wait\",\"ms\":2000},"
    printf '{"name":"capture %s","viewport":{"width":1280,"height":800},"events":[{"type":"wait","ms":3000},%s{"type":"render","file":"%s/%s.png"}]}\n' \
        "$name" "$click" "$WORK" "$name" > "$WORK/$name.json"
    ./lambda.exe view "$entry" --headless --no-log --event-file "$WORK/$name.json" > "$WORK/$name.log" 2>&1
    printf '<html><body style="margin:0;overflow:hidden"><img src="%s.png" style="display:block;margin:-%spx 0 0 -%spx"></body></html>\n' \
        "$name" "$y" "$x" > "$WORK/$name-crop.html"
    ./lambda.exe render "$WORK/$name-crop.html" -o "$OUT/$name.jpg" -vw "$w" -vh "$h" --no-log > /dev/null 2>&1
    echo "captured $OUT/$name.jpg"
done
