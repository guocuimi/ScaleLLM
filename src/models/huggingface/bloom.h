#pragma once

#include <torch/torch.h>
#include <torch/types.h>

#include "layers/activation.h"
#include "layers/attention_alibi.h"
#include "layers/embedding.h"
#include "layers/linear.h"
#include "layers/normalization.h"
#include "layers/pos_embedding.h"
#include "memory/kv_cache.h"
#include "models/args.h"
#include "models/input_parameters.h"
#include "models/model_registry.h"

// bloom model compatible with huggingface weights

namespace llm::hf {

class BloomMLPImpl : public torch::nn::Module {
 public:
  BloomMLPImpl(const ModelArgs& args,
               const QuantizationArgs& quant_args,
               const ParallelArgs& parallel_args,
               torch::ScalarType dtype,
               const torch::Device& device) {
    const int64_t hidden_size = args.hidden_size();
    const int64_t intermediate_size = args.intermediate_size();

    act_ = Activation::get_act_func("gelu", device);
    GCHECK(act_ != nullptr);

    // register the weight parameter
    dense_h_to_4h_ =
        register_module("dense_h_to_4h",
                        ColumnParallelLinear(hidden_size,
                                             intermediate_size,
                                             /*bias=*/true,
                                             /*gather_output=*/false,
                                             quant_args,
                                             parallel_args,
                                             dtype,
                                             device));
    dense_4h_to_h_ =
        register_module("dense_4h_to_h",
                        RowParallelLinear(intermediate_size,
                                          hidden_size,
                                          /*bias=*/true,
                                          /*input_is_parallelized=*/true,
                                          quant_args,
                                          parallel_args,
                                          dtype,
                                          device));
  }

  torch::Tensor forward(torch::Tensor x) {
    return dense_4h_to_h_(act_(dense_h_to_4h_(x)));
  }

  // load the weight from the checkpoint
  void load_state_dict(const StateDict& state_dict) {
    // call each submodule's load_state_dict function
    dense_h_to_4h_->load_state_dict(state_dict.select("dense_h_to_4h."));
    dense_4h_to_h_->load_state_dict(state_dict.select("dense_4h_to_h."));
  }

  void verify_loaded_weights(const std::string& prefix) const {
    dense_h_to_4h_->verify_loaded_weights(prefix + "dense_h_to_4h.");
    dense_4h_to_h_->verify_loaded_weights(prefix + "dense_4h_to_h.");
  }

 private:
  // parameter members, must be registered
  ColumnParallelLinear dense_h_to_4h_{nullptr};
  RowParallelLinear dense_4h_to_h_{nullptr};

  ActFunc act_{nullptr};
};
TORCH_MODULE(BloomMLP);

class BloomAttentionImpl : public torch::nn::Module {
 public:
  BloomAttentionImpl(const ModelArgs& args,
                     const QuantizationArgs& quant_args,
                     const ParallelArgs& parallel_args,
                     torch::ScalarType dtype,
                     const torch::Device& device) {
    const auto world_size = parallel_args.world_size();
    const int64_t n_heads = args.n_heads();
    const int64_t n_local_heads = n_heads / world_size;
    hidden_size_ = args.hidden_size();
    head_dim_ = hidden_size_ / n_heads;

    // register submodules
    query_key_value_ =
        register_module("query_key_value",
                        ColumnParallelLinear(hidden_size_,
                                             3 * hidden_size_,
                                             /*bias=*/true,
                                             /*gather_output=*/false,
                                             quant_args,
                                             parallel_args,
                                             dtype,
                                             device));

    dense_ = register_module("dense",
                             RowParallelLinear(hidden_size_,
                                               hidden_size_,
                                               /*bias=*/true,
                                               /*input_is_parallelized=*/true,
                                               quant_args,
                                               parallel_args,
                                               dtype,
                                               device));

    // initialize positional embedding
    const torch::Tensor alibi_slopes =
        prepare_alibi_slopes(n_heads, parallel_args);
    const float scale = 1.0f / std::sqrt(static_cast<float>(head_dim_));
    atten_ = register_module("atten",
                             AttentionWithAlibi(n_local_heads,
                                                n_local_heads,
                                                head_dim_,
                                                scale,
                                                alibi_slopes,
                                                dtype,
                                                device));
  }

