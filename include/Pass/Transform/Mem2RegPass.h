#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"

namespace midend {

class Mem2RegContext {
   private:
    using ValueStack = std::vector<std::unordered_map<AllocaInst*, Value*>>;

    std::unordered_map<BasicBlock*, std::unordered_map<AllocaInst*, Value*>>
        valueMap_;
    std::unordered_set<AllocaInst*> promotableAllocas_;
    std::vector<Instruction*> instructionsToRemove_;
    std::unordered_map<PHINode*, AllocaInst*> phiToAlloca_;
    std::unordered_set<BasicBlock*> visitedBlocks_;

    DominanceInfo* dominanceInfo_;

    bool isPromotable(AllocaInst* alloca);
    void collectPromotableAllocas(Function& function);
    void insertPhiNodes(Function& function);
    void performSSAConstruction(Function& function);
    void renameVariables(BasicBlock* block, ValueStack& valueStack);
    Value* decideVariableValue(AllocaInst* alloca,
                               const ValueStack& valueStack);
    void cleanupInstructions();

    Value* lookupValue(BasicBlock* block, AllocaInst* alloca);
    void setValue(BasicBlock* block, AllocaInst* alloca, Value* value);

   public:
    Mem2RegContext() {}
    bool runOnFunction(Function& function, AnalysisManager& am);
};

class Mem2RegPass : public FunctionPass {
   public:
    Mem2RegPass()
        : FunctionPass("Mem2RegPass", "Memory to Register Promotion") {}

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        required.insert("dominance");
        preserved.insert("dominance");
    }

    bool runOnFunction(Function& function, AnalysisManager& am) override {
        Mem2RegContext context;
        return context.runOnFunction(function, am);
    }
};

}  // namespace midend