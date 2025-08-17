#pragma once

#include <unordered_map>

#include "Pass/Pass.h"

namespace midend {

class BasicBlock;
class CallInst;
class CallGraph;
class Function;

class InlinePass : public ModulePass {
   public:
    InlinePass() : ModulePass("InlinePass", "Function Inlining") {}

    bool runOnModule(Module& module, AnalysisManager& am) override;

    static void setInlineThreshold(unsigned threshold) {
        inlineThreshold_ = threshold;
    }

    static unsigned getInlineThreshold() { return inlineThreshold_; }

    static void setMaxSizeGrowthThreshold(unsigned threshold) {
        maxSizeGrowthThreshold_ = threshold;
    }

    static unsigned getMaxSizeGrowthThreshold() {
        return maxSizeGrowthThreshold_;
    }

   private:
    static unsigned inlineThreshold_;
    static unsigned maxSizeGrowthThreshold_;
    unsigned inlineCounter_ = 0;  // Counter for generating unique inline IDs

    struct InlineCost {
        unsigned cost;
        bool shouldInline;
    };

    InlineCost calculateInlineCost(Function* callee, CallInst* callSite,
                                   const CallGraph& cg);
    bool shouldInline(Function* callee, CallInst* callSite,
                      const CallGraph& cg);
    bool inlineFunction(CallInst* callSite);
    void cloneAndMapInstructions(Function* caller, Function* callee,
                                 BasicBlock* callBB, BasicBlock* afterCallBB,
                                 std::unordered_map<Value*, Value*>& valueMap,
                                 Value** returnValue, unsigned inlineId);
};

}  // namespace midend
