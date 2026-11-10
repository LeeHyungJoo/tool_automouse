#include "../AutoClicker/Core/Interval.h"

#include <cstdlib>
#include <iostream>
#include <string>

using autoclicker::IntervalUnit;
using autoclicker::ParseInterval;

namespace
{
unsigned int checks = 0;

void Expect(std::wstring_view input, IntervalUnit unit,
    std::optional<unsigned int> expected)
{
    ++checks;
    if (ParseInterval(input, unit) != expected)
    {
        std::wcerr << L"Interval parsing failed for [" << input << L"]\n";
        std::exit(EXIT_FAILURE);
    }
}
}

int main()
{
    constexpr auto ms = IntervalUnit::Milliseconds;
    constexpr auto seconds = IntervalUnit::Seconds;
    Expect(L"1", ms, 1);
    Expect(L"1000", ms, 1000);
    Expect(L"3600000", ms, 3600000);
    Expect(L"000010", ms, 10);
    Expect(L" \t250\r\n", ms, 250);
    Expect(L"0", ms, std::nullopt);
    Expect(L"3600001", ms, std::nullopt);
    Expect(L"999999999999999999999999", ms, std::nullopt);
    Expect(std::wstring(20000, L'9'), ms, std::nullopt);
    Expect(L"1.0", ms, std::nullopt);
    Expect(L"", ms, std::nullopt);
    Expect(L" \t\n", ms, std::nullopt);
    Expect(L"+1", ms, std::nullopt);
    Expect(L"-1", ms, std::nullopt);
    Expect(L"1e3", ms, std::nullopt);
    Expect(L"1,000", ms, std::nullopt);
    Expect(L"0x10", ms, std::nullopt);
    Expect(L"1 ms", ms, std::nullopt);
    Expect(L"1 0", ms, std::nullopt);
    Expect(L"１２", ms, std::nullopt);
    Expect(L"\u00a01", ms, std::nullopt);
    Expect(std::wstring_view(L"1\0", 2), ms, std::nullopt);

    Expect(L"0.001", seconds, 1);
    Expect(L"0.01", seconds, 10);
    Expect(L"0.1", seconds, 100);
    Expect(L"1", seconds, 1000);
    Expect(L"1.25", seconds, 1250);
    Expect(L"12.345", seconds, 12345);
    Expect(L"0001.010", seconds, 1010);
    Expect(L" \r2.005\t ", seconds, 2005);
    Expect(L"3599.999", seconds, 3599999);
    Expect(L"3600", seconds, 3600000);
    Expect(L"3600.000", seconds, 3600000);
    Expect(L"0", seconds, std::nullopt);
    Expect(L"0.000", seconds, std::nullopt);
    Expect(L"0.0001", seconds, std::nullopt);
    Expect(L"1.0000", seconds, std::nullopt);
    Expect(L"3600.001", seconds, std::nullopt);
    Expect(L"3601", seconds, std::nullopt);
    Expect(L".001", seconds, std::nullopt);
    Expect(L"1.", seconds, std::nullopt);
    Expect(L"1..0", seconds, std::nullopt);
    Expect(L"1,5", seconds, std::nullopt);
    Expect(L"1.2s", seconds, std::nullopt);
    Expect(L"-0.001", seconds, std::nullopt);
    Expect(L"1. 2", seconds, std::nullopt);
    Expect(L"100000000000000000", seconds, std::nullopt);
    Expect(L"1", static_cast<IntervalUnit>(20), std::nullopt);

    // Every millisecond in the first second survives a three-decimal conversion.
    for (unsigned int value = 1; value < 1000; ++value)
    {
        std::wstring fraction = std::to_wstring(value);
        fraction.insert(0, 3 - fraction.size(), L'0');
        Expect(L"0." + fraction, seconds, value);
    }
    std::cout << "Interval tests passed (" << checks << " checks).\n";
}
