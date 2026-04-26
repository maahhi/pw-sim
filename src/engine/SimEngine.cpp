#include "engine/SimEngine.hpp"
#include "engine/VirtualClock.hpp"
#include "engine/XrunPolicy.hpp"
#include "engine/ProbeReader.hpp"
#include "io/AudioReader.hpp"
#include "io/AudioWriter.hpp"
#include "metrics/MetricsWriter.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <string>
#include <sndfile.h>
#include <pthread.h>
#include <sched.h>

static double timespec_to_us(const struct timespec& ts) {
    return static_cast<double>(ts.tv_sec)  * 1e6 +
           static_cast<double>(ts.tv_nsec) / 1e3;
}

// Map output_format string to a libsndfile subformat code.
// "same" uses the input file's own subformat (SF_FORMAT_SUBMASK).
static int resolve_output_subformat(const std::string& fmt, int input_sf_format) {
    if (fmt == "same") return input_sf_format & SF_FORMAT_SUBMASK;
    if (fmt == "s16")  return SF_FORMAT_PCM_16;
    if (fmt == "s24")  return SF_FORMAT_PCM_24;
    if (fmt == "f32")  return SF_FORMAT_FLOAT;
    throw std::runtime_error(
        "unknown output_format '" + fmt + "' (valid: same, s16, s24, f32)");
}

SimEngine::SimEngine(const SimConfig& config, FutFn fut)
    : m_config(config)
    , m_fut(std::move(fut))
{}

double SimEngine::wall_now_us() const {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return timespec_to_us(ts);
}

