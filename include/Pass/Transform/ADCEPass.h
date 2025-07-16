#pragma once

#include <unordered_set>
#include <vector>
#include <queue>

#include "Pass/Pass.h"

namespace midend {

class Instruction;
class Function;

class ADCEPass : public FunctionPass {
   public:
    ADCEPass()
        : FunctionPass("ADCEPass", "Aggressive Dead Code Elimination") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    std::unordered_set<Instruction*> liveInstructions_;
    std::queue<Instruction*> workList_;

    void markInstructionLive(Instruction* inst);
    bool isAlwaysLive(Instruction* inst);
    void performLivenessAnalysis(Function& function);
    bool removeDeadInstructions(Function& function);
};

}  // namespace midend