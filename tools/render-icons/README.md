# render-icons

Rasterises the icon masters in `packaging/linux/` to the PNG set that
`CMakeLists.txt` installs and embeds. Only needed when the artwork changes;
the PNGs are committed.

Any SVG rasteriser does the same job (`rsvg-convert`, `inkscape
--export-type=png`). This one exists because neither is available on the
Windows development machine, and Qt's SVG module already is.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Then, from `packaging/linux/`:

```bash
for s in 16 22 24; do
    ../../tools/render-icons/build/render-icons dhtinspector-small.svg  $s icons/${s}x${s}/dhtinspector.png
done
for s in 32 48; do
    ../../tools/render-icons/build/render-icons dhtinspector-medium.svg $s icons/${s}x${s}/dhtinspector.png
done
for s in 64 128 256; do
    ../../tools/render-icons/build/render-icons dhtinspector.svg        $s icons/${s}x${s}/dhtinspector.png
done
```

Which master feeds which size is the point of the exercise: the full drawing
has 1 px detail that disappears below about 64, so smaller sizes come from
drawings with fewer elements and heavier strokes.
