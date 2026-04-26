# pw-sim Configuration Reference

pw-sim is configured through a TOML file with optional per-run CLI overrides.
The precedence chain is:

```
built-in defaults  <  pw-sim.toml  <  CLI flags
```

---

## TOML Schema

```toml
[engine]
clock_mode              = "realtime"   # "sequential" | "realtime"
chunk_size              = 256          # frames per FUT call (64, 128, 256, 512, 1024)
sample_rate             = 48000        # Hz — must match input file
warmup_chunks           = 4            # calls before metrics are recorded
pre_fill_output         = "zeros"      # "zeros" | "passthrough"

[io]
input_file              = "input.wav"
output_file             = "output.wav"
output_format           = "same"       # "same" | "s16" | "s24" | "f32"
tail_policy             = "zero_pad"   # "zero_pad" | "drop"

[realtime]
xrun_policy             = "zeros"      # "zeros" | "repeat_last" | "passthrough"
deadline_budget_us_offset = 0          # µs to subtract from the deadline budget

[metrics]
log_file                = "pw-sim.log.csv"
warn_cpu_governor       = true
try_rt_priority         = false

[probes]
cpu_time                = true   # CLOCK_THREAD_CPUTIME_ID alongside wall time
context_switches        = true   # voluntary + involuntary ctx switches per chunk
page_faults             = true   # minor + major page faults per chunk
```

---

## Key Reference

### [engine]

| Key | Type | Values | Description |
|-----|------|--------|-------------|
| `clock_mode` | string | `sequential` / `realtime` | **sequential**: FUT always runs to completion; overruns are logged only. **realtime**: a virtual clock advances one period per chunk regardless of FUT time; overruns trigger `xrun_policy`. |
| `chunk_size` | int | 64–1024 | PCM frames handed to FUT per callback. Matches the PipeWire quantum you intend to deploy at. |
| `sample_rate` | int | e.g. 44100, 48000 | Must match the input file. Used to compute the deadline: `chunk_size / sample_rate × 1 000 000 µs`. |
| `warmup_chunks` | int | ≥0 | Chunks to run through FUT before recording metrics. Output for warmup chunks is written as silence. Lets model caches and JIT paths settle. |
| `pre_fill_output` | string | `zeros` / `passthrough` | **zeros**: output buffer is zeroed before each FUT call (matches PipeWire behaviour). **passthrough**: output is pre-copied from input, simulating a plugin that does a safety memcpy first. |

### [io]

| Key | Type | Values | Description |
|-----|------|--------|-------------|
| `input_file` | string | path | Source audio file. Any format libsndfile can read (WAV, FLAC, AIFF, …). |
| `output_file` | string | path | Destination WAV file. Always written as WAV container. |
| `output_format` | string | `same` / `s16` / `s24` / `f32` | **same**: preserve the input file's bit depth. **s16/s24/f32**: convert on write. Useful for checking whether bit-depth reduction introduces audible artefacts after processing. |
| `tail_policy` | string | `zero_pad` / `drop` | What to do with the last partial chunk when total frames is not a multiple of `chunk_size`. **zero_pad**: pad with silence and process it (default — every input frame is heard). **drop**: discard the trailing frames (every chunk is exactly `chunk_size` frames). |

### [realtime]

| Key | Type | Description |
|-----|------|-------------|
| `xrun_policy` | string | What fills the output buffer when FUT misses the deadline. **zeros**: silence (PipeWire default). **repeat_last**: copy the previous good output chunk. **passthrough**: copy the dry input for that chunk. |
| `deadline_budget_us_offset` | float | Subtract N µs from the computed deadline to simulate real-world overhead (driver scheduling jitter, graph latency, etc.). Full period at 256 frames / 48 kHz = 5 333 µs. A 200 µs offset tightens the budget to 5 133 µs — closer to what a real PipeWire node actually has. |

### [metrics]

| Key | Type | Description |
|-----|------|-------------|
| `log_file` | string | Path for the per-chunk CSV log. |
| `warn_cpu_governor` | bool | Warn at startup if the CPU scaling governor is not `performance`. Inflated latency numbers are common on `powersave`. |
| `try_rt_priority` | bool | Attempt `SCHED_FIFO` on the main thread. Requires `CAP_SYS_NICE` or root. Prints a warning if it fails. |

### [probes]

| Key | Type | Description |
|-----|------|-------------|
| `cpu_time` | bool | Measure CPU time (`CLOCK_THREAD_CPUTIME_ID`) alongside wall time. A cpu/wall gap flags blocking — IO, mutex waits, or OS preemption. |
| `context_switches` | bool | Count voluntary + involuntary context switches per chunk via `/proc/self/status`. Nonzero voluntary → FUT blocked; nonzero involuntary → OS preempted FUT. |
| `page_faults` | bool | Count minor + major page faults per chunk via `getrusage(RUSAGE_THREAD)`. Nonzero → FUT touched previously-unpaged memory (allocation spike). |

---

## CLI Flags

Every TOML key can be overridden at the command line without editing the file.
CLI flags are applied on top of whatever the TOML file (or defaults) set.

```
pw-sim [--config <file>] [flags...] [--rnnoise | --nam <model.nam>] [input.wav [output.wav]]
pw-sim --help
```

