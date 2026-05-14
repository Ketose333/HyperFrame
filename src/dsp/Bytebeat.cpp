#include "dsp/Bytebeat.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace hyperframe::dsp {
namespace {

class BytebeatParser {
public:
    BytebeatParser(std::string_view expression, std::uint32_t t)
        : expression_(expression)
        , t_(t) {}

    bool parse(std::int64_t& result) {
        result = parseConditional();
        skipWhitespace();
        return ok_ && position_ == expression_.size();
    }

private:
    void skipWhitespace() {
        while (position_ < expression_.size()) {
            const auto c = expression_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++position_;
        }
    }

    bool consume(char c) {
        skipWhitespace();
        if (position_ < expression_.size() && expression_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    bool consume(std::string_view token) {
        skipWhitespace();
        if (expression_.substr(position_, token.size()) == token) {
            position_ += token.size();
            return true;
        }
        return false;
    }

    std::int64_t parseConditional() {
        auto value = parseLogicalOr();
        if (consume('?')) {
            const auto trueValue = parseConditional();
            if (!consume(':')) {
                ok_ = false;
                return 0;
            }
            const auto falseValue = parseConditional();
            value = value != 0 ? trueValue : falseValue;
        }
        return value;
    }

    std::int64_t parseLogicalOr() {
        auto value = parseLogicalAnd();
        while (consume("||")) {
            const auto rhs = parseLogicalAnd();
            value = (value != 0 || rhs != 0) ? 1 : 0;
        }
        return value;
    }

    std::int64_t parseLogicalAnd() {
        auto value = parseBitwiseOr();
        while (consume("&&")) {
            const auto rhs = parseBitwiseOr();
            value = (value != 0 && rhs != 0) ? 1 : 0;
        }
        return value;
    }

    std::int64_t parseBitwiseOr() {
        auto value = parseBitwiseXor();
        while (true) {
            skipWhitespace();
            if (expression_.substr(position_, 2) == "||" || !consume('|')) {
                break;
            }
            value |= parseBitwiseXor();
        }
        return value;
    }

    std::int64_t parseBitwiseXor() {
        auto value = parseBitwiseAnd();
        while (consume('^')) {
            value ^= parseBitwiseAnd();
        }
        return value;
    }

    std::int64_t parseBitwiseAnd() {
        auto value = parseEquality();
        while (true) {
            skipWhitespace();
            if (expression_.substr(position_, 2) == "&&" || !consume('&')) {
                break;
            }
            value &= parseEquality();
        }
        return value;
    }

    std::int64_t parseEquality() {
        auto value = parseRelational();
        while (true) {
            if (consume("==")) {
                value = value == parseRelational() ? 1 : 0;
            } else if (consume("!=")) {
                value = value != parseRelational() ? 1 : 0;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parseRelational() {
        auto value = parseShift();
        while (true) {
            skipWhitespace();
            if (expression_.substr(position_, 2) == "<<" || expression_.substr(position_, 2) == ">>") {
                break;
            }
            if (consume("<=")) {
                value = value <= parseShift() ? 1 : 0;
            } else if (consume(">=")) {
                value = value >= parseShift() ? 1 : 0;
            } else if (consume('<')) {
                value = value < parseShift() ? 1 : 0;
            } else if (consume('>')) {
                value = value > parseShift() ? 1 : 0;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parseShift() {
        auto value = parseAdditive();
        while (true) {
            if (consume("<<")) {
                const auto amount = std::clamp<std::int64_t>(parseAdditive(), 0, 62);
                value <<= amount;
            } else if (consume(">>")) {
                const auto amount = std::clamp<std::int64_t>(parseAdditive(), 0, 62);
                value >>= amount;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parseAdditive() {
        auto value = parseMultiplicative();
        while (true) {
            if (consume('+')) {
                value += parseMultiplicative();
            } else if (consume('-')) {
                value -= parseMultiplicative();
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parseMultiplicative() {
        auto value = parseUnary();
        while (true) {
            if (consume('*')) {
                value *= parseUnary();
            } else if (consume('/')) {
                const auto rhs = parseUnary();
                value = rhs == 0 ? 0 : value / rhs;
            } else if (consume('%')) {
                const auto rhs = parseUnary();
                value = rhs == 0 ? 0 : value % rhs;
            } else {
                break;
            }
        }
        return value;
    }

    std::int64_t parseUnary() {
        if (consume('+')) {
            return parseUnary();
        }
        if (consume('-')) {
            return -parseUnary();
        }
        if (consume('~')) {
            return ~parseUnary();
        }
        if (consume('!')) {
            return parseUnary() == 0 ? 1 : 0;
        }
        return parsePrimary();
    }

    std::int64_t parsePrimary() {
        skipWhitespace();
        if (consume('(')) {
            const auto value = parseConditional();
            if (!consume(')')) {
                ok_ = false;
            }
            return value;
        }

        if (position_ < expression_.size() && expression_[position_] == 't') {
            ++position_;
            return static_cast<std::int64_t>(t_);
        }

        return parseNumber();
    }

    std::int64_t parseNumber() {
        skipWhitespace();
        if (position_ >= expression_.size()) {
            ok_ = false;
            return 0;
        }

        auto base = 10;
        if (position_ + 2 <= expression_.size()
            && expression_[position_] == '0'
            && (expression_[position_ + 1] == 'x' || expression_[position_ + 1] == 'X')) {
            base = 16;
            position_ += 2;
        }

        auto value = std::int64_t { 0 };
        auto parsedDigit = false;
        while (position_ < expression_.size()) {
            const auto c = expression_[position_];
            int digit = -1;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = 10 + c - 'a';
            } else if (c >= 'A' && c <= 'F') {
                digit = 10 + c - 'A';
            }

            if (digit < 0 || digit >= base) {
                break;
            }

            parsedDigit = true;
            if (value <= (std::numeric_limits<std::int64_t>::max() - digit) / base) {
                value = (value * base) + digit;
            }
            ++position_;
        }

        if (!parsedDigit) {
            ok_ = false;
        }
        return value;
    }

    std::string_view expression_;
    std::size_t position_ = 0;
    std::uint32_t t_ = 0;
    bool ok_ = true;
};

} // namespace

bool evaluateBytebeatExpression(std::string_view expression, std::uint32_t t, std::int64_t& result) {
    BytebeatParser parser(expression, t);
    return parser.parse(result);
}

bool renderBytebeatTicks(std::string_view expression,
                         std::uint32_t startTick,
                         std::size_t tickCount,
                         std::vector<float>& samples) {
    samples.clear();
    if (expression.empty() || tickCount < 8 || tickCount > 1048576) {
        return false;
    }

    samples.resize(tickCount);
    for (std::size_t i = 0; i < tickCount; ++i) {
        auto value = std::int64_t { 0 };
        if (!evaluateBytebeatExpression(expression, startTick + static_cast<std::uint32_t>(i), value)) {
            samples.clear();
            return false;
        }

        const auto byte = static_cast<int>(value & 0xFF);
        samples[i] = (static_cast<float>(byte) / 127.5f) - 1.0f;
    }

    return true;
}

bool renderBytebeat(std::string_view expression,
                    double sampleRate,
                    double startSeconds,
                    double durationSeconds,
                    std::vector<float>& samples) {
    samples.clear();
    if (expression.empty()) {
        return false;
    }

    const auto safeSampleRate = std::clamp(sampleRate, 1.0, 2000000.0);
    const auto safeStartSeconds = std::max(0.0, startSeconds);
    const auto minimumDurationSeconds = 8.0 / safeSampleRate;
    const auto safeDurationSeconds = std::clamp(durationSeconds, minimumDurationSeconds, 30.0);
    const auto sampleCount = static_cast<std::size_t>(std::round(safeDurationSeconds * safeSampleRate));
    if (sampleCount < 8) {
        return false;
    }

    const auto startT = static_cast<std::uint32_t>(std::round(safeStartSeconds * safeSampleRate));
    return renderBytebeatTicks(expression, startT, sampleCount, samples);
}

} // namespace hyperframe::dsp
