#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "args.h"
#include "causal_lm.h"
#include "common/json_reader.h"
#include "dialog.h"

namespace llm {

using CausalLMFactory =
    std::function<std::unique_ptr<CausalLM>(const ModelArgs& args,
                                            const QuantizationArgs& quant_args,
                                            const ParallelArgs& parallel_args,
                                            torch::ScalarType dtype,
                                            const torch::Device& device)>;

using DialogFactory = std::function<std::unique_ptr<Dialog>()>;

using ModelArgsLoader =
    std::function<bool(const JsonReader& json, ModelArgs* args)>;

using QuantizationArgsLoader =
    std::function<bool(const JsonReader& json, QuantizationArgs* args)>;

// TODO: add default args loader.
struct ModelMeta {
  CausalLMFactory causal_lm_factory;
  DialogFactory dialog_factory;
  ModelArgsLoader model_args_loader;
  QuantizationArgsLoader quant_args_loader;
};

// Model registry is a singleton class that registers all models with the
// ModelFactory, ModelArgParser to facilitate model loading.
class ModelRegistry {
 public:
  static ModelRegistry* get_instance();

  static void register_causallm_factory(const std::string& name,
                                        CausalLMFactory factory);

  static void register_model_args_loader(const std::string& name,
                                         ModelArgsLoader loader);

  static void register_quant_args_loader(const std::string& name,
                                         QuantizationArgsLoader loader);

  static void register_dialog_factory(const std::string& name,
                                      DialogFactory factory);

  static CausalLMFactory get_causallm_factory(const std::string& name);

  static ModelArgsLoader get_model_args_loader(const std::string& name);

  static QuantizationArgsLoader get_quant_args_loader(const std::string& name);

  static DialogFactory get_dialog_factory(const std::string& name);

 private:
  std::unordered_map<std::string, ModelMeta> model_registry_;
};

// Macro to register a model with the ModelRegistry
#define REGISTER_CAUSAL_MODEL(ModelType, ModelClass)                        \
  const bool ModelType##_registered = []() {                                \
    ModelRegistry::register_causallm_factory(                               \
        #ModelType,                                                         \
        [](const ModelArgs& args,                                           \
           const QuantizationArgs& quant_args,                              \
           const ParallelArgs& parallel_args,                               \
           torch::ScalarType dtype,                                         \
           const torch::Device& device) {                                   \
          ModelClass model(args, quant_args, parallel_args, dtype, device); \
          model->eval();                                                    \
          return std::make_unique<llm::CausalLMImpl<ModelClass>>(           \
              std::move(model));                                            \
        });                                                                 \
    return true;                                                            \
  }()

#define REGISTER_DIALOG(ModelType, ModelClass)                        \
  const bool ModelType##_dialog_registered = []() {                   \
    ModelRegistry::register_dialog_factory(                           \
        #ModelType, []() { return std::make_unique<ModelClass>(); }); \
    return true;                                                      \
  }()

// Macro to register a model args loader with the ModelRegistry
#define REGISTER_MODEL_ARGS_LOADER(ModelType, Loader)              \
  const bool ModelType##_args_loader_registered = []() {           \
    ModelRegistry::register_model_args_loader(#ModelType, Loader); \
    return true;                                                   \
  }()

// Macro to register a quantization args loader with the ModelRegistry
#define REGISTER_QUANT_ARGS_LOADER(ModelType, Loader)              \
  const bool ModelType##_quant_args_loader_registered = []() {     \
    ModelRegistry::register_quant_args_loader(#ModelType, Loader); \
    return true;                                                   \
  }()

#define REGISTER_MODEL_ARGS(ModelType, ...)                                \
  REGISTER_MODEL_ARGS_LOADER(ModelType,                                    \
                             [](const JsonReader& json, ModelArgs* args) { \
                               __VA_ARGS__();                              \
                               return true;                                \
                             })

#define LOAD_ARG_OR(arg_name, json_name, default_value)           \
  [&] {                                                           \
    auto value = args->arg_name();                                \
    args->arg_name() =                                            \
        json.value_or<decltype(value)>(json_name, default_value); \
  }()

#define LOAD_OPTIONAL_ARG(arg_name, json_name)                             \
  [&] {                                                                    \
    auto value = args->arg_name();                                         \
    args->arg_name() = json.value<decltype(value)::value_type>(json_name); \
  }()

#define LOAD_ARG_OR_FUNC(arg_name, json_name, ...)                  \
  [&] {                                                             \
    auto value = args->arg_name();                                  \
    if (auto data_value = json.value<decltype(value)>(json_name)) { \
      args->arg_name() = data_value.value();                        \
    } else {                                                        \
      args->arg_name() = __VA_ARGS__();                             \
    }                                                               \
  }()

}  // namespace llm
