// Mathematical expressions of one variable for the Waveform Editor's Expression components
// (docs/instruments/gen.md 4.3.5).
//
// Grammar: numbers (1, 2.5, 1e-3), the variable x, constants pi and e, binary + - * / ^ (^ is
// right-associative and binds tighter than unary minus: -x^2 = -(x^2)), unary + and -, parentheses,
// and the functions abs acos asin atan ceil cos cosh exp floor ln log (base 10) max(a,b) min(a,b)
// mod(a,b) (remainder with the quotient rounded toward -infinity) sign sin sinh sqrt tan tanh.
// Names are case-insensitive; there is no implicit multiplication.
//
// The text is compiled once into a small stack program, so evaluating it over millions of samples
// is cheap. Errors carry the 1-based character position: "Missing ')' (11)".
#pragma once

#include <QString>

#include <span>
#include <vector>

namespace mybench::instruments::gen {

class Expression
{
public:
    Expression() = default;

    // Parses `text`. Check ok(); on failure error() is "<message> (<position>)".
    static Expression parse(const QString& text);

    bool ok() const { return m_ok; }
    QString error() const { return m_error; }        // empty when ok
    QString message() const { return m_message; }    // error without the position
    int errorPosition() const { return m_position; } // 1-based, 0 when ok
    QString text() const { return m_text; }

    // Value at x (NaN/inf possible, e.g. sqrt(-1)); 0 for an invalid expression.
    double evaluate(double x) const;
    // out[i] = f(xs[i]); out and xs have the same size (may alias).
    void evaluate(std::span<const double> xs, std::span<double> out) const;

private:
    struct Op {
        enum Kind { Constant, Variable, Negate, Add, Subtract, Multiply, Divide, Power, Call1, Call2 };
        Kind kind = Constant;
        double value = 0;
        int function = 0;
    };
    friend class ExpressionParser;

    QString m_text;
    bool m_ok = false;
    QString m_error;
    QString m_message;
    int m_position = 0;
    std::vector<Op> m_code;
    size_t m_stackDepth = 0;
};

}  // namespace mybench::instruments::gen
