#include "type_output.h"

#include "util/text.h"

namespace shuowen {

TypeOutputPlan PlanTypeOutput(const std::string& previous_utf8,
                              const std::string& raw_next_utf8,
                              bool traditional) {
  TypeOutputPlan plan;
  plan.previous = Utf8ToWide(previous_utf8);
  plan.next = PostProcessText(raw_next_utf8, traditional);
  plan.next_utf8 = WideToUtf8(plan.next);
  plan.changed = plan.next_utf8 != previous_utf8;
  return plan;
}

}  // namespace shuowen
