#pragma once
#ifdef PW_SIM_HAVE_SPLEETER

#include "fut/FutInterface.hpp"
#include <tensorflow/c/c_api.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

// Spleeter 2-stem FUT
//
// The model takes raw PCM waveform [N, 2] (stereo, 44100 Hz) and outputs
// separated audio [2, M] where M = (ceil(N/1024)+3)*1024 (STFT tail padding).
// All STFT/U-Net/iSTFT is inside the TF graph — no DSP needed here.
//
// Hot-path design:
//   - Accumulate SP_BATCH stereo samples into in_pcm
//   - When full: TF_SessionRun (blocks ~600 ms on CPU → OVERRUN logged)
//   - Re-interleave planar [2, M] output into out_pcm ring
//   - Drain out_pcm to caller; zero-fill while still accumulating

namespace {

static constexpr size_t SP_BATCH_SECS  = 5;
static constexpr size_t SP_SAMPLE_RATE = 44100;
static constexpr size_t SP_BATCH       = SP_BATCH_SECS * SP_SAMPLE_RATE; // 220500
static constexpr size_t SP_MODEL_CH    = 2;  // model is always stereo

struct SpleeterState {
    TF_Graph*   graph   = nullptr;
    TF_Session* session = nullptr;
    TF_Status*  tfs     = nullptr;

    std::string stem;
    std::string input_op;
    std::string output_op;

    size_t channels = 0;  // FUT channel count (set on first call)

    // Input accumulation: interleaved stereo [SP_BATCH * SP_MODEL_CH]
    std::vector<float> in_pcm;
    size_t in_count = 0;  // stereo samples accumulated so far

    // Output ring: interleaved, 'channels' wide
    std::vector<float> out_pcm;
    size_t out_head  = 0;  // read offset in floats
    size_t out_avail = 0;  // floats ready to read

    ~SpleeterState() {
        if (session) { TF_DeleteSession(session, tfs); }
        if (graph)   { TF_DeleteGraph(graph); }
        if (tfs)     { TF_DeleteStatus(tfs); }
    }
};

static void check_tf(TF_Status* s, const char* where) {
    if (TF_GetCode(s) != TF_OK)
        throw std::runtime_error(std::string(where) + ": " + TF_Message(s));
}

// Run TF inference on in_pcm and append result to out_pcm ring.
// in_pcm layout: interleaved stereo [SP_BATCH * 2]
// out tensor layout: planar [2, M]
static void run_inference(SpleeterState& st) {
    const size_t in_samples = SP_BATCH;
    int64_t in_dims[2] = { (int64_t)in_samples, (int64_t)SP_MODEL_CH };
    size_t  in_bytes   = in_samples * SP_MODEL_CH * sizeof(float);

    TF_Tensor* in_tensor = TF_NewTensor(
        TF_FLOAT, in_dims, 2,
        st.in_pcm.data(), in_bytes,
        [](void*, size_t, void*){}, nullptr);
    if (!in_tensor)
        throw std::runtime_error("SpleeterFut: TF_NewTensor failed");

    TF_Operation* in_op  = TF_GraphOperationByName(st.graph, st.input_op.c_str());
    TF_Operation* out_op = TF_GraphOperationByName(st.graph, st.output_op.c_str());
    if (!in_op)
        throw std::runtime_error("SpleeterFut: input op not found: " + st.input_op);
    if (!out_op)
        throw std::runtime_error("SpleeterFut: output op not found: " + st.output_op);

    TF_Output inputs[1]  = {{ in_op,  0 }};
    TF_Output outputs[1] = {{ out_op, 0 }};
    TF_Tensor* out_tensor = nullptr;

    TF_SessionRun(st.session,
                  nullptr,
                  inputs,  &in_tensor,  1,
                  outputs, &out_tensor, 1,
                  nullptr, 0,
                  nullptr, st.tfs);
    TF_DeleteTensor(in_tensor);
    check_tf(st.tfs, "TF_SessionRun");

    // Output shape: [model_ch, out_samples] — planar
    const long model_ch    = (TF_NumDims(out_tensor) >= 1) ? TF_Dim(out_tensor, 0) : 1;
    const long out_samples = (TF_NumDims(out_tensor) >= 2) ? TF_Dim(out_tensor, 1) : 0;
    const float* raw       = static_cast<const float*>(TF_TensorData(out_tensor));

    // Re-interleave planar → interleaved for FUT channel count
    const size_t fch       = st.channels;
    const size_t new_floats = (size_t)out_samples * fch;
    const size_t write_off  = st.out_avail;

    st.out_pcm.resize(write_off + new_floats);
    for (size_t s = 0; s < (size_t)out_samples; ++s) {
        for (size_t c = 0; c < fch; ++c) {
            size_t mc = std::min(c, (size_t)(model_ch - 1));
            st.out_pcm[write_off + s * fch + c] = raw[mc * (size_t)out_samples + s];
        }
    }
    st.out_avail += new_floats;

    TF_DeleteTensor(out_tensor);
}

} // anonymous namespace

