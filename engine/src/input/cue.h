#pragma once

#include <string>

namespace shuowen {

class Cue {
 public:
  static void Play(bool start, const std::string& theme,
                   const std::string& volume);
};

}  // namespace shuowen
