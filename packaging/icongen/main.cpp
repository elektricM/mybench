// mybench-icongen: renders the application icon SVG (assets/app-icon.svg) into the platform icon
// formats, so the repository holds one vector source and no binary icon files.
//
//   mybench-icongen <icon.svg> <out-dir> <basename>
//
// writes <out-dir>/<basename>.icns          macOS (PNG-based icns: 16..1024, @1x and @2x)
//        <out-dir>/<basename>.ico           Windows (PNG-compressed entries 16..256)
//        <out-dir>/png/<N>x<N>.png          Linux hicolor sizes (16..512)
//
// The icns/ico containers are written directly (both are simple chunk formats with embedded
// PNGs), so no iconutil/ImageMagick/Qt image plugin is needed on the build machine.
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>
#include <utility>
#include <vector>

namespace {

QImage render(QSvgRenderer& svg, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    svg.render(&painter, QRectF(0, 0, size, size));
    painter.end();
    return image.convertToFormat(QImage::Format_ARGB32);
}

QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void appendBe32(QByteArray& out, quint32 v)
{
    out.append(char((v >> 24) & 0xff)).append(char((v >> 16) & 0xff)).append(char((v >> 8) & 0xff)).append(char(v & 0xff));
}

void appendLe16(QByteArray& out, quint16 v)
{
    out.append(char(v & 0xff)).append(char((v >> 8) & 0xff));
}

void appendLe32(QByteArray& out, quint32 v)
{
    appendLe16(out, quint16(v & 0xffff));
    appendLe16(out, quint16(v >> 16));
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        std::fprintf(stderr, "mybench-icongen: cannot write %s\n", qPrintable(path));
        return false;
    }
    return true;
}

// Apple icns: 'icns' + total length, then (OSType, length incl. 8-byte header, PNG data) chunks.
QByteArray icns(QSvgRenderer& svg)
{
    const std::vector<std::pair<const char*, int>> chunks = {
        {"icp4", 16},   {"icp5", 32},   {"ic11", 32},  {"ic12", 64},  {"ic07", 128},
        {"ic13", 256},  {"ic08", 256},  {"ic14", 512}, {"ic09", 512}, {"ic10", 1024},
    };
    QByteArray body;
    for (const auto& [type, size] : chunks) {
        const QByteArray data = png(render(svg, size));
        body.append(type, 4);
        appendBe32(body, quint32(data.size() + 8));
        body.append(data);
    }
    QByteArray out("icns", 4);
    appendBe32(out, quint32(body.size() + 8));
    out.append(body);
    return out;
}

// Windows ico: ICONDIR, ICONDIRENTRY per image, PNG image data (Vista+ reads PNG entries).
QByteArray ico(QSvgRenderer& svg)
{
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::vector<QByteArray> images;
    for (int size : sizes)
        images.push_back(png(render(svg, size)));
    const int count = int(images.size());
    QByteArray out;
    appendLe16(out, 0);   // reserved
    appendLe16(out, 1);   // type: icon
    appendLe16(out, quint16(count));
    quint32 offset = quint32(6 + 16 * count);
    for (int i = 0; i < count; ++i) {
        const int size = sizes[i];
        out.append(char(size >= 256 ? 0 : size));   // width (0 = 256)
        out.append(char(size >= 256 ? 0 : size));   // height
        out.append(char(0));                        // palette
        out.append(char(0));                        // reserved
        appendLe16(out, 1);                         // planes
        appendLe16(out, 32);                        // bits per pixel
        appendLe32(out, quint32(images[size_t(i)].size()));
        appendLe32(out, offset);
        offset += quint32(images[size_t(i)].size());
    }
    for (const QByteArray& image : images)
        out.append(image);
    return out;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: mybench-icongen <icon.svg> <out-dir> <basename>\n");
        return 2;
    }
    // Rendering only: never needs a display (CI, SSH builds).
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid()) {
        std::fprintf(stderr, "mybench-icongen: cannot read %s\n", argv[1]);
        return 1;
    }
    const QDir out(QString::fromLocal8Bit(argv[2]));
    const QString base = QString::fromLocal8Bit(argv[3]);
    if (!out.mkpath(QStringLiteral("png")))
        return 1;

    bool ok = writeFile(out.filePath(base + QStringLiteral(".icns")), icns(svg))
              && writeFile(out.filePath(base + QStringLiteral(".ico")), ico(svg));
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
        const QString path = out.filePath(QStringLiteral("png/%1x%1.png").arg(size));
        ok = ok && render(svg, size).save(path, "PNG");
    }
    return ok ? 0 : 1;
}
