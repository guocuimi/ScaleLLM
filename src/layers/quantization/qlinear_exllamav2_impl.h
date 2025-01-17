#pragma once

#include <c10/core/TensorImpl.h>
#include <torch/torch.h>

#include "model_loader/state_dict.h"
#include "models/args.h"
#include "qlinear_impl.h"

namespace llm {

// Quantized Linear layer with column parallelism.
class ColumnParallelQLinearExllamav2Impl : public ColumnParallelQLinearImpl {
 public:
  ColumnParallelQLinearExllamav2Impl(int64_t in_features,
                                     int64_t out_features,
                                     bool bias,
                                     const QuantizationArgs& quant_args,
                                     bool gather_output,
                                     const ParallelArgs& parallel_args,
                                     torch::ScalarType dtype,
                                     const torch::Device& device);

  ~ColumnParallelQLinearExllamav2Impl() override;

  torch::Tensor quant_matmul(const torch::Tensor& input,
                             const torch::Tensor& qweight,
                             const torch::Tensor& qzeros,
                             const torch::Tensor& scales) const override;

 private:
  // parameter members, must be registered
  torch::Tensor g_idx_{nullptr};
  torch::Tensor q_perm_{nullptr};
  torch::Tensor q_invperm_{nullptr};

  bool qweight_is_loaded_ = false;

  // QMatrix handler for exllamav2
  mutable uintptr_t q_matrix_ = 0;

  // quantization parameters
  int64_t bits_ = 0;
};

// Linear layer with row parallelism.
class RowParallelQLinearExllamav2Impl : public RowParallelQLinearImpl {
 public:
  RowParallelQLinearExllamav2Impl(int64_t in_features,
                                  int64_t out_features,
                                  bool bias,
                                  const QuantizationArgs& quant_args,
                                  bool input_is_parallelized,
                                  const ParallelArgs& parallel_args,
                                  torch::ScalarType dtype,
                                  const torch::Device& device);

  ~RowParallelQLinearExllamav2Impl() override;

  torch::Tensor quant_matmul(const torch::Tensor& input,
                             const torch::Tensor& qweight,
                             const torch::Tensor& qzeros,
                             const torch::Tensor& scales) const override;

 private:
  // parameter members, must be registered
  torch::Tensor g_idx_{nullptr};
  torch::Tensor q_perm_{nullptr};
  torch::Tensor q_invperm_{nullptr};

  // QMatrix handler for exllamav2
  mutable uintptr_t q_matrix_ = 0;

  // quantization parameters
  int64_t bits_ = 0;
};
}  // namespace llm
