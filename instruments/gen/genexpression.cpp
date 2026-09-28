#include "genexpression.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mybench::instruments::gen {

namespace {

enum Function1 { Abs, Acos, Asin, Atan, Ceil, Cos, Cosh, Exp, Floor, Ln, Log, Sign, Sin, Sinh, Sqrt, Tan, Tanh };
enum Function2 { Max, Min, Mod };

struct FunctionName {
    const char* name;
    int arity;
    int id;
};

constexpr FunctionName kFunctions[] = {
    {"abs", 1, Abs},   {"acos", 1, Acos}, {"asin", 1, Asin}, {"atan", 1, Atan}, {"ceil", 1, Ceil},
    {"cos", 1, Cos},   {"cosh", 1, Cosh}, {"exp", 1, Exp},   {"floor", 1, Floor}, {"ln", 1, Ln},
    {"log", 1, Log},   {"max", 2, Max},   {"min", 2, Min},   {"mod", 2, Mod},   {"sign", 1, Sign},
    {"sin", 1, Sin},   {"sinh", 1, Sinh}, {"sqrt", 1, Sqrt}, {"tan", 1, Tan},   {"tanh", 1, Tanh},
};

double call1(int function, double a)
{
    switch (function) {
    case Abs: return std::abs(a);
    case Acos: return std::acos(a);
    case Asin: return std::asin(a);
    case Atan: return std::atan(a);
    case Ceil: return std::ceil(a);
    case Cos: return std::cos(a);
    case Cosh: return std::cosh(a);
    case Exp: return std::exp(a);
    case Floor: return std::floor(a);
    case Ln: return std::log(a);
    case Log: return std::log10(a);
    case Sign: return a > 0 ? 1.0 : a < 0 ? -1.0 : 0.0;
    case Sin: return std::sin(a);
    case Sinh: return std::sinh(a);
    case Sqrt: return std::sqrt(a);
    case Tan: return std::tan(a);
    case Tanh: return std::tanh(a);
    }
    return 0;
}

double call2(int function, double a, double b)
{
    switch (function) {
    case Max: return std::max(a, b);
    case Min: return std::min(a, b);
    case Mod: return a - b * std::floor(a / b);
    }
    return 0;
}

QString tr(const char* text)
{
    return QCoreApplication::translate("Expression", text);
}

}  // namespace

// Recursive-descent parser emitting postfix code.
class ExpressionParser
{
public:
    explicit ExpressionParser(const QString& text) : m_text(text) {}

    Expression run()
    {
        Expression result;
        result.m_text = m_text;
        try {
            skipSpace();
            if (atEnd())
                fail(tr("Empty expression"), position());
            parseSum();
            skipSpace();
            if (!atEnd()) {
                if (m_text[m_pos] == u')')
                    fail(tr("Unexpected ')'"), position());
                fail(tr("Unexpected '%1'").arg(m_text[m_pos]), position());
            }
            result.m_code = std::move(m_code);
            result.m_stackDepth = m_maxDepth;
            result.m_ok = true;
        } catch (const Failure& failure) {
            result.m_ok = false;
            result.m_message = failure.message;
            result.m_position = failure.position;
            result.m_error = QStringLiteral("%1 (%2)").arg(failure.message).arg(failure.position);
        }
        return result;
    }

private:
    struct Failure {
        QString message;
        int position;
    };
    using Op = Expression::Op;

