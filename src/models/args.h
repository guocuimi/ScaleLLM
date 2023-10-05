#pragma once

#include <optional>

#include "common/arg.h"
#include "common/process_group.h"

namespace llm {

struct ModelArgs {
  DEFINE_ARG(std::string, model_type);

  // dimension of the encoder layer.
  DEFINE_ARG(int64_t, hidden_size) = 0;

  DEFINE_ARG(std::string, hidden_act);

  // dimension of the 'intermediate' (aka feed-forward) layer.
  DEFINE_ARG(int64_t, intermediate_size) = 0;

  // number of hidden layers in the encoder.
  DEFINE_ARG(int64_t, n_layers) = 0;

  // number of attention heads.
  DEFINE_ARG(int64_t, n_heads) = 0;

  // number of attention heads for key/value.
  DEFINE_ARG(std::optional<int64_t>, n_kv_heads);

  // number of tokens in the vocabulary.
  DEFINE_ARG(int64_t, vocab_size) = -1;

  // the epsilon value to use for rms norm.
  DEFINE_ARG(float, rms_norm_eps) = 0.0f;

  // the epsilon value to use for layer norm.
  DEFINE_ARG(float, layer_norm_eps) = 0.0f;

  // args for rotary position embeddings
  // the base period of the rotary position embeddings.
  DEFINE_ARG(float, rope_theta) = 10000.0f;

  // rope scaling factor.
  DEFINE_ARG(float, rope_scaling) = 0.0f;

  // percentage of hidden dimension to allocate to rotary position embeddings.
  DEFINE_ARG(float, rotary_pct) = 1.0f;

  // the maximum sequence length to use for rotary position embeddings.
  DEFINE_ARG(int64_t, max_position_embeddings) = 0;

  // token id for beginning of sentence.
  DEFINE_ARG(int32_t, bos_token_id) = 0;

  // token id for end of sentence.
  DEFINE_ARG(int32_t, eos_token_id) = 0;

  // configs for gpt_neox
  // whether to use a 'parallel' formulation in each transformer layer, which
  // can provide a slight training speedup at large scales (e.g. 20B).
  DEFINE_ARG(bool, use_parallel_residual) = false;
};

inline std::ostream& operator<<(std::ostream& os, const ModelArgs& args) {
  os << "ModelArgs: [model_type: " << args.model_type();
  os << ", hidden_size: " << args.hidden_size();
  os << ", hidden_act: " << args.hidden_act();
  os << ", intermediate_size: " << args.intermediate_size();
  os << ", n_layers: " << args.n_layers();
  os << ", n_heads: " << args.n_heads();
  os << ", n_kv_heads: " << args.n_kv_heads().value_or(0);
  os << ", vocab_size: " << args.vocab_size();
  os << ", rms_norm_eps: " << args.rms_norm_eps();
  os << ", layer_norm_eps: " << args.layer_norm_eps();
  os << ", rope_theta: " << args.rope_theta();
  os << ", rope_scaling: " << args.rope_scaling();
  os << ", rotary_pct: " << args.rotary_pct();
  os << ", max_position_embeddings: " << args.max_position_embeddings();
  os << ", bos_token_id: " << args.bos_token_id();
  os << ", eos_token_id: " << args.eos_token_id();
  os << ", use_parallel_residual: " << args.use_parallel_residual();
  os << "]";
  return os;
}

struct QuantizationArgs {
  DEFINE_ARG(std::string, quant_method) = "";

  // quantization bits
  DEFINE_ARG(int64_t, bits) = 0;

  // quantization group size
  DEFINE_ARG(int64_t, group_size) = 0;

  DEFINE_ARG(bool, desc_act) = false;

  DEFINE_ARG(bool, true_sequential) = false;
};

inline std::ostream& operator<<(std::ostream& os, const QuantizationArgs& args) {
  os << "QuantizationArgs: [";
  os << "quant_method: " << args.quant_method();
  os << ", bits: " << args.bits();
  os << ", group_size: " << args.group_size();
  os << ", desc_act: " << args.desc_act();
  os << ", true_sequential: " << args.true_sequential();
  os << "]";
  return os;
}


struct ParallelArgs {
  ParallelArgs(int32_t rank, int32_t world_size, ProcessGroup* process_group)
      : rank_(rank), world_size_(world_size), process_group_(process_group) {}

  // rank of current process
  DEFINE_ARG(int32_t, rank) = 0;

  // world size
  DEFINE_ARG(int32_t, world_size) = 0;

  // pointer to process group, nullptr if world size is 1
  DEFINE_PTR_ARG(ProcessGroup, process_group) = nullptr;
};

inline std::ostream& operator<<(std::ostream& os, const ParallelArgs& args) {
  os << "ParallelArgs: [";
  os << "rank: " << args.rank();
  os << ", world_size: " << args.world_size();
  os << "]";
  return os;
}

}  // namespace llm