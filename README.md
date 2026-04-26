# pw-sim — PipeWire Plugin Simulator

A simulator that models the real-time constraints of a PipeWire filter node.
You provide an audio file and a C++ function (your ML model or any DSP code).
pw-sim calls your function chunk by chunk — exactly like PipeWire would — measures how long it takes, and writes the result to an output audio file you can listen to.

---

## What it tells you

- Whether your function can process audio within the PipeWire deadline
- What the output would actually sound like when it can't (glitches preserved)
- Whether your function is doing RT-hostile things: blocking on IO, getting preempted, allocating memory
- p50/p95/p99 latency distribution, overrun rate, cumulative deadline debt
- A CSV log with per-chunk measurements for plotting

---

## Requirements

```bash
sudo apt install libsndfile1-dev cmake build-essential
```

---

## Build

```bash
cd pw-sim
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is at `build/pw-sim`.

For a debug build with address sanitizer:
```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

---

## Quick start

```bash
./build/pw-sim input.wav output.wav
```

- `input.wav` — any WAV file (16-bit, 24-bit, 32-bit int, or 32-bit float)
- `output.wav` — result audio written here after the simulation

The log CSV is always written to `pw-sim.log.csv` in the current directory.

---

## How to plug in your function

Open `src/main.cpp` and find `make_active_fut()`. Replace the active stub with your own lambda:

```cpp
static FutFn make_active_fut() {
    return [](const float* input,
              float*       output,
              size_t       frames,
              size_t       channels,
              size_t       chunk_index)
    {
        // Your ML model or DSP code goes here.
        // Rules — same as a real PipeWire plugin:
        //   - Must return before the deadline (chunk_size / sample_rate seconds)
        //   - Do not call malloc, sleep, or any blocking IO
        //   - Do not write to `input` — it is read-only
        //   - Write your result into `output` (same size as input)
        (void)chunk_index;

        for (size_t i = 0; i < frames * channels; ++i)
            output[i] = input[i];  // replace with your processing
    };
}
```

Then rebuild:
```bash
cmake --build build
./build/pw-sim input.wav output.wav
```

### Buffer layout

Both `input` and `output` are **interleaved float32**:

```
Stereo:  [L0, R0, L1, R1, ..., L(frames-1), R(frames-1)]
Mono:    [S0, S1, S2, ..., S(frames-1)]
```

Total floats per buffer: `frames * channels`

---

## How to change settings

All settings are in `src/main.cpp` inside `make_config()`. Edit and rebuild.
In Tier 3, settings will be loaded from a TOML file with no recompile needed.

### Essential settings

```cpp
cfg.chunk_size    = 256;    // frames per callback — try 64, 128, 256, 512, 1024
cfg.sample_rate   = 48000;  // Hz — must match your input file
cfg.warmup_chunks = 4;      // FUT is called N times before measurement starts
```

**Deadline** is computed automatically:
```
deadline = chunk_size / sample_rate * 1,000,000  microseconds
256 / 48000 * 1e6 = 5333 µs
128 / 48000 * 1e6 = 2666 µs
```

### Clock mode

```cpp
cfg.clock_mode = ClockMode::SEQUENTIAL;  // default
cfg.clock_mode = ClockMode::REALTIME;
```

| Mode | Behaviour |
|---|---|
| `SEQUENTIAL` | FUT always runs to completion. Output is always FUT's result. Overruns are flagged in the log but do not affect audio. Use this to test correctness first. |
| `REALTIME` | A virtual clock advances by one period per chunk regardless of FUT time. If FUT overruns, output is replaced by the xrun policy. Cumulative deadline debt is tracked. Use this to simulate actual PipeWire behaviour. |

### Xrun policy (REALTIME mode only)

What goes into the output audio when FUT misses the deadline:

```cpp
cfg.xrun_policy = XrunPolicy::ZEROS;        // silence — what PipeWire actually does
cfg.xrun_policy = XrunPolicy::REPEAT_LAST;  // repeat previous good output chunk
cfg.xrun_policy = XrunPolicy::PASSTHROUGH;  // output dry/unprocessed audio
```

Listen to the output file after a REALTIME run to hear what each policy sounds like.

### Pre-fill policy

What the output buffer contains *before* your function is called each chunk:

