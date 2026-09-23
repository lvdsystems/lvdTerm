#pragma once

// Pure backoff math for auto-reconnect (see TerminalView, which is what
// actually drives reconnection - this header has no state of its own).
// Same policy regardless of which Transport is involved: reconnection
// only ever depends on the shared Transport::State machine, not on
// anything transport-specific.
namespace ReconnectPolicy
{

// After this many failed attempts, TerminalView stops retrying
// automatically and leaves a "give up" message with a manual retry
// button instead.
constexpr int kMaxAttempts = 6;

// Exponential backoff with a 30s ceiling: 1, 2, 4, 8, 16, 30 (for
// attempt = 1..6). `attempt` is 1-based.
inline int delaySecondsForAttempt(int attempt)
{
    if (attempt <= 0)
        return 1;
    const int uncapped = 1 << (attempt - 1); // may overflow for silly attempt values, hence the cap below
    return uncapped > 30 || uncapped <= 0 ? 30 : uncapped;
}

} // namespace ReconnectPolicy
