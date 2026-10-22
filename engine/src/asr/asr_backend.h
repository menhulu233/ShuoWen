#pragma once

#include <cstddef>
#include <string>

namespace shuowen {

// Streaming ASR surface used by Dictation. Keeping it abstract lets the
// worker loop run against a deterministic fake in the self-tests.
class AsrBackend {
 public:
  virtual ~AsrBackend() = default;

  virtual bool ready() const = 0;
  virtual void Accept(const float* samples, size_t count) = 0;
  virtual std::string Partial() = 0;
  virtual std::string Finalize() = 0;
  virtual void ResetStream() = 0;

  virtual void VadAccept(const float* samples, size_t count) = 0;
  virtual bool VadSpeech() const = 0;
  virtual bool VadHasSegment() const = 0;
  virtual void VadPopSegment() = 0;
  virtual void VadFlush() = 0;
  virtual void VadReset() = 0;
};

}  // namespace shuowen