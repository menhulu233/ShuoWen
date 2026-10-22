#pragma once

#include <string>

namespace shuowen {

struct TypeOutputPlan {
  bool changed = false;
  std::wstring previous;
  std::wstring next;
  std::string next_utf8;
};

TypeOutputPlan PlanTypeOutput(const std::string& previous_utf8,
                              const std::string& raw_next_utf8,
                              bool traditional);

}  // namespace shuowen