```cpp
cfg.pre_fill = PreFillPolicy::ZEROS;        // realistic — PipeWire pre-zeros output
cfg.pre_fill = PreFillPolicy::PASSTHROUGH;  // simulates an optimistic memcpy pattern
```

**Optimistic copy pattern** — a real plugin can do this to protect against overruns:
```cpp
// Inside your process() callback:
memcpy(output, input, frames * channels * sizeof(float));  // safety net first
// now run your model, overwriting output with processed audio
// if you overrun the deadline, output already has dry audio instead of silence
```
Setting `pre_fill = PASSTHROUGH` simulates this without modifying your FUT.

### Deadline offset

Subtract microseconds from the budget to simulate driver and graph scheduling overhead:

```cpp
cfg.deadline_offset_us = 0.0;    // full theoretical period (default)
cfg.deadline_offset_us = 200.0;  // model 200 µs of real-world overhead
```

### Probes

```cpp
cfg.probe_cpu_time          = true;  // CLOCK_THREAD_CPUTIME_ID alongside wall clock
cfg.probe_context_switches  = true;  // read /proc/self/status per chunk
cfg.probe_page_faults       = true;  // read getrusage(RUSAGE_THREAD) per chunk
```

These add ~2–5 µs of overhead per chunk. All default to on.

### Real-time scheduling

```cpp
cfg.warn_cpu_governor = true;   // warns if CPU is not on 'performance' governor
cfg.try_rt_priority   = false;  // attempt SCHED_FIFO (needs root or CAP_SYS_NICE)
```

For the most accurate measurements, before running:
```bash
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

---

## The three built-in stubs

These are for verifying the simulator works before you plug in your real function.

### PassthroughFut
```cpp
return make_passthrough_fut();
```
Output == input. No processing. Expected: 0 overruns, output sounds identical to input.
Use this to confirm your audio pipeline is correct end-to-end.

### GainFut
```cpp
return make_gain_fut(0.5f);   // 0.5 = half volume (-6dB)
```
Multiplies every sample by a gain factor. Expected: 0 overruns, output sounds quieter/louder.
Use this to confirm audio alteration works correctly.

### SlowFut
```cpp
return make_slow_fut(8000, 0);   // sleep 8000µs every chunk  → every chunk overruns
return make_slow_fut(8000, 20);  // sleep 8000µs every 20th chunk → ~5% overrun rate
```
Passthrough with an artificial sleep to deliberately trigger overruns.
Use this to hear what xrun artifacts sound like in the output audio.

---

## Reading the output

### Stdout — per-chunk log

```
chunk    frames  wall(us)   budgt(us)  ratio    flags     debt(us)   status
----------------------------------------------------------------------
0        256     1243.1     5333.3     0.233    -         0.0        OK
1        256     8320.5     5333.3     1.560    V X       2987.2     OVERRUN
```

**Flags column** — RT-hostile events detected inside FUT:

| Flag | Meaning |
|---|---|
| `V` | Voluntary context switch — FUT blocked on something (IO, mutex, sleep) |
| `I` | Involuntary context switch — OS preempted FUT mid-execution |
| `P` | Minor page fault — FUT touched unmapped memory (allocation or first access) |
| `M` | Major page fault — FUT triggered disk IO. Extremely bad in RT. |
| `X` | Xrun applied — output for this chunk was replaced by xrun policy |

**debt(us)** — how far behind the virtual clock you are. Only accumulates in REALTIME mode. If this grows across consecutive chunks, your function is fundamentally too slow for the chosen chunk size.

### Stdout — summary

```
  wall latency (CLOCK_MONOTONIC)
    p50  :       1.2 µs   (0.000x budget)
    p95  :    4800.1 µs   (0.900x budget)
    p99  :    6100.3 µs   (1.143x budget)   ← over 1.0 means overrun
    max  :    9200.7 µs   (1.725x budget)

  cpu latency  (CLOCK_THREAD_CPUTIME_ID)
    p50  :       1.1 µs
    p99  :    5900.2 µs

  overruns        : 12 / 1020  (1.17%)
  xruns applied   : 12
  total glitch    : 64.0 ms
  max debt        : 3755.1 µs

  verdict         : ❌ FAIL