inline FutFn make_spleeter_fut(
        const std::string& model_dir,
        const std::string& stem      = "vocals",
        const std::string& input_op  = "waveform",
        const std::string& vocals_op = "inverse_stft/overlap_and_add/strided_slice_4",
        const std::string& acmp_op   = "inverse_stft_1/overlap_and_add/strided_slice_4")
{
    auto st = std::make_shared<SpleeterState>();
    st->stem      = stem;
    st->input_op  = input_op;
    st->output_op = (stem == "vocals") ? vocals_op : acmp_op;

    st->tfs   = TF_NewStatus();
    st->graph = TF_NewGraph();

    TF_SessionOptions* opts = TF_NewSessionOptions();
    const char* tags[] = { "serve" };

    st->session = TF_LoadSessionFromSavedModel(
        opts, nullptr, model_dir.c_str(),
        tags, 1, st->graph, nullptr, st->tfs);
    TF_DeleteSessionOptions(opts);
    check_tf(st->tfs, "TF_LoadSessionFromSavedModel");

    std::fprintf(stderr,
        "[SpleeterFut] loaded: %s\n"
        "[SpleeterFut] stem=%s  output_op=%s\n"
        "[SpleeterFut] batch=%zu samples (%.1f s at 44100 Hz) — expect ~600 ms OVERRUN per batch\n",
        model_dir.c_str(), st->stem.c_str(), st->output_op.c_str(),
        SP_BATCH, (double)SP_BATCH / SP_SAMPLE_RATE);

    st->in_pcm.assign(SP_BATCH * SP_MODEL_CH, 0.0f);

    return [st](const float* input, float* output,
                size_t frames, size_t channels, size_t /*chunk_index*/)
    {
        if (st->channels != channels) {
            st->channels  = channels;
            st->in_count  = 0;
            st->out_head  = 0;
            st->out_avail = 0;
            st->out_pcm.clear();
            st->in_pcm.assign(SP_BATCH * SP_MODEL_CH, 0.0f);
        }

        const size_t fch = channels;

        // Append input samples to accumulation buffer (convert to stereo)
        for (size_t s = 0; s < frames && st->in_count < SP_BATCH; ++s) {
            st->in_pcm[st->in_count * SP_MODEL_CH + 0] =
                (fch >= 1) ? input[s * fch + 0] : 0.0f;
            st->in_pcm[st->in_count * SP_MODEL_CH + 1] =
                (fch >= 2) ? input[s * fch + 1] : st->in_pcm[st->in_count * SP_MODEL_CH];
            ++st->in_count;
        }

        // Batch full — run inference (blocking, causes OVERRUN)
        if (st->in_count >= SP_BATCH) {
            run_inference(*st);
            st->in_count = 0;
            std::fill(st->in_pcm.begin(), st->in_pcm.end(), 0.0f);
        }

        const size_t need = frames * fch;

        if (st->out_avail >= need) {
            // Drain from ring
            std::memcpy(output,
                        st->out_pcm.data() + st->out_head,
                        need * sizeof(float));
            st->out_head  += need;
            st->out_avail -= need;

            // Compact ring once head reaches half capacity
            if (st->out_head > st->out_pcm.size() / 2) {
                st->out_pcm.erase(st->out_pcm.begin(),
                                  st->out_pcm.begin() + (long)st->out_head);
                st->out_head = 0;
            }
        } else {
            // Still accumulating — output silence
            std::fill(output, output + need, 0.0f);
        }
    };
}

#endif // PW_SIM_HAVE_SPLEETER
