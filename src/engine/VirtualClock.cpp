#include "engine/VirtualClock.hpp"
#include <algorithm>  // std::max

VirtualClock::VirtualClock(double period_us, double offset_us)
    : m_period_us(period_us)
    , m_deadline_us(period_us - offset_us)
    , m_debt_us(0.0)
{}

void VirtualClock::reset() {
    m_debt_us = 0.0;
}

double VirtualClock::end_chunk(double wall_us) {
    // The virtual clock advances by exactly one period.
    // If FUT took longer than the deadline, the excess becomes debt.
    // Debt from previous chunks is already carried in m_debt_us.
    //
    // Derivation:
    //   new_debt = old_debt + wall_us - period_us
    //
    // Why period_us and not deadline_us here?
    //   The deadline_us is the *budget we give FUT* (period - offset).
    //   But the clock itself advances by a full period — the offset models
    //   overhead that exists regardless, not extra time FUT gets.
    //   So debt accumulates against the real clock (period), not the budget.
    //
    m_debt_us = m_debt_us + wall_us - m_period_us;

    // Debt cannot go below zero — being early doesn't give you credit
    // towards a future late chunk. Each period is independent on the real clock.
    m_debt_us = std::max(m_debt_us, 0.0);

    return m_debt_us;
}
