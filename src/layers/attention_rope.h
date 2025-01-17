#pragma once

#include <torch/torch.h>

#include "layers/pos_embedding.h"
#include "memory/kv_cache.h"
#include "models/input_parameters.h"

namespace llm {

// Attention with rotary embedding
class AttentionWithRoPEImpl : public torch::nn::Module {
 public:
  AttentionWithRoPEImpl(int64_t n_heads,
                        int64_t n_kv_heads,
                        int64_t head_dim,
                        float scale,
                        int64_t rotary_dim,
                        float rope_sclaing,
                        float rope_theta,
                        int64_t max_position,
                        bool interleaved,
                        torch::ScalarType dtype,
                        const torch::Device& device);

  // query: [num_tokens, n_heads, head_dim]
  // key/value: [num_tokens, n_kv_heads, head_dim]
  // return: [num_tokens, n_heads, head_dim]
  torch::Tensor forward(const torch::Tensor& query,
                        const torch::Tensor& key,
                        const torch::Tensor& value,
                        const torch::Tensor& positions,
                        KVCache& kv_cache,
                        const InputParameters& input_params);

 private:
  RotaryEmbedding pos_emb_{nullptr};

  int64_t n_heads_ = 0;
  int64_t n_kv_heads_ = 0;
  int64_t head_dim_ = 0;

  // scale factor
  float scale_ = 0.0;

  // head mapping used for single_query_masked_self_attention
  // [num_heads]
  torch::Tensor kv_head_mapping_;
};
TORCH_MODULE(AttentionWithRoPE);

}  // namespace llm
