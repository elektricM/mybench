// Offscreen helper self-test. Normal run shows the scene; with --screenshot it is rendered
// offscreen. --self-check verifies pixel colors of each element in the written PNG.
#include <mybench/offscreen.h>

#include <QCoreApplication>
#include <QImage>

#include <cstdio>
#include <cstdlib>

namespace {

bool near(QRgb c, int r, int g, int b, int tolerance = 40)
{
    return std::abs(qRed(c) - r) <= tolerance && std::abs(qGreen(c) - g) <= tolerance
        && std::abs(qBlue(c) - b) <= tolerance;
}

int selfCheck(const QString &path)
{
    QImage image(path);
    if (image.isNull()) {
        std::fprintf(stderr, "self-check: cannot read %s\n", qPrintable(path));
        return 1;
    }
    image = image.convertToFormat(QImage::Format_RGB32);
    const qreal s = image.width() / 640.0;  // screenshot may use --dpr
    auto px = [&](int x, int y) { return image.pixel(int(x * s), int(y * s)); };
    struct Check { const char *what; int x, y, r, g, b; };
    const Check checks[] = {
        {"background", 5, 390, 0x10, 0x12, 0x14},
        {"Shape (green circle)", 130, 220, 0x00, 0xc8, 0x53},
        {"MultiEffect source (blue)", 320, 220, 0x29, 0x79, 0xff},
        {"custom QSGGeometryNode (red triangle)", 520, 260, 0xff, 0x17, 0x44},
        {"animation ran (white square at end)", 610, 360, 0xff, 0xff, 0xff},
    };
    int failures = 0;
    for (const Check &c : checks) {
        const QRgb v = px(c.x, c.y);
        const bool ok = near(v, c.r, c.g, c.b);
        std::fprintf(stderr, "self-check: %-40s %s (got #%06x)\n", c.what, ok ? "ok" : "FAIL", v & 0xffffff);
        failures += ok ? 0 : 1;
    }
    // Glow: a pixel just outside the blue square must be tinted yellow (r,g > b).
    const QRgb glow = px(262, 220);
    const bool glowOk = qRed(glow) > qBlue(glow) + 15 && qGreen(glow) > qBlue(glow) + 15;
    std::fprintf(stderr, "self-check: %-40s %s (got #%06x)\n", "MultiEffect glow", glowOk ? "ok" : "FAIL",
                 glow & 0xffffff);
    failures += glowOk ? 0 : 1;
    // Text: some bright pixels in the title line.
    int bright = 0;
    for (int y = 16; y < 40; ++y)
        for (int x = 20; x < 400; ++x)
            bright += qGray(px(x, y)) > 160 ? 1 : 0;
    const bool textOk = bright > 200;
    std::fprintf(stderr, "self-check: %-40s %s (%d bright px)\n", "text", textOk ? "ok" : "FAIL", bright);
    failures += textOk ? 0 : 1;
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char *argv[])
{
    bool check = false;
    bool windowRoot = false;
    QString shot;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--self-check")
            check = true;
        else if (a == "--window")
            windowRoot = true;
        else if (a == "--screenshot" && i + 1 < argc)
            shot = QString::fromLocal8Bit(argv[i + 1]);
    }
    const int code = mybench::runQmlApp(argc, argv, {
        .module = QStringLiteral("MyBench.OffscreenTest"),
        .type = windowRoot ? QStringLiteral("WindowScene") : QStringLiteral("Scene"),
        .applicationName = QStringLiteral("mybench-offscreen-test"),
        .setup = [](QQmlEngine &, const mybench::LaunchOptions &options, QVariantMap &) {
            if (!options.arguments.isEmpty())
                std::fprintf(stderr, "extra arguments: %s\n", qPrintable(options.arguments.join(u' ')));
        },
    });
    if (code != 0 || !check || shot.isEmpty())
        return code;
    return selfCheck(shot);
}
