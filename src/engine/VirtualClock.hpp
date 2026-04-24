#pragma once
#include <cstddef>

// =============================================================================
// VirtualClock
//
// Models the hardware period clock that drives a PipeWire graph.
//
// In PipeWire, the clock advances by exactly one period (chunk_size/sample_rate)
// on every hardware interrupt — regardless of whether your plugin finished on
// time. This is what creates deadline debt: if your FUT takes 1.5× the period,
// the NEXT chunk's deadline is already 0.5× periods closer.
//
// Usage per chunk:
//   clock.start_chunk()                 ← called just before FUT
//   ... FUT runs ...
//   double debt = clock.end_chunk(wall_us)  ← advances clock, returns current debt
//   bool xrun  = clock.is_in_debt()
//
// Debt semantics:
//   debt > 0  → we are behind the virtual clock by `debt` µs.
//               In real PipeWire this chunk would have xrun'd and the next
//               deadline is already partially consumed.
//   debt <= 0 → we are on time (debt is how much headroom we have).
// =============================================================================

class VirtualClock {
public:
    // period_us : the theoretical period in µs (chunk_size / sample_rate * 1e6)
    // offset_us : additional µs to subtract from budget (driver overhead model)
    explicit VirtualClock(double period_us, double offset_us = 0.0);

    // Reset to t=0. Call before engine loop starts.
    void reset();

    // Record the wall time FUT actually took for this chunk.
    // Advances the virtual clock by one period.
    // Returns the updated cumulative debt in µs (positive = behind).
    double end_chunk(double wall_us);

    // True if we are currently behind the virtual clock.
    bool   is_in_debt() const { return m_debt_us > 0.0; }

    // Current cumulative debt in µs.
    double debt_us()    const { return m_debt_us; }

    // Effective deadline for the current chunk (period - offset).
    double deadline_us() const { return m_deadline_us; }

private:
    double m_period_us;     // raw period = chunk_size / sample_rate * 1e6
    double m_deadline_us;   // effective budget = period - offset
    double m_debt_us = 0.0; // accumulated time debt (positive = behind)
};