  torch::Tensor forward(torch::Tensor x,
                        KVCache& kv_cache,
                        const InputParameters& input_params) {
    auto qkv = query_key_value_(x).chunk(/*chunks=*/3, /*dim=*/-1);
    DCHECK_EQ(qkv.size(), 3);
    // calculate attention, output: (num_tokens, n_local_heads * head_dim)
    auto output = atten_(qkv[0], qkv[1], qkv[2], kv_cache, input_params);
    return dense_(output);
  }

  // load the weight from the checkpoint
  void load_state_dict(const StateDict& state_dict) {
    // call each submodule's load_state_dict function
    query_key_value_->load_state_dict(state_dict.select("query_key_value."),
                                      [this](const torch::Tensor& tensor) {
                                        return reshape_qkv_tensor(tensor);
                                      });
    dense_->load_state_dict(state_dict.select("dense."));
  }

  void verify_loaded_weights(const std::string& prefix) const {
    query_key_value_->verify_loaded_weights(prefix + "query_key_value.");
    dense_->verify_loaded_weights(prefix + "dense.");
  }

 private:
  static torch::Tensor prepare_alibi_slopes(int64_t n_heads,
                                            const ParallelArgs& parallel_args) {
    // calculate alibi_slopes
    const int64_t closest_power_of_2 =
        std::pow(2, std::floor(std::log2(n_heads)));
    const float base =
        std::pow(2, -(std::pow(2, -(std::log2(closest_power_of_2) - 3))));
    const torch::Tensor powers = torch::arange(
        /*start=*/1, /*end=*/1 + closest_power_of_2, torch::kFloat32);
    torch::Tensor slopes = torch::pow(base, powers);

    if (closest_power_of_2 != n_heads) {
      const float extra_base =
          std::pow(2, -(std::pow(2, -(std::log2(2 * closest_power_of_2) - 3))));
      const int64_t n_remaining_heads =
          std::min(closest_power_of_2, n_heads - closest_power_of_2);
      const torch::Tensor extra_powers =
          torch::arange(/*start=*/1,
                        /*end=*/1 + 2 * n_remaining_heads,
                        /*step=*/2,
                        torch::kFloat32);
      const torch::Tensor extra_slopes = torch::pow(extra_base, extra_powers);
      slopes = torch::cat({slopes, extra_slopes}, /*dim=*/0);
    }
    if (parallel_args.world_size() > 1) {
      slopes = slopes.chunk(/*chunks=*/parallel_args.world_size(),
                            /*dim=*/0)[parallel_args.rank()];
    }
    return slopes;
  }
  // reshape qkv tensor from [n_heads, 3, ...] to [3, n_heads, ...]
  torch::Tensor reshape_qkv_tensor(const torch::Tensor& tensor) {
    GCHECK(tensor.dim() == 2 || tensor.dim() == 1)
        << "unexpected tensor dim: " << tensor.dim();
    if (tensor.dim() == 2) {
      return tensor.view({-1, 3, head_dim_, hidden_size_})
          .permute({1, 0, 2, 3})
          .reshape({-1, hidden_size_});
    }
    return tensor.view({-1, 3, head_dim_}).permute({1, 0, 2}).reshape({-1});
  }

  // parameter members, must be registered
  ColumnParallelLinear query_key_value_{nullptr};

  RowParallelLinear dense_{nullptr};

  // module members without parameters
  AttentionWithAlibi atten_{nullptr};

  int64_t hidden_size_ = 0;
  int64_t head_dim_ = 0;
};
TORCH_MODULE(BloomAttention);

class BloomBlockImpl : public torch::nn::Module {
 public:
  BloomBlockImpl(const ModelArgs& args,
                 const QuantizationArgs& quant_args,
                 const ParallelArgs& parallel_args,
                 torch::ScalarType dtype,
                 const torch::Device& device)
      : residual_post_layernorm_(args.residual_post_layernorm()) {
    // register submodules
    self_attention_ = register_module(
        "self_attention",
        BloomAttention(args, quant_args, parallel_args, dtype, device));
    mlp_ = register_module(
        "mlp", BloomMLP(args, quant_args, parallel_args, dtype, device));
    input_layernorm_ = register_module("input_layernorm",
                                       LayerNorm(args.hidden_size(),
                                                 args.layer_norm_eps(),
                                                 /*bias=*/true,
                                                 dtype,
                                                 device));
    post_attention_layernorm_ = register_module("post_attention_layernorm",
                                                LayerNorm(args.hidden_size(),
                                                          args.layer_norm_eps(),
                                                          /*bias=*/true,
                                                          dtype,
                                                          device));
  }

