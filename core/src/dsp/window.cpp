#include "mydaq/dsp/window.h"

#include <cmath>
#include <numbers>
#include <span>

namespace mydaq::dsp {

namespace {

// Cosine-sum coefficients a_m (the window is sum_m (-1)^m a_m cos(2*pi*m*k/N)).
constexpr double kNone[] = {1.0};
constexpr double kHanning[] = {0.5, 0.5};
constexpr double kHamming[] = {0.54, 0.46};
constexpr double kBlackmanHarris[] = {0.42323, 0.49755, 0.07922};
constexpr double kExactBlackman[] = {7938.0 / 18608.0, 9240.0 / 18608.0, 1430.0 / 18608.0};
constexpr double kBlackman[] = {0.42, 0.50, 0.08};
constexpr double kFlatTop[] = {0.215578950, 0.416631580, 0.277263158, 0.083578947, 0.006947368};
constexpr double kBlackmanHarris4[] = {0.35875, 0.48829, 0.14128, 0.01168};
constexpr double kBlackmanHarris7[] = {0.27105140069342, 0.43329793923448, 0.21812299954311,
                                       0.06592544638803, 0.01081174209837, 0.00077658482522,
                                       0.00001388721735};
constexpr double kLowSidelobe[] = {0.323215218, 0.471492057, 0.17553428, 0.028497078, 0.001261367};

std::span<const double> coefficients(WindowType type)
{
    switch (type) {
    case WindowType::None: return kNone;
    case WindowType::Hanning: return kHanning;
    case WindowType::Hamming: return kHamming;
    case WindowType::BlackmanHarris: return kBlackmanHarris;
    case WindowType::ExactBlackman: return kExactBlackman;
    case WindowType::Blackman: return kBlackman;
    case WindowType::FlatTop: return kFlatTop;
    case WindowType::BlackmanHarris4: return kBlackmanHarris4;
    case WindowType::BlackmanHarris7: return kBlackmanHarris7;
    case WindowType::LowSidelobe: return kLowSidelobe;
    }
    return kNone;
}

}  // namespace

const std::vector<WindowType>& windowTypes()
{
    static const std::vector<WindowType> types = {
        WindowType::None,          WindowType::Hanning,         WindowType::Hamming,
        WindowType::BlackmanHarris, WindowType::ExactBlackman,  WindowType::Blackman,
        WindowType::FlatTop,       WindowType::BlackmanHarris4, WindowType::BlackmanHarris7,
        WindowType::LowSidelobe,
    };
    return types;
}

QString windowName(WindowType type)
{
    switch (type) {
    case WindowType::None: return QStringLiteral("None");
    case WindowType::Hanning: return QStringLiteral("Hanning");
    case WindowType::Hamming: return QStringLiteral("Hamming");
    case WindowType::BlackmanHarris: return QStringLiteral("Blackman-Harris");
    case WindowType::ExactBlackman: return QStringLiteral("Exact Blackman");
    case WindowType::Blackman: return QStringLiteral("Blackman");
    case WindowType::FlatTop: return QStringLiteral("Flat Top");
    case WindowType::BlackmanHarris4: return QStringLiteral("4 Term B-Harris");
    case WindowType::BlackmanHarris7: return QStringLiteral("7 Term B-Harris");
    case WindowType::LowSidelobe: return QStringLiteral("Low Sidelobe");
    }
    return {};
}

std::vector<double> makeWindow(WindowType type, size_t n)
{
    if (n <= 1)
        return std::vector<double>(n, 1.0);
    const std::span<const double> a = coefficients(type);
    std::vector<double> w(n);
    for (size_t k = 0; k < n; ++k) {
        const double x = 2.0 * std::numbers::pi * double(k) / double(n);
        double sum = 0.0;
        double sign = 1.0;
        for (size_t m = 0; m < a.size(); ++m, sign = -sign)
            sum += sign * a[m] * std::cos(double(m) * x);
        w[k] = sum;
    }
    return w;
}

void applyWindow(std::vector<double>& x, WindowType type)
{
    if (type == WindowType::None)
        return;
    const std::vector<double> w = makeWindow(type, x.size());
    for (size_t k = 0; k < x.size(); ++k)
        x[k] *= w[k];
}

double coherentGain(WindowType type) { return coefficients(type)[0]; }

double equivalentNoiseBandwidth(WindowType type)
{
    // Parseval on the cosine sum: mean(w^2) = a0^2 + sum_{m>0} a_m^2 / 2, mean(w) = a0.
    const std::span<const double> a = coefficients(type);
    double meanSquare = a[0] * a[0];
    for (size_t m = 1; m < a.size(); ++m)
        meanSquare += 0.5 * a[m] * a[m];
    return meanSquare / (a[0] * a[0]);
}

int mainLobeHalfWidth(WindowType type) { return int(coefficients(type).size()); }

double coherentGain(const std::vector<double>& window)
{
    if (window.empty())
        return 0.0;
    double sum = 0.0;
    for (double w : window)
        sum += w;
    return sum / double(window.size());
}

double equivalentNoiseBandwidth(const std::vector<double>& window)
{
    double sum = 0.0;
    double sumSquares = 0.0;
    for (double w : window) {
        sum += w;
        sumSquares += w * w;
    }
    return sum == 0.0 ? 0.0 : double(window.size()) * sumSquares / (sum * sum);
}

}  // namespace mydaq::dsp
