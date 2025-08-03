#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Analysis/LoopInfo.h"
#include "Pass/Pass.h"

namespace midend {

class Function;
class BasicBlock;
class Instruction;
class Value;
class Loop;
class PHINode;

class LICMPass : public FunctionPass {
   public:
    LICMPass() : FunctionPass("LICMPass", "Loop Invariant Code Motion Pass") {}
    ~LICMPass() = default;

    static const std::string& getName() {
        static const std::string name = "LICMPass";
        return name;
    }

    bool runOnFunction(Function& F, AnalysisManager& AM) override;

    void getAnalysisUsage(std::unordered_set<std::string>& required,
                          std::unordered_set<std::string>&) const override {
        required.insert(DominanceAnalysis::getName());
        required.insert(LoopAnalysis::getName());
        required.insert(AliasAnalysis::getName());
        required.insert(CallGraphAnalysis::getName());
    }

   private:
    DominanceInfo* DI = nullptr;
    LoopInfo* LI = nullptr;
    AliasAnalysis::Result* AA = nullptr;
    CallGraph* CG = nullptr;

    std::unordered_set<Value*> loopInvariants_;
    std::unordered_set<Instruction*> hoistedInstructions_;
    std::unordered_map<Instruction*, Loop*> instructionToLoop_;

    bool processLoop(Loop* L);
    void identifyLoopInvariants(Loop* L);
    bool hoistInstructions(Loop* L, BasicBlock* preheader);
    bool hoistToFunctionEntry();
    bool simplifyInvariantPHIs(Loop* L);
    bool isLoopInvariant(Value* V, Loop* L) const;
    bool canHoistInstruction(Instruction* I, Loop* L) const;
    bool isMemorySafe(Instruction* I, Loop* L) const;
    bool hasSideEffects(Instruction* I) const;
    bool isDominatedByLoop(Instruction* I, Loop* L) const;
    bool isAlwaysExecuted(Instruction* I, Loop* L) const;
    bool isSafeToSpeculate(Instruction* I) const;
    BasicBlock* getOrCreatePreheader(Loop* L);
    bool isPureFunction(Function* F) const;
    void moveInstructionToPreheader(Instruction* I, BasicBlock* preheader);
    std::vector<Loop*> getLoopsInPostOrder(
        const std::vector<std::unique_ptr<Loop>>& topLevelLoops);
    void addLoopsToPostOrder(Loop* L, std::vector<Loop*>& postOrder,
                             std::unordered_set<Loop*>& visited);
    Loop* findOutermostInvariantLoop(Instruction* I, Loop* currentLoop);
    bool compareInstructionsForHoisting(Instruction* A, Instruction* B);
};

}  // namespace midend