  torch::Tensor forward(torch::Tensor x,
                        KVCache& kv_cache,
                        const InputParameters& input_params) {
    auto ln_output = input_layernorm_(x);
    auto residual = residual_post_layernorm_ ? ln_output : x;

    auto attn_output = self_attention_(ln_output, kv_cache, input_params);
    attn_output += residual;

    ln_output = post_attention_layernorm_(attn_output);
    residual = residual_post_layernorm_ ? ln_output : attn_output;
    return mlp_(ln_output) + residual;
  }

  // load the weight from the checkpoint
  void load_state_dict(const StateDict& state_dict) {
    // call each submodule's load_state_dict function
    self_attention_->load_state_dict(state_dict.select("self_attention."));
    mlp_->load_state_dict(state_dict.select("mlp."));
    input_layernorm_->load_state_dict(state_dict.select("input_layernorm."));
    post_attention_layernorm_->load_state_dict(
        state_dict.select("post_attention_layernorm."));
  }

  void verify_loaded_weights(const std::string& prefix) const {
    self_attention_->verify_loaded_weights(prefix + "self_attention.");
    mlp_->verify_loaded_weights(prefix + "mlp.");
    input_layernorm_->verify_loaded_weights(prefix + "input_layernorm.");
    post_attention_layernorm_->verify_loaded_weights(
        prefix + "post_attention_layernorm.");
  }

 private:
  // parameter members, must be registered
  BloomAttention self_attention_{nullptr};

  BloomMLP mlp_{nullptr};

  LayerNorm input_layernorm_{nullptr};

  LayerNorm post_attention_layernorm_{nullptr};

  bool residual_post_layernorm_ = false;
};
TORCH_MODULE(BloomBlock);

class BloomModelImpl : public torch::nn::Module {
 public:
  BloomModelImpl(const ModelArgs& args,
                 const QuantizationArgs& quant_args,
                 const ParallelArgs& parallel_args,
                 torch::ScalarType dtype,
                 const torch::Device& device) {
    // register submodules
    word_embeddings_ = register_module("word_embeddings",
                                       ParallelEmbedding(args.vocab_size(),
                                                         args.hidden_size(),
                                                         parallel_args,
                                                         dtype,
                                                         device));
    word_embeddings_layernorm_ =
        register_module("word_embeddings_layernorm",
                        LayerNorm(args.hidden_size(),
                                  args.layer_norm_eps(),
                                  /*bias=*/true,
                                  dtype,
                                  device));

    blocks_ = register_module("h", torch::nn::ModuleList());
    layers_.reserve(args.n_layers());
    for (int32_t i = 0; i < args.n_layers(); i++) {
      auto block = BloomBlock(args, quant_args, parallel_args, dtype, device);
      layers_.push_back(block);
      blocks_->push_back(block);
    }
    ln_f_ = register_module("ln_f",
                            LayerNorm(args.hidden_size(),
                                      args.layer_norm_eps(),
                                      /*bias=*/true,
                                      dtype,
                                      device));
  }

  // tokens: [num_tokens]
  torch::Tensor forward(torch::Tensor tokens,
                        std::vector<KVCache>& kv_caches,
                        const InputParameters& input_params) {
    auto h = word_embeddings_(tokens);
    h = word_embeddings_layernorm_(h);
    for (size_t i = 0; i < layers_.size(); i++) {
      auto& layer = layers_[i];
      h = layer(h, kv_caches[i], input_params);
    }
    return ln_f_(h);
  }