```

**Verdict:**

| Verdict | Condition |
|---|---|
| `✅ PASS` | 0 overruns |
| `⚠ MARGINAL` | Overrun rate < 1% |
| `❌ FAIL` | Overrun rate ≥ 1%, or p99 > deadline |

**wall >> cpu** (p99 wall much larger than p99 cpu) means FUT spent time waiting rather than computing — blocked on IO, a mutex, or being preempted by the OS. On a real RT system with SCHED_FIFO, preemption cannot happen, but blocking still can.

### CSV log — `pw-sim.log.csv`

One row per non-warmup chunk:

```
chunk_idx, frames, wall_us, cpu_us, deadline_us, overrun, wall_ratio,
xrun_applied, cumulative_debt_us,
vol_ctx_sw, invol_ctx_sw, page_faults_minor, page_faults_major
```

Plot with Python:
```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("pw-sim.log.csv")
plt.figure(figsize=(12, 4))
plt.plot(df["chunk_idx"], df["wall_us"], label="wall_us")
plt.axhline(df["deadline_us"].iloc[0], color="red", linestyle="--", label="deadline")
plt.xlabel("chunk"); plt.ylabel("µs"); plt.legend(); plt.tight_layout()
plt.savefig("latency.png")
```

---

## Interpreting results for PipeWire deployment

| Observation | What it means |
|---|---|
| `wall_ratio` consistently < 0.5 | Good headroom. Safe to deploy. |
| `wall_ratio` p99 between 0.5–0.9 | Acceptable. Watch for spikes under load. |
| `wall_ratio` p99 > 1.0 | Will xrun in PipeWire. Optimize or increase chunk size. |
| `cumulative_debt_us` growing | Function is systematically too slow for this chunk size. |
| p99 >> p50 | Unpredictable spikes — dangerous even if average is fine. |
| `V` flags on many chunks | Function is blocking. Find and remove the blocking call. |
| `P` flags on early chunks | Normal model warm-up. Use `warmup_chunks` to exclude from stats. |
| `P` flags on all chunks | Function allocates every call. Pre-allocate before the loop. |
| `M` flags anywhere | Major page fault in RT context. Critical — fix immediately. |

---

## Typical workflow

```
1. Set clock_mode = SEQUENTIAL
   Plug in your FUT
   Run with PassthroughFut first → confirm output audio is correct
   Run with your FUT → confirm audio is altered as expected

2. Set clock_mode = SEQUENTIAL, xrun_policy = ZEROS
   Run with your FUT
   Check: are there overruns? What is p99 / deadline ratio?
   Check: are there V / I / P / M flags?

3. Set clock_mode = REALTIME, xrun_policy = ZEROS
   Run again
   Listen to output.wav → this is exactly what users would hear in PipeWire
   Check cumulative_debt_us — does it grow unboundedly?

4. Tune chunk_size
   Try 512 or 1024 if your model needs more time
   Larger chunk = more latency (512/48000 = 10.7ms) but easier deadline

5. When p99 < 0.7x deadline with 0 RT flags → ready for PipeWire
```

---

## File structure

```
pw-sim/
├── CMakeLists.txt
└── src/
    ├── main.cpp               ← edit this: config + active FUT
    ├── SimConfig.hpp          ← all knobs documented here
    ├── engine/
    │   ├── ChunkMetric.hpp    ← per-chunk measurement struct
    │   ├── SimEngine.hpp/.cpp ← main simulation loop
    │   ├── VirtualClock.hpp/.cpp  ← PipeWire period clock model
    │   ├── XrunPolicy.hpp/.cpp    ← zeros / repeat_last / passthrough
    │   └── ProbeReader.hpp/.cpp   ← context switch + page fault probes
    ├── fut/
    │   ├── FutInterface.hpp       ← FutFn typedef and contract
    │   └── stubs/
    │       ├── PassthroughFut.hpp
    │       ├── GainFut.hpp
    │       └── SlowFut.hpp
    ├── io/
    │   ├── AudioReader.hpp/.cpp   ← libsndfile input
    │   └── AudioWriter.hpp/.cpp   ← libsndfile output
    └── metrics/
        └── MetricsWriter.hpp/.cpp ← CSV writer + summary printer
```
## fut helper
`fut-prompt-helper.txt` is a summary that help you write your fut easier with help of llms

## RNNoise FUT
In order to have pw-sim run the fut that uses RNNoise model you must run with --rnnoise. Example:
```bash
pw-sim --rnnoise input.wav output.wav
```