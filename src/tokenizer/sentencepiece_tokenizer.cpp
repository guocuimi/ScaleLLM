#include "sentencepiece_tokenizer.h"

#include "common/logging.h"
#include "sentencepiece/sentencepiece_processor.h"

namespace llm {

SentencePieceTokenizer::SentencePieceTokenizer(
    const std::string& vocab_file_path,
    bool prepend_bos)
    : vocab_file_path_(vocab_file_path), prepend_bos_(prepend_bos) {
  const auto status = sp_processor_.Load(vocab_file_path);
  if (!status.ok()) {
    GLOG(FATAL) << "Failed to load SentencePiece model from " << vocab_file_path
                << ": " << status.ToString() << ", error " << status.ToString();
  }
}

std::unique_ptr<Tokenizer> SentencePieceTokenizer::clone() const {
  return std::make_unique<SentencePieceTokenizer>(this->vocab_file_path_,
                                                  this->prepend_bos_);
}

bool SentencePieceTokenizer::encode(const std::string_view& text,
                                    std::vector<int32_t>* ids) const {
  const auto status = sp_processor_.Encode({text.data(), text.size()}, ids);
  if (!status.ok()) {
    GLOG(ERROR) << "Failed to encode text: " << text << ", error "
                << status.ToString();
    return false;
  }
  // prepend bos token
  if (prepend_bos_) {
    ids->insert(ids->begin(), sp_processor_.bos_id());
  }
  return true;
}

std::string SentencePieceTokenizer::decode(
    const std::vector<int32_t>& ids) const {
  std::string text;
  const auto status = sp_processor_.Decode(ids, &text);
  if (!status.ok()) {
    GLOG(ERROR) << "Failed to decode ids: " << status.ToString();
  }
  return text;
}

}  // namespace llm
