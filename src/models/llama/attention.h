#pragma once

#include <torch/torch.h>

#include "models/model_args.h"
#include "layers/linear.h"
#include "layers/pos_embedding.h"
#include "layers/attention.h"

namespace llm {

class AttentionImpl : public torch::nn::Module {
 public:
  AttentionImpl(const ModelArgs& args, int64_t world_size);

  torch::Tensor forward(torch::Tensor x,
                        torch::Tensor positions,
                        const std::vector<int64_t>& cu_seq_lens);

  // load the weight from the checkpoint
  void load_state_dict(const StateDict& state_dict);

  // parameter members, must be registered
  ColumnParallelLinear wq_{nullptr};

  ColumnParallelLinear wk_{nullptr};

  ColumnParallelLinear wv_{nullptr};

  RowParallelLinear wo_{nullptr};

  RotaryEmbedding pos_emb_{nullptr};

  // configs
  int64_t world_size_;
  int64_t n_kv_heads_;
  int64_t n_local_heads_;
  int64_t n_local_kv_heads_;
  int64_t n_rep_;
  int64_t head_dim_;
};

TORCH_MODULE(Attention);

}  // namespace llm
