#pragma once

#include <queue>
#include <unordered_set>

#include "Pass/Pass.h"

namespace midend {

class Instruction;
class Function;

class ADCEPass : public FunctionPass {
   public:
    ADCEPass() : FunctionPass("ADCEPass", "Aggressive Dead Code Elimination") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    std::unordered_set<Instruction*> liveInstructions_;
    std::queue<Instruction*> workList_;
    bool deletedBlocks_;

    void init() {
        liveInstructions_.clear();
        while (!workList_.empty()) {
            workList_.pop();
        }
        deletedBlocks_ = false;
    }

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        required.insert("dominance");
        if (!deletedBlocks_) {
            preserved.insert("dominance");
        }
    }

    void markInstructionLive(Instruction* inst);
    bool isAlwaysLive(Instruction* inst);
    void performLivenessAnalysis(Function& function);
    bool removeDeadInstructions(Function& function);
};

}  // namespace midend