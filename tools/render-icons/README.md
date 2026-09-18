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

## The Windows icon

One `.ico` holds every size. Entries below 128 px are written as DIBs,
which anything can read; 128 and 256 are PNG, which the shell has read
since Vista and which keeps the file to about 65 KB instead of 250 KB.

```bash
../../tools/render-icons/build/render-icons --ico ../windows/dhtinspector.ico     dhtinspector-small.svg  16     dhtinspector-small.svg  20     dhtinspector-small.svg  24     dhtinspector-medium.svg 32     dhtinspector-medium.svg 40     dhtinspector-medium.svg 48     dhtinspector.svg        64     dhtinspector.svg        128     dhtinspector.svg        256
```

20 and 40 are there for the 125% and 250% display scale factors, which
Windows draws without resampling if the size is present.