    [[noreturn]] void fail(const QString& message, int position) { throw Failure{message, position}; }
    bool atEnd() const { return m_pos >= m_text.size(); }
    int position() const { return int(m_pos) + 1; }
    void skipSpace()
    {
        while (!atEnd() && m_text[m_pos].isSpace())
            ++m_pos;
    }
    bool accept(QChar c)
    {
        skipSpace();
        if (!atEnd() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }
    void push(Op op, int stackDelta)
    {
        m_code.push_back(op);
        m_depth += stackDelta;
        m_maxDepth = std::max(m_maxDepth, size_t(std::max(0, m_depth)));
    }

    void parseSum()
    {
        parseProduct();
        for (;;) {
            if (accept(u'+')) {
                parseProduct();
                push({Op::Add}, -1);
            } else if (accept(u'-')) {
                parseProduct();
                push({Op::Subtract}, -1);
            } else {
                return;
            }
        }
    }

    void parseProduct()
    {
        parseUnary();
        for (;;) {
            if (accept(u'*')) {
                parseUnary();
                push({Op::Multiply}, -1);
            } else if (accept(u'/')) {
                parseUnary();
                push({Op::Divide}, -1);
            } else {
                return;
            }
        }
    }

    void parseUnary()
    {
        if (accept(u'-')) {
            parseUnary();
            push({Op::Negate}, 0);
        } else if (accept(u'+')) {
            parseUnary();
        } else {
            parsePower();
        }
    }

    void parsePower()
    {
        parsePrimary();
        if (accept(u'^')) {
            parseUnary();   // right-associative; allows 2^-1
            push({Op::Power}, -1);
        }
    }

    void parsePrimary()
    {
        skipSpace();
        if (atEnd())
            fail(tr("Unexpected end of expression"), position());
        const QChar c = m_text[m_pos];
        if (c.isDigit() || c == u'.') {
            parseNumber();
            return;
        }
        if (c.isLetter() || c == u'_') {
            parseName();
            return;
        }
        if (c == u'(') {
            const int open = position();
            ++m_pos;
            parseSum();
            if (!accept(u')')) {
                skipSpace();
                Q_UNUSED(open)
                fail(tr("Missing ')'"), position());
            }
            return;
        }
        fail(tr("Unexpected '%1'").arg(c), position());
    }

    void parseNumber()
    {
        const qsizetype start = m_pos;
        while (!atEnd() && (m_text[m_pos].isDigit() || m_text[m_pos] == u'.'))
            ++m_pos;
        if (!atEnd() && (m_text[m_pos] == u'e' || m_text[m_pos] == u'E')) {
            qsizetype p = m_pos + 1;
            if (p < m_text.size() && (m_text[p] == u'+' || m_text[p] == u'-'))
                ++p;
            if (p < m_text.size() && m_text[p].isDigit()) {
                m_pos = p;
                while (!atEnd() && m_text[m_pos].isDigit())
                    ++m_pos;
            }
        }
        bool ok = false;
        const double value = m_text.mid(start, m_pos - start).toDouble(&ok);
        if (!ok)
            fail(tr("Invalid number '%1'").arg(m_text.mid(start, m_pos - start)), int(start) + 1);
        push({Op::Constant, value}, 1);
    }

    void parseName()
    {
        const qsizetype start = m_pos;
        while (!atEnd() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == u'_'))
            ++m_pos;
        const QString name = m_text.mid(start, m_pos - start);
        const QString lower = name.toLower();
        skipSpace();
        const bool call = !atEnd() && m_text[m_pos] == u'(';
        if (!call) {
            if (lower == u"x")
                push({Op::Variable}, 1);
            else if (lower == u"pi")
                push({Op::Constant, std::numbers::pi}, 1);
            else if (lower == u"e")
                push({Op::Constant, std::numbers::e}, 1);
            else if (findFunction(lower))
                fail(tr("Missing '(' after '%1'").arg(name), position());
            else
                fail(tr("Unknown name '%1'").arg(name), int(start) + 1);
            return;
        }
        const FunctionName* function = findFunction(lower);
        if (!function)
            fail(tr("Unknown function '%1'").arg(name), int(start) + 1);
        ++m_pos;   // '('
        parseSum();
        int arguments = 1;
        while (accept(u',')) {
            parseSum();
            ++arguments;
        }
        if (!accept(u')')) {
            skipSpace();
            fail(tr("Missing ')'"), position());
        }
        if (arguments != function->arity)
            fail(function->arity == 1 ? tr("'%1' takes one argument").arg(name)
                                      : tr("'%1' takes two arguments").arg(name),
                 int(start) + 1);
        if (function->arity == 1)
            push({Op::Call1, 0, function->id}, 0);
        else
            push({Op::Call2, 0, function->id}, -1);
    }

    static const FunctionName* findFunction(const QString& lower)
    {
        for (const FunctionName& f : kFunctions) {
            if (lower == QLatin1StringView(f.name))
                return &f;
        }
        return nullptr;
    }

    QString m_text;
    qsizetype m_pos = 0;
    std::vector<Op> m_code;
    int m_depth = 0;
    size_t m_maxDepth = 0;
};

Expression Expression::parse(const QString& text)
{
    return ExpressionParser(text).run();
}

double Expression::evaluate(double x) const
{
    double out = 0;
    evaluate(std::span<const double>(&x, 1), std::span<double>(&out, 1));
    return out;
}

void Expression::evaluate(std::span<const double> xs, std::span<double> out) const
{
    if (!m_ok) {
        std::fill(out.begin(), out.end(), 0.0);
        return;
    }
    std::vector<double> stack(std::max<size_t>(m_stackDepth, 1) + 1);
    for (size_t i = 0; i < xs.size() && i < out.size(); ++i) {
        const double x = xs[i];
        size_t top = 0;   // number of values on the stack
        for (const Op& op : m_code) {
            switch (op.kind) {
            case Op::Constant: stack[top++] = op.value; break;
            case Op::Variable: stack[top++] = x; break;
            case Op::Negate: stack[top - 1] = -stack[top - 1]; break;
            case Op::Add: --top; stack[top - 1] += stack[top]; break;
            case Op::Subtract: --top; stack[top - 1] -= stack[top]; break;
            case Op::Multiply: --top; stack[top - 1] *= stack[top]; break;
            case Op::Divide: --top; stack[top - 1] /= stack[top]; break;
            case Op::Power: --top; stack[top - 1] = std::pow(stack[top - 1], stack[top]); break;
            case Op::Call1: stack[top - 1] = call1(op.function, stack[top - 1]); break;
            case Op::Call2: --top; stack[top - 1] = call2(op.function, stack[top - 1], stack[top]); break;
            }
        }
        out[i] = top > 0 ? stack[top - 1] : 0.0;
    }
}

}  // namespace mybench::instruments::gen
