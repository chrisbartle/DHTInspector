// Rasterises the icon masters, so the PNG set and the Windows .ico can be
// regenerated without Inkscape or rsvg-convert.
//
//   render-icons <in.svg> <size> <out.png>
//   render-icons --ico <out.ico> <in.svg> <size> [<in.svg> <size> ...]

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <QtEndian>

#include <vector>

namespace {

QTextStream err(stderr);

QImage render(const QString &path, int size)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        err << "cannot read " << path << Qt::endl;
        return {};
    }
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return image;
}

void appendLE(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

void appendLE(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}

QByteArray asPng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// An icon directory entry holding a bottom-up 32-bit DIB: a
// BITMAPINFOHEADER whose height covers the colour rows and the mask rows,
// the pixels, then the AND mask. The mask is unused at 32 bits but Windows
// still expects the bytes to be there.
QByteArray asDib(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();

    QByteArray out;
    appendLE(out, quint32(40));       // biSize
    appendLE(out, quint32(w));        // biWidth
    appendLE(out, quint32(2 * h));    // biHeight: colour rows plus mask rows
    appendLE(out, quint16(1));        // biPlanes
    appendLE(out, quint16(32));       // biBitCount
    appendLE(out, quint32(0));        // biCompression: BI_RGB
    appendLE(out, quint32(w * h * 4));// biSizeImage
    appendLE(out, quint32(0));        // biXPelsPerMeter
    appendLE(out, quint32(0));        // biYPelsPerMeter
    appendLE(out, quint32(0));        // biClrUsed
    appendLE(out, quint32(0));        // biClrImportant

    // ARGB32 is 0xAARRGGBB in a little-endian word, so in memory it is
    // already B, G, R, A: exactly what a 32-bit DIB wants. Rows run bottom
    // to top.
    for (int y = h - 1; y >= 0; --y)
        out.append(reinterpret_cast<const char *>(image.constScanLine(y)), w * 4);

    const int maskStride = ((w + 31) / 32) * 4;
    out.append(QByteArray(maskStride * h, '\0'));
    return out;
}

bool writeIco(const QString &path, const std::vector<QImage> &images)
{
    std::vector<QByteArray> payloads;
    payloads.reserve(images.size());
    for (const QImage &image : images) {
        // PNG keeps the big entries small; the shell has read them since
        // Vista. Smaller entries stay as DIBs, which everything reads.
        payloads.push_back(image.width() >= 128 ? asPng(image) : asDib(image));
    }

    QByteArray out;
    appendLE(out, quint16(0));                    // reserved
    appendLE(out, quint16(1));                    // type: icon
    appendLE(out, quint16(images.size()));

    quint32 offset = quint32(6 + 16 * images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const int size = images[i].width();
        out.append(char(size >= 256 ? 0 : size));  // 0 means 256
        out.append(char(size >= 256 ? 0 : size));
        out.append('\0');                          // palette size
        out.append('\0');                          // reserved
        appendLE(out, quint16(1));                 // planes
        appendLE(out, quint16(32));                // bits per pixel
        appendLE(out, quint32(payloads[i].size()));
        appendLE(out, offset);
        offset += quint32(payloads[i].size());
    }
    for (const QByteArray &payload : payloads)
        out.append(payload);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        err << "cannot write " << path << Qt::endl;
        return false;
    }
    return file.write(out) == out.size();
}

} // namespace

int main(int argc, char **argv)
{
    // No QGuiApplication: this only paints shapes onto a QImage, and the
    // static build has no offscreen platform plugin to fall back on.
    QCoreApplication app(argc, argv);

    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    if (args.size() >= 4 && args[0] == QLatin1String("--ico")) {
        const QString output = args[1];
        std::vector<QImage> images;
        for (int i = 2; i + 1 < args.size(); i += 2) {
            QImage image = render(args[i], args[i + 1].toInt());
            if (image.isNull())
                return 1;
            images.push_back(std::move(image));
        }
        return writeIco(output, images) ? 0 : 1;
    }

    if (args.size() == 3) {
        const QImage image = render(args[0], args[1].toInt());
        if (image.isNull())
            return 1;
        if (!image.save(args[2], "PNG")) {
            err << "cannot write " << args[2] << Qt::endl;
            return 1;
        }
        return 0;
    }

    err << "usage: render-icons <in.svg> <size> <out.png>" << Qt::endl;
    err << "       render-icons --ico <out.ico> <in.svg> <size> [<in.svg> <size> ...]" << Qt::endl;
    return 2;
}
