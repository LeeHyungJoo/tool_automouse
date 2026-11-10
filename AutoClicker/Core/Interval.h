#pragma once

#include <optional>
#include <string_view>

namespace autoclicker
{
inline constexpr unsigned int kMinIntervalMs = 1;
inline constexpr unsigned int kMaxIntervalMs = 3'600'000;
inline constexpr unsigned int kDefaultIntervalMs = 1'000;

enum class IntervalUnit
{
    Milliseconds,
    Seconds
};

inline bool IsAsciiWhitespace(wchar_t ch) noexcept
{
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' ||
        ch == L'\f' || ch == L'\v';
}

// Parse without floating-point rounding or locale-dependent conversion.
// Seconds accept a decimal point and one to three fractional digits.
inline std::optional<unsigned int> ParseInterval(
    std::wstring_view text, IntervalUnit unit) noexcept
{
    if (unit != IntervalUnit::Milliseconds && unit != IntervalUnit::Seconds)
        return std::nullopt;

    while (!text.empty() && IsAsciiWhitespace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && IsAsciiWhitespace(text.back()))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;

    const unsigned int wholeLimit = unit == IntervalUnit::Milliseconds
        ? kMaxIntervalMs : kMaxIntervalMs / 1000;
    unsigned int whole = 0;
    std::size_t offset = 0;
    for (; offset < text.size() && text[offset] >= L'0' && text[offset] <= L'9'; ++offset)
    {
        const unsigned int digit = static_cast<unsigned int>(text[offset] - L'0');
        if (whole > wholeLimit / 10 ||
            (whole == wholeLimit / 10 && digit > wholeLimit % 10))
            return std::nullopt;
        whole = whole * 10 + digit;
    }
    if (offset == 0)
        return std::nullopt;

    unsigned int fractionMs = 0;
    if (offset < text.size())
    {
        if (unit != IntervalUnit::Seconds || text[offset++] != L'.')
            return std::nullopt;
        const auto fractionDigits = text.size() - offset;
        if (fractionDigits < 1 || fractionDigits > 3)
            return std::nullopt;
        for (; offset < text.size(); ++offset)
        {
            if (text[offset] < L'0' || text[offset] > L'9')
                return std::nullopt;
            fractionMs = fractionMs * 10 + static_cast<unsigned int>(text[offset] - L'0');
        }
        for (std::size_t count = fractionDigits; count < 3; ++count)
            fractionMs *= 10;
    }

    const unsigned int intervalMs = unit == IntervalUnit::Milliseconds
        ? whole : whole * 1000 + fractionMs;
    if (intervalMs < kMinIntervalMs || intervalMs > kMaxIntervalMs)
        return std::nullopt;
    return intervalMs;
}
}