| Flag | Equivalent TOML key |
|------|---------------------|
| `--config <file>` | *(loads the TOML file)* |
| `--input <file>` | `[io] input_file` |
| `--output <file>` | `[io] output_file` |
| `--log-file <file>` | `[metrics] log_file` |
| `--clock-mode <sequential\|realtime>` | `[engine] clock_mode` |
| `--chunk-size <N>` | `[engine] chunk_size` |
| `--sample-rate <N>` | `[engine] sample_rate` |
| `--warmup-chunks <N>` | `[engine] warmup_chunks` |
| `--pre-fill <zeros\|passthrough>` | `[engine] pre_fill_output` |
| `--xrun-policy <zeros\|repeat_last\|passthrough>` | `[realtime] xrun_policy` |
| `--tail-policy <zero_pad\|drop>` | `[io] tail_policy` |
| `--output-format <same\|s16\|s24\|f32>` | `[io] output_format` |
| `--deadline-offset <µs>` | `[realtime] deadline_budget_us_offset` |
| `--probe-cpu <on\|off>` | `[probes] cpu_time` |
| `--probe-ctx <on\|off>` | `[probes] context_switches` |
| `--probe-faults <on\|off>` | `[probes] page_faults` |

Positional arguments (backward-compatible): the first non-flag argument after
all flags is treated as `--input`, the second as `--output`. Named flags take
precedence over positionals if both are provided.

---

## Example Runs

### 1. Baseline — load defaults from TOML, run RNNoise

```bash
pw-sim --config pw-sim.toml --rnnoise
```

Reads `pw-sim.toml` for all settings, applies no overrides. Good starting point.

---

### 2. Quick chunk-size sweep without editing the file

```bash
for q in 64 128 256 512 1024; do
  pw-sim --config pw-sim.toml \
         --chunk-size $q \
         --output "out_${q}.wav" \
         --log-file "log_${q}.csv" \
         --rnnoise
done
```

Runs five back-to-back experiments at different PipeWire quanta. Lets you see
at which chunk size the overrun rate crosses your threshold.

---

### 3. Correctness check in sequential mode

```bash
pw-sim --config pw-sim.toml \
       --clock-mode sequential \
       --input dry.wav \
       --output processed.wav \
       --rnnoise
```

Turns off the virtual clock so FUT always runs to completion regardless of time.
Use this to verify audio quality before doing any timing analysis.

---

### 4. Simulate tighter real-world deadline (driver overhead model)

```bash
pw-sim --config pw-sim.toml \
       --deadline-offset 300 \
       --rnnoise
```

Subtracts 300 µs from the budget (5 333 → 5 033 µs at 256/48 kHz). Models a
system where ~300 µs of each period is consumed by driver scheduling and graph
overhead. If your FUT passes at 0 µs offset but fails here, it will xrun on
real hardware.

---

### 5. NAM model with 16-bit output for listening comparison

```bash
pw-sim --config pw-sim.toml \
       --nam models/my_amp.nam \
       --input dry_guitar.wav \
       --output amp_s16.wav \
       --output-format s16
```

Writes the processed output as 16-bit PCM (CD quality) instead of float32.
Useful for checking whether the dithering loss is audible after the NAM model.

---

### 6. Drop tail — strict chunk alignment

```bash
pw-sim --config pw-sim.toml \
       --tail-policy drop \
       --rnnoise
```

Any frames beyond the last complete chunk are discarded. Use when your FUT
(e.g. an overlap-add FFT) requires exactly `chunk_size` frames and produces
undefined output on a partial buffer.

---

### 7. Repeat-last xrun policy — less jarring glitch simulation

```bash
pw-sim --config pw-sim.toml \
       --xrun-policy repeat_last \
       --deadline-offset 200 \
       --rnnoise
```

When a chunk overruns the tightened budget, the previous good output is
repeated instead of silence. Listen to `output.wav` to hear how the glitch
sounds compared to the default `zeros` policy.

---

### 8. No TOML file — pure CLI, experiment mode

```bash
pw-sim \
  --clock-mode realtime \
  --chunk-size 128 \
  --sample-rate 48000 \
  --warmup-chunks 8 \
  --deadline-offset 150 \
  --xrun-policy passthrough \
  --tail-policy drop \
  --output-format f32 \
  --input dry.wav \
  --output out.wav \
  --nam models/guitar_amp.nam
```

Skips the TOML file entirely; every knob is set on the command line. Handy for
one-off experiments where you do not want to leave an edited config file behind.

---

### 9. Disable all probes for fastest possible wall-time measurement

```bash
pw-sim --config pw-sim.toml \
       --probe-cpu off \
       --probe-ctx off \
       --probe-faults off \
       --input dry.wav \
       --rnnoise
```

Turning off all probes removes `/proc/self/status` reads and `getrusage` calls
from the hot path. Use this when you want the cleanest possible wall-time
numbers with minimal measurement overhead.

---

### 10. Separate config file per experiment

```
configs/
├── baseline.toml       # 256-frame realtime, zeros policy
├── tight_budget.toml   # 256-frame realtime, 400 µs offset
└── sequential.toml     # sequential mode, no xrun
```

```bash
pw-sim --config configs/tight_budget.toml --input dry.wav --rnnoise
```

Keeping per-experiment TOML files lets you reproduce any past result exactly by
re-running the same command.

---

## Deadline Formula

```
period         = chunk_size / sample_rate × 1 000 000     (µs)
effective_budget = period - deadline_budget_us_offset      (µs)
```

| chunk_size | sample_rate | period | budget (offset=0) | budget (offset=300µs) |
|------------|-------------|--------|-------------------|-----------------------|
| 64 | 48000 | 1 333 µs | 1 333 µs | 1 033 µs |
| 128 | 48000 | 2 667 µs | 2 667 µs | 2 367 µs |
| 256 | 48000 | 5 333 µs | 5 333 µs | 5 033 µs |
| 512 | 48000 | 10 667 µs | 10 667 µs | 10 367 µs |
| 1024 | 48000 | 21 333 µs | 21 333 µs | 21 033 µs |

A `wall_ratio > 1.0` in the log means that chunk overran its budget. In
`realtime` mode that chunk's output is replaced by `xrun_policy`; in
`sequential` mode the output is preserved but the overrun is still flagged.