void SimEngine::check_cpu_governor() const {
    FILE* f = std::fopen(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (!f) return;
    char gov[64] = {};
    if (std::fgets(gov, sizeof(gov), f)) {
        for (char& c : gov) if (c == '\n') c = '\0';
    }
    std::fclose(f);
    if (std::string(gov) != "performance") {
        std::printf(
            "WARNING: CPU governor is '%s' - latency measurements may be inflated.\n"
            "  For accurate results: echo performance | sudo tee "
            "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor\n\n", gov);
    }
}

void SimEngine::try_rt_scheduling() const {
    struct sched_param sp;
    sp.sched_priority = sched_get_priority_min(SCHED_FIFO) + 1;
    int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (rc != 0) {
        std::printf(
            "WARNING: could not set SCHED_FIFO (error %d).\n"
            "  Run as root or grant CAP_SYS_NICE for real-time scheduling.\n\n", rc);
    } else {
        std::printf("  SCHED_FIFO set - thread is now real-time priority.\n\n");
    }
}

void SimEngine::print_chunk_log(const ChunkMetric& m) const {
    char flags[16] = {};
    int  fi = 0;
    if (m.voluntary_ctx_switches   > 0) flags[fi++] = 'V';
    if (m.involuntary_ctx_switches > 0) flags[fi++] = 'I';
    if (m.page_faults_minor        > 0) flags[fi++] = 'P';
    if (m.page_faults_major        > 0) flags[fi++] = 'M';
    if (m.xrun_applied)                 flags[fi++] = 'X';
    if (fi == 0) { flags[0] = '-'; flags[1] = '\0'; }

    const char* status = m.overrun ? "OVERRUN" : "OK";

    std::printf("  %-7zu  %-6zu  %-9.1f  %-9.1f  %-7.3f  %-8s  %-9.1f  %s\n",
                m.chunk_index,
                m.frames,
                m.wall_us,
                m.deadline_us,
                m.wall_ratio,
                flags,
                m.cumulative_debt_us,
                status);
}

void SimEngine::run() {
    if (m_config.warn_cpu_governor)  check_cpu_governor();
    if (m_config.try_rt_priority)    try_rt_scheduling();

    AudioReader reader(m_config.input_file);
    reader.open();

    const int    channels     = reader.channels();
    const int    file_rate    = reader.sample_rate();
    const size_t total_frames = reader.total_frames();
    const size_t chunk_size   = m_config.chunk_size;

    if (file_rate != static_cast<int>(m_config.sample_rate)) {
        std::fprintf(stderr,
            "WARNING: file sample rate (%d Hz) differs from config (%zu Hz).\n"
            "  Deadline computed from config rate. Audio pitch will be wrong.\n",
            file_rate, m_config.sample_rate);
    }

    const int out_sub = resolve_output_subformat(m_config.output_format,
                                                 reader.sf_format());
    AudioWriter writer(m_config.output_file, channels, file_rate, out_sub);
    writer.open();

    MetricsWriter csv(m_config.log_file);
    csv.open();

    const size_t buf_samples = chunk_size * static_cast<size_t>(channels);
    std::vector<float> in_buf(buf_samples,    0.0f);
    std::vector<float> out_buf(buf_samples,   0.0f);
    std::vector<float> last_good(buf_samples, 0.0f);

    const double budget = effective_deadline_us(m_config);
    const double period = (static_cast<double>(chunk_size) /
                           static_cast<double>(m_config.sample_rate)) * 1e6;
    VirtualClock vclock(period, m_config.deadline_budget_us_offset);

    const size_t est_chunks = (total_frames + chunk_size - 1) / chunk_size;

    std::printf("\n");
    std::printf("=================================================================\n");
    std::printf("                      pw-sim  Tier 3\n");
    std::printf("=================================================================\n");
    std::printf("  input          : %s\n",    m_config.input_file.c_str());
    std::printf("  output         : %s\n",    m_config.output_file.c_str());
    std::printf("  log            : %s\n",    m_config.log_file.c_str());
    std::printf("  format         : %s\n",    reader.format_description().c_str());
    std::printf("  sample rate    : %d Hz\n", file_rate);
    std::printf("  channels       : %d\n",    channels);
    std::printf("  chunk size     : %zu frames\n", chunk_size);
    std::printf("  period         : %.1f us\n",  period);
    std::printf("  budget         : %.1f us", budget);
    if (m_config.deadline_budget_us_offset > 0.0)
        std::printf("  (period - %.0f us offset)", m_config.deadline_budget_us_offset);
    std::printf("\n");
    std::printf("  clock mode     : %s\n",   clock_mode_str(m_config.clock_mode));
    if (m_config.clock_mode == ClockMode::REALTIME)
        std::printf("  xrun policy    : %s\n", xrun_policy_str(m_config.xrun_policy));
    std::printf("  pre-fill       : %s\n",   prefill_str(m_config.pre_fill));
    std::printf("  tail policy    : %s\n",   tail_policy_str(m_config.tail_policy));
    std::printf("  output format  : %s\n",   m_config.output_format.c_str());
    std::printf("  warmup chunks  : %zu\n",  m_config.warmup_chunks);
    std::printf("  total frames   : %zu (~%zu chunks)\n", total_frames, est_chunks);
    std::printf("  probes         : cpu=%s  ctx_sw=%s  page_faults=%s\n",
                m_config.probe_cpu_time         ? "on" : "off",
                m_config.probe_context_switches ? "on" : "off",
                m_config.probe_page_faults      ? "on" : "off");
    std::printf("\n");
    std::printf("  %-7s  %-6s  %-9s  %-9s  %-7s  %-8s  %-9s  %s\n",
                "chunk", "frames", "wall(us)", "budgt(us)", "ratio",
                "flags", "debt(us)", "status");
    std::printf("  flags: V=voluntary-block  I=involuntary-preempt"
                "  P=page-fault  M=major-fault  X=xrun\n");
    std::printf("  %s\n", std::string(74, '-').c_str());

    size_t chunk_index  = 0;
    size_t metric_index = 0;

    while (true) {
        std::fill(in_buf.begin(),  in_buf.end(),  0.0f);
        std::fill(out_buf.begin(), out_buf.end(), 0.0f);

        size_t frames_read = reader.read(in_buf.data(), chunk_size);
        if (frames_read == 0) break;

        // Tail policy: skip the last partial chunk if configured to drop it.
        if (frames_read < chunk_size && m_config.tail_policy == TailPolicy::DROP)
            break;

        const bool is_warmup = (chunk_index < m_config.warmup_chunks);

        if (is_warmup) {
            m_fut(in_buf.data(), out_buf.data(),
                  frames_read, static_cast<size_t>(channels), chunk_index);
            writer.write_silence(frames_read);
            std::printf("  %-7zu  %-6zu  [warmup]\n", chunk_index, frames_read);
            ++chunk_index;
            continue;
        }

        // Pre-fill
        if (m_config.pre_fill == PreFillPolicy::PASSTHROUGH)
            std::memcpy(out_buf.data(), in_buf.data(), buf_samples * sizeof(float));

        // Probes before
        ProbeSnapshot snap_before = {};
        if (m_config.probe_context_switches || m_config.probe_page_faults)
            snap_before = ProbeReader::snapshot();

        double cpu_before = 0.0;
        if (m_config.probe_cpu_time)
            cpu_before = ProbeReader::cpu_now_us();

        // Time + call FUT
        double wall_before = wall_now_us();
        m_fut(in_buf.data(), out_buf.data(),
              frames_read, static_cast<size_t>(channels), chunk_index);
        double wall_after = wall_now_us();

        // Probes after
        double cpu_after = 0.0;
        if (m_config.probe_cpu_time)
            cpu_after = ProbeReader::cpu_now_us();

        ProbeSnapshot snap_after = {};
        if (m_config.probe_context_switches || m_config.probe_page_faults)
            snap_after = ProbeReader::snapshot();

        const double wall_us = wall_after - wall_before;
        const double cpu_us  = cpu_after  - cpu_before;

        ProbeDelta pd = {};
        if (m_config.probe_context_switches || m_config.probe_page_faults)
            pd = ProbeReader::delta(snap_before, snap_after);

        double debt_us = vclock.end_chunk(wall_us);
        const bool overrun = (wall_us > budget);

        ChunkMetric m;
        m.chunk_index        = metric_index;
        m.frames             = frames_read;
        m.wall_us            = wall_us;
        m.cpu_us             = m_config.probe_cpu_time ? cpu_us : 0.0;
        m.deadline_us        = budget;
        m.wall_ratio         = wall_us / budget;
        m.overrun            = overrun;
        m.cumulative_debt_us = debt_us;
        m.is_warmup          = false;

        if (m_config.probe_context_switches) {
            m.voluntary_ctx_switches   = pd.voluntary_ctx_switches;
            m.involuntary_ctx_switches = pd.involuntary_ctx_switches;
        }
        if (m_config.probe_page_faults) {
            m.page_faults_minor = pd.page_faults_minor;
            m.page_faults_major = pd.page_faults_major;
        }

        // REALTIME: apply xrun policy on overrun
        if (m_config.clock_mode == ClockMode::REALTIME && overrun) {
            apply_xrun_policy(m_config.xrun_policy,
                              out_buf.data(),
                              in_buf.data(),
                              last_good,
                              frames_read,
                              static_cast<size_t>(channels));
            m.xrun_applied = true;
        }

        if (!m.xrun_applied)
            std::memcpy(last_good.data(), out_buf.data(), buf_samples * sizeof(float));

        writer.write(out_buf.data(), frames_read);

        m_metrics.push_back(m);
        csv.write_chunk(m);
        print_chunk_log(m);

        ++metric_index;
        ++chunk_index;
    }

    reader.close();
    writer.close();
    csv.close();

    std::printf("  %s\n", std::string(74, '-').c_str());
    MetricsWriter::print_summary(m_metrics, clock_mode_str(m_config.clock_mode));
}
