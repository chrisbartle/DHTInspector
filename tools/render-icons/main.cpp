// Rasterises an SVG to a PNG at a given size, so the icon set can be
// regenerated without Inkscape or rsvg-convert.
//
//   rendericon <in.svg> <size> <out.png>

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

int main(int argc, char **argv)
{
    // No QGuiApplication: this only paints shapes onto a QImage, and the
    // static build has no offscreen platform plugin to fall back on.
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    if (argc < 4) {
        err << "usage: rendericon <in.svg> <size> <out.png>" << Qt::endl;
        return 2;
    }
    const QString input = QString::fromLocal8Bit(argv[1]);
    const int size = QString::fromLatin1(argv[2]).toInt();
    const QString output = QString::fromLocal8Bit(argv[3]);

    QSvgRenderer renderer(input);
    if (!renderer.isValid()) {
        err << "cannot read " << input << Qt::endl;
        return 1;
    }
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();
    if (!image.save(output, "PNG")) {
        err << "cannot write " << output << Qt::endl;
        return 1;
    }
    return 0;
}
