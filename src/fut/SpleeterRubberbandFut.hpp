#pragma once
#if defined(PW_SIM_HAVE_SPLEETER) && defined(PW_SIM_HAVE_RUBBERBAND)

#include "fut/FutInterface.hpp"
#include <tensorflow/c/c_api.h>
#include <rubberband/RubberBandStretcher.h>

#include <memory>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <string>

// Combined Spleeter source-separation + Rubberband pitch-shift FUT.
//
// Runs entirely inside a single FUT callback — no multi-stage simulation.
//
// Per 5-second batch:
//   1. TF_SessionRun with TWO outputs → vocals[model_ch][M] + acmp[model_ch][M]
//   2. Feed vocals through a Rubberband R2 real-time pitch shifter
//   3. Mix: out[c][s] = shifted_vocals[c][s] + acmp[c][s]
//   4. Reinterleave into the output FIFO
//
// Rubberband state persists across batches so latency is paid once (~85 ms at
// 48 kHz with R2).  The FIFO outputs silence during initial Spleeter
// accumulation and the Rubberband priming window.

namespace {

static constexpr size_t SPR_BATCH_SECS    = 5;
static constexpr size_t SPR_SAMPLE_RATE   = 44100;
static constexpr size_t SPR_BATCH         = SPR_BATCH_SECS * SPR_SAMPLE_RATE;
static constexpr size_t SPR_MODEL_CH      = 2;
// Fallback feed size when Rubberband's internal buffer is saturated (getSamplesRequired()==0).
// Small enough to not over-commit; large enough to make steady progress.
static constexpr size_t SPR_RB_NUDGE_SAMPLES = 64;

static void spr_check_tf(TF_Status* s, const char* where) {
    if (TF_GetCode(s) != TF_OK)
        throw std::runtime_error(std::string(where) + ": " + TF_Message(s));
}

struct SprState {
    using RBS = RubberBand::RubberBandStretcher;

    // TF session
    TF_Graph*   graph   = nullptr;
    TF_Session* session = nullptr;
    TF_Status*  tfs     = nullptr;
    std::string input_op_name;
    std::string vocals_op_name;
    std::string acmp_op_name;

    // Rubberband
    std::unique_ptr<RBS> rb;
    size_t sample_rate;
    double semitones;

    // FUT state
    size_t channels  = 0;
    bool   mix_acmp  = true;  // false → output pitch-shifted vocals only

    // Input accumulation: interleaved stereo [SPR_BATCH * SPR_MODEL_CH]
    std::vector<float> in_pcm;
    size_t             in_count = 0;

    // Output FIFO: interleaved, 'channels' wide
    std::vector<float> out_pcm;
    size_t             out_head  = 0;
    size_t             out_avail = 0;

    // Rubberband planar scratch (reused for both input feeding and output retrieval)
    std::vector<std::vector<float>> rb_plane;
    std::vector<const float*>       rb_in_ptrs;
    std::vector<float*>             rb_out_ptrs;

    SprState(size_t sr, double semi, bool mix,
             std::string inp, std::string voc, std::string acp)
        : input_op_name(std::move(inp)),
          vocals_op_name(std::move(voc)),
          acmp_op_name(std::move(acp)),
          sample_rate(sr),
          semitones(semi),
          mix_acmp(mix) {}

    ~SprState() {
        if (session) TF_DeleteSession(session, tfs);
        if (graph)   TF_DeleteGraph(graph);
        if (tfs)     TF_DeleteStatus(tfs);
    }

    void init_channels(size_t ch) {
        channels = ch;
        double pitch_scale = std::pow(2.0, semitones / 12.0);
        auto opts = RBS::OptionProcessRealTime | RBS::OptionPitchHighConsistency;
        rb = std::make_unique<RBS>(sample_rate, ch, opts, 1.0, pitch_scale);

        rb_plane.assign(ch, {});
        rb_in_ptrs.resize(ch);
        rb_out_ptrs.resize(ch);
        in_pcm.assign(SPR_BATCH * SPR_MODEL_CH, 0.0f);

        std::fprintf(stderr,
            "[SpleeterRubberbandFut] init: channels=%zu  sample_rate=%zu  "
            "semitones=%.2f  pitch_scale=%.6f  rb_latency=%zu samples\n",
            ch, sample_rate, semitones, pitch_scale, rb->getLatency());
    }

