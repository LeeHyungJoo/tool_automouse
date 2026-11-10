#include "../AutoClicker/Core/ChordState.h"

#include <cstdlib>
#include <iostream>

using State = autoclicker::ChordState;
using Action = State::Action;

namespace {
int checks = 0;

void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void Expect(State::Result result, Action action, bool suppress, const char* description)
{
    Check(result.action == action && result.suppress == suppress, description);
}

void Prefix(State& state, std::uint32_t alt = State::LeftAlt)
{
    Expect(state.Process(alt, true), Action::None, false, "Alt prefix passes through");
    Expect(state.Process(State::C, true), Action::None, false, "C prefix passes through");
}

void BasicAndRepeat()
{
    State state;
    Prefix(state);
    Expect(state.Process(State::P, true), Action::Start, true, "fresh P starts");
    Expect(state.Process(State::P, true), Action::None, true, "P autorepeat is consumed without restart");
    Expect(state.Process(State::LeftAlt, false), Action::None, false, "Alt release passes through");
    Expect(state.Process(State::P, false), Action::None, true, "P release pairs even after prefix released");
    Expect(state.Process(State::P, false), Action::None, false, "duplicate P release is not consumed");
    Expect(state.Process(State::S, true), Action::None, false, "S without Alt is ordinary typing");
    Expect(state.Process(State::S, false), Action::None, false, "ordinary S release passes");
    Expect(state.Process(State::LeftAlt, true), Action::None, false, "Alt may be pressed after C");
    Expect(state.Process(State::S, true), Action::Stop, true, "fresh S stops");
    Expect(state.Process(State::S, true), Action::None, true, "S autorepeat does not repeat stop");
    Expect(state.Process(State::S, false), Action::None, true, "recognized S key-up is consumed");
}

void PrefixOrderAndSeeding()
{
    State state;
    Expect(state.Process(State::P, true), Action::None, false, "P before prefix passes");
    Prefix(state);
    Expect(state.Process(State::P, true), Action::None, false, "holding old P cannot trigger by repeat");
    Expect(state.Process(State::P, false), Action::None, false, "old P release passes");
    Expect(state.Process(State::P, true), Action::Start, true, "repressing P after prefix starts");

    state.Reset();
    state.SeedPressed(State::RightAlt);
    state.SeedPressed(State::C);
    state.SeedPressed(State::P);
    Expect(state.Process(State::P, true), Action::None, false, "startup held P does not synthesize start");
    Expect(state.Process(State::P, false), Action::None, false, "startup held P release passes");
    Expect(state.Process(State::P, true), Action::Start, true, "right Alt works after seeded P released");
    Expect(state.Process(State::P, false), Action::None, true, "seeded prefix later action is paired");
    Expect(state.Process(State::RightAlt, false), Action::None, false, "right Alt releases");
    Expect(state.Process(State::P, true), Action::None, false, "no stale Alt after release");
}

void ExtraModifiers()
{
    constexpr std::uint32_t modifiers[] = { State::Control, State::LeftControl,
        State::RightControl, State::Shift, State::LeftShift, State::RightShift,
        State::LeftWin, State::RightWin };
    for (const auto modifier : modifiers) {
        State state;
        Prefix(state, State::RightAlt);
        state.Process(modifier, true);
        Expect(state.Process(State::P, true), Action::None, false, "extra modifier blocks start including AltGr");
        state.Process(modifier, false);
        Expect(state.Process(State::P, true), Action::None, false, "modifier release with held P does not start");
        state.Process(State::P, false);
        Expect(state.Process(State::P, true), Action::Start, true, "new P after extra modifier release starts");
        state.Process(modifier, true);
        Expect(state.Process(State::P, false), Action::None, true, "added modifier does not lose matching key-up");
        Expect(state.Process(State::S, true), Action::None, false, "extra modifier also blocks stop");
    }
}

void BothTerminalKeys()
{
    State state;
    Prefix(state);
    Expect(state.Process(State::P, true), Action::Start, true, "P starts before S");
    Expect(state.Process(State::S, true), Action::Stop, true, "S stops when P is held");
    Expect(state.Process(State::S, false), Action::None, true, "S release is paired");
    Expect(state.Process(State::P, true), Action::None, true, "releasing S cannot restart held P");
    Expect(state.Process(State::P, false), Action::None, true, "held P eventually releases");
    Expect(state.Process(State::P, true), Action::Start, true, "fresh P can explicitly restart");

    state.Reset();
    Prefix(state);
    Expect(state.Process(State::S, true), Action::Stop, true, "S first stops");
    Expect(state.Process(State::P, true), Action::Stop, true, "stop wins when P pressed with S held");
    Expect(state.Process(State::S, false), Action::None, true, "stop key-up consumed in reverse order");
    Expect(state.Process(State::P, true), Action::None, true, "P repeat still cannot restart after S release");
    Expect(state.Process(State::P, false), Action::None, true, "P consumed up in reverse order");
}

void IndependentAltAndRecovery()
{
    State state;
    Prefix(state);
    state.Process(State::RightAlt, true);
    state.Process(State::LeftAlt, false);
    Expect(state.Process(State::P, true), Action::Start, true, "one Alt may release while the other stays held");
    state.CancelSuppression(State::P);
    Expect(state.Process(State::P, true), Action::None, false, "failed delivery passes subsequent repeats");
    Expect(state.Process(State::P, false), Action::None, false, "failed delivery passes matching key-up");
    state.Reset();
    Expect(state.Process(State::P, false), Action::None, false, "reset clears old suppression");
    Expect(state.Process(999, true), Action::None, false, "out-of-range key ignored");
    state.SeedPressed(999);
    state.CancelSuppression(999);
    Expect(state.Process(State::P, true), Action::None, false, "reset clears old pressed prefix");
}
} // namespace

int main()
{
    BasicAndRepeat();
    PrefixOrderAndSeeding();
    ExtraModifiers();
    BothTerminalKeys();
    IndependentAltAndRecovery();
    std::cout << "chord_tests: " << checks << " checks passed\n";
}
