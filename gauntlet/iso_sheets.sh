#!/usr/bin/env bash
# Isolation test: render every building ALONE on flat ground (--isolate N) from
# the six orbit views (--iview 0..5), then tile each building's views into one
# contact sheet: gauntlet/shots/iso/bNN.png (top: FL FR BL / bottom: BR high front).
# Usage: bash gauntlet/iso_sheets.sh [first] [last]
set -e
cd "$(dirname "$0")/.."
E=./native/build-msvc/Release/sea_trial.exe
OUT=gauntlet/shots/iso
mkdir -p "$OUT/raw"
first=${1:-1}; last=${2:-29}
for n in $(seq "$first" "$last"); do
  for v in 0 1 2 3 4 5; do
    "$E" --isolate "$n" --iview "$v" --scene sail --pos 0 44 --head 0 --shot-frames 40 \
         --shot "$OUT/raw/b$(printf %02d "$n")_v$v.png" > /dev/null
  done
  python - "$OUT" "$n" <<'EOF'
import sys
from PIL import Image, ImageDraw
out, n = sys.argv[1], int(sys.argv[2])
names = ['front-left', 'front-right', 'back-left', 'back-right', 'overhead', 'front']
sheet = Image.new('RGB', (1920, 720), (0, 0, 0))
for v in range(6):
    im = Image.open('%s/raw/b%02d_v%d.png' % (out, n, v)).convert('RGB').resize((640, 360))
    ImageDraw.Draw(im).text((8, 6), 'B%02d  %s' % (n, names[v]), fill=(255, 255, 0))
    sheet.paste(im, ((v % 3) * 640, (v // 3) * 360))
sheet.save('%s/b%02d.png' % (out, n))
EOF
  echo "building $n done"
done