    // Drain Rubberband output into `shifted` per-channel vectors.
    void drain_rb(std::vector<std::vector<float>>& shifted) {
        const size_t fch = channels;
        for (;;) {
            int avail = rb->available();
            if (avail <= 0) break;
            size_t n = static_cast<size_t>(avail);
            for (size_t c = 0; c < fch; ++c) {
                rb_plane[c].resize(n);
                rb_out_ptrs[c] = rb_plane[c].data();
            }
            size_t got = rb->retrieve(rb_out_ptrs.data(), n);
            for (size_t c = 0; c < fch; ++c)
                shifted[c].insert(shifted[c].end(),
                                  rb_plane[c].data(), rb_plane[c].data() + got);
        }
    }

    // Run TF inference (both stems), pitch-shift vocals via Rubberband,
    // mix with accompaniment, and append the result to out_pcm.
    void run_inference() {
        const size_t fch = channels;

        // Build input tensor [SPR_BATCH, 2]
        int64_t in_dims[2] = { (int64_t)SPR_BATCH, (int64_t)SPR_MODEL_CH };
        size_t  in_bytes   = SPR_BATCH * SPR_MODEL_CH * sizeof(float);
        TF_Tensor* in_tensor = TF_NewTensor(
            TF_FLOAT, in_dims, 2,
            in_pcm.data(), in_bytes,
            [](void*, size_t, void*){}, nullptr);
        if (!in_tensor)
            throw std::runtime_error("SpleeterRubberbandFut: TF_NewTensor failed");

        TF_Operation* in_op  = TF_GraphOperationByName(graph, input_op_name.c_str());
        TF_Operation* voc_op = TF_GraphOperationByName(graph, vocals_op_name.c_str());
        TF_Operation* acp_op = TF_GraphOperationByName(graph, acmp_op_name.c_str());
        if (!in_op)  throw std::runtime_error("SpleeterRubberbandFut: input op not found: "  + input_op_name);
        if (!voc_op) throw std::runtime_error("SpleeterRubberbandFut: vocals op not found: " + vocals_op_name);
        if (!acp_op) throw std::runtime_error("SpleeterRubberbandFut: acmp op not found: "   + acmp_op_name);

        TF_Output  tf_in[1]       = {{ in_op, 0 }};
        TF_Output  tf_out[2]      = {{ voc_op, 0 }, { acp_op, 0 }};
        TF_Tensor* out_tensors[2] = { nullptr, nullptr };

        TF_SessionRun(session,
                      nullptr,
                      tf_in,  &in_tensor,   1,
                      tf_out,  out_tensors, 2,
                      nullptr, 0, nullptr, tfs);
        TF_DeleteTensor(in_tensor);
        spr_check_tf(tfs, "TF_SessionRun");

        // Both tensors: shape [model_ch, M]
        const long model_ch = (TF_NumDims(out_tensors[0]) >= 1) ? TF_Dim(out_tensors[0], 0) : 1;
        const long M        = (TF_NumDims(out_tensors[0]) >= 2) ? TF_Dim(out_tensors[0], 1) : 0;
        const float* vocals_raw = static_cast<const float*>(TF_TensorData(out_tensors[0]));
        const float* acmp_raw   = static_cast<const float*>(TF_TensorData(out_tensors[1]));

        // Direct pointers into TF tensor data (valid until TF_DeleteTensor below).
        // Map FUT channels onto the model's two channels; no copy needed.
        std::vector<const float*> vocals_ptr(fch), acmp_ptr(fch);
        for (size_t c = 0; c < fch; ++c) {
            size_t mc = std::min(c, static_cast<size_t>(model_ch - 1));
            vocals_ptr[c] = vocals_raw + mc * M;
            acmp_ptr[c]   = acmp_raw   + mc * M;
        }

        // Feed vocals through Rubberband; use tensor pointers directly — no copy.
        std::vector<std::vector<float>> shifted(fch);
        size_t pos = 0;
        while (pos < static_cast<size_t>(M)) {
            drain_rb(shifted);

            size_t need = rb->getSamplesRequired();
            if (need == 0) need = SPR_RB_NUDGE_SAMPLES;
            size_t feed = std::min(need, static_cast<size_t>(M) - pos);

            for (size_t c = 0; c < fch; ++c)
                rb_in_ptrs[c] = vocals_ptr[c] + pos;
            rb->process(rb_in_ptrs.data(), feed, false);
            pos += feed;
        }
        drain_rb(shifted);

        // Tensors no longer needed — free before the mix allocation.
        TF_DeleteTensor(out_tensors[0]);
        TF_DeleteTensor(out_tensors[1]);

        const size_t shifted_len = shifted.empty() ? 0 : shifted[0].size();
        const size_t write_off   = out_head + out_avail;

        if (mix_acmp) {
            // Split into branch-free segments to allow auto-vectorisation.
            // On all batches after the first, common == M and the tail segments are empty.
            const size_t common   = std::min(shifted_len, static_cast<size_t>(M));
            const size_t acp_tail = static_cast<size_t>(M) - common;
            const size_t voc_tail = shifted_len - common;
            const size_t mix_len  = common + acp_tail + voc_tail;

            out_pcm.resize(write_off + mix_len * fch);

            for (size_t s = 0; s < common; ++s)
                for (size_t c = 0; c < fch; ++c)
                    out_pcm[write_off + s * fch + c] = shifted[c][s] + acmp_ptr[c][s];

            for (size_t s = common; s < common + acp_tail; ++s)
                for (size_t c = 0; c < fch; ++c)
                    out_pcm[write_off + s * fch + c] = acmp_ptr[c][s];

            for (size_t s = common; s < common + voc_tail; ++s)
                for (size_t c = 0; c < fch; ++c)
                    out_pcm[write_off + s * fch + c] = shifted[c][s];

            out_avail += mix_len * fch;
        } else {
            out_pcm.resize(write_off + shifted_len * fch);
            for (size_t s = 0; s < shifted_len; ++s)
                for (size_t c = 0; c < fch; ++c)
                    out_pcm[write_off + s * fch + c] = shifted[c][s];
            out_avail += shifted_len * fch;
        }
    }
};

} // anonymous namespace

