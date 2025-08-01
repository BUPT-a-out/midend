#pragma once

#include <unordered_set>
#include <vector>

#include "Pass/Pass.h"

namespace midend {

class BasicBlock;
class Function;
class CallInst;
class ReturnInst;
class PHINode;
class Value;
class CallGraph;

class TailRecursionOptimizationPass : public FunctionPass {
   public:
    TailRecursionOptimizationPass()
        : FunctionPass("TailRecursionOptimizationPass",
                       "Tail Recursion Optimization") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    struct TailCall {
        CallInst* callInst;
        ReturnInst* returnInst;
        BasicBlock* block;
    };

    ReturnInst* getTailCallReturnInst(CallInst* callInst,
                                      const CallGraph* callGraph);
    bool hasSideEffectsBetween(CallInst* callInst, ReturnInst* retInst,
                               const CallGraph* callGraph);
    bool transformToLoop(Function& function,
                         const std::vector<TailCall>& tailCalls);
    BasicBlock* createLoopHeader(Function& function);
    void updateCallsToLoop(const std::vector<TailCall>& tailCalls,
                           BasicBlock* loopHeader);
    void createPHINodes(BasicBlock* loopHeader, Function& function);
};

}  // namespace midend