  // load the weight from the checkpoint
  void load_state_dict(const StateDict& state_dict) {
    word_embeddings_->load_state_dict(state_dict.select("word_embeddings."));
    word_embeddings_layernorm_->load_state_dict(
        state_dict.select("word_embeddings_layernorm."));
    // call each layer's load_state_dict function
    for (int i = 0; i < layers_.size(); i++) {
      layers_[i]->load_state_dict(
          state_dict.select("h." + std::to_string(i) + "."));
    }
    ln_f_->load_state_dict(state_dict.select("ln_f."));
  }

  void verify_loaded_weights() const {
    word_embeddings_->verify_loaded_weights("word_embeddings.");
    word_embeddings_layernorm_->verify_loaded_weights(
        "word_embeddings_layernorm.");
    for (int i = 0; i < layers_.size(); i++) {
      layers_[i]->verify_loaded_weights("h." + std::to_string(i) + ".");
    }
    ln_f_->verify_loaded_weights("ln_f.");
  }

 private:
  // parameter members, must be registered
  ParallelEmbedding word_embeddings_{nullptr};

  LayerNorm word_embeddings_layernorm_{nullptr};

  torch::nn::ModuleList blocks_{nullptr};
  // hold same data but different type as blocks_ to avoid type cast
  std::vector<BloomBlock> layers_;

  // final layer norm
  LayerNorm ln_f_{nullptr};
};
TORCH_MODULE(BloomModel);

class BloomForCausalLMImpl : public torch::nn::Module {
 public:
  BloomForCausalLMImpl(const ModelArgs& args,
                       const QuantizationArgs& quant_args,
                       const ParallelArgs& parallel_args,
                       torch::ScalarType dtype,
                       const torch::Device& device) {
    // register submodules
    model_ = register_module(
        "transformer",
        BloomModel(args, quant_args, parallel_args, dtype, device));

    lm_head_ = register_module("lm_head",
                               ColumnParallelLinear(args.hidden_size(),
                                                    args.vocab_size(),
                                                    /*bias=*/false,
                                                    /*gather_output=*/true,
                                                    parallel_args,
                                                    dtype,
                                                    device));
  }

  // tokens: [num_tokens]
  // positions: [num_tokens] token pos in the sequence
  torch::Tensor forward(torch::Tensor tokens,
                        torch::Tensor /*positions*/,
                        std::vector<KVCache>& kv_caches,
                        const InputParameters& input_params) {
    auto h = model_(tokens, kv_caches, input_params);
    // select last token for each sequence
    h = h.index_select(/*dim=*/0, input_params.last_token_indicies);
    return lm_head_(h);
  }

  // load the weight from the checkpoint
  void load_state_dict(const StateDict& state_dict) {
    model_->load_state_dict(state_dict);
    // load lm_head weights from word_embeddings
    lm_head_->load_state_dict(state_dict.select("word_embeddings."));
  }

  void verify_loaded_weights() const {
    model_->verify_loaded_weights();
    lm_head_->verify_loaded_weights("word_embeddings.");
  }

 private:
  // parameter members, must be registered
  BloomModel model_{nullptr};

  ColumnParallelLinear lm_head_{nullptr};
};
TORCH_MODULE(BloomForCausalLM);

// register the model to make it available
REGISTER_CAUSAL_MODEL(bloom, BloomForCausalLM);
REGISTER_MODEL_ARGS(bloom, [&] {
  // example config:
  // https://huggingface.co/bigscience/bloom/blob/main/config.json
  LOAD_ARG_OR(dtype, "torch_dtype", "");
  LOAD_ARG_OR(vocab_size, "vocab_size", 250880);
  LOAD_ARG_OR(hidden_size, "n_embed", 14336);
  LOAD_ARG_OR(n_layers, "n_layer", 70);
  LOAD_ARG_OR(n_heads, "num_attention_heads", 112);
  LOAD_ARG_OR(layer_norm_eps, "layer_norm_epsilon", 1e-5);
  LOAD_ARG_OR(bos_token_id, "bos_token_id", 1);
  LOAD_ARG_OR(eos_token_id, "eos_token_id", 2);
  LOAD_ARG_OR(residual_post_layernorm,
              "apply_residual_connection_post_layernorm",
              false);

  LOAD_ARG_OR_FUNC(intermediate_size, "intermediate_size", [&] {
    return args->hidden_size() * 4;
  });
});

}  // namespace llm::hf