inline FutFn make_spleeter_rubberband_fut(
        const std::string& model_dir,
        double             semitones,
        size_t             sample_rate,
        bool               mix_acmp  = false,
        const std::string& input_op  = "waveform",
        const std::string& vocals_op = "inverse_stft/overlap_and_add/strided_slice_4",
        const std::string& acmp_op   = "inverse_stft_1/overlap_and_add/strided_slice_4")
{
    auto st = std::make_shared<SprState>(
        sample_rate, semitones, mix_acmp, input_op, vocals_op, acmp_op);

    st->tfs   = TF_NewStatus();
    st->graph = TF_NewGraph();

    TF_SessionOptions* sess_opts = TF_NewSessionOptions();
    const char* tags[] = { "serve" };
    st->session = TF_LoadSessionFromSavedModel(
        sess_opts, nullptr, model_dir.c_str(),
        tags, 1, st->graph, nullptr, st->tfs);
    TF_DeleteSessionOptions(sess_opts);
    spr_check_tf(st->tfs, "TF_LoadSessionFromSavedModel");

    std::fprintf(stderr,
        "[SpleeterRubberbandFut] loaded: %s\n"
        "[SpleeterRubberbandFut] semitones=%.2f  pitch_scale=%.6f\n"
        "[SpleeterRubberbandFut] batch=%zu samples (%.1f s at %zu Hz)\n"
        "[SpleeterRubberbandFut] pipeline per batch: "
        "TF_SessionRun (both stems) → Rubberband pitch-shift on vocals → mix\n",
        model_dir.c_str(), semitones, std::pow(2.0, semitones / 12.0),
        SPR_BATCH, static_cast<double>(SPR_BATCH) / SPR_SAMPLE_RATE, SPR_SAMPLE_RATE);

    return [st](const float* input, float* output,
                size_t frames, size_t channels, size_t /*chunk_index*/) {
        if (st->channels != channels) st->init_channels(channels);

        const size_t fch = channels;

        const bool has_left  = (fch >= 1);
        const bool has_right = (fch >= 2);
        for (size_t s = 0; s < frames && st->in_count < SPR_BATCH; ++s, ++st->in_count) {
            const float l = has_left  ? input[s * fch]     : 0.0f;
            const float r = has_right ? input[s * fch + 1] : l;
            st->in_pcm[st->in_count * 2 + 0] = l;
            st->in_pcm[st->in_count * 2 + 1] = r;
        }

        if (st->in_count >= SPR_BATCH) {
            st->run_inference();
            st->in_count = 0;
            std::fill(st->in_pcm.begin(), st->in_pcm.end(), 0.0f);
        }

        const size_t need = frames * fch;

        if (st->out_avail >= need) {
            std::memcpy(output, st->out_pcm.data() + st->out_head, need * sizeof(float));
            st->out_head  += need;
            st->out_avail -= need;
            // Compact ring once the head passes the halfway mark
            if (st->out_head > st->out_pcm.size() / 2) {
                st->out_pcm.erase(st->out_pcm.begin(),
                                  st->out_pcm.begin() + static_cast<long>(st->out_head));
                st->out_head = 0;
            }
        } else {
            std::fill(output, output + need, 0.0f);
        }
    };
}

#endif // PW_SIM_HAVE_SPLEETER && PW_SIM_HAVE_RUBBERBAND
