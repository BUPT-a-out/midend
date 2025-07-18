#pragma once

#include <unordered_map>
#include <unordered_set>

#include "Pass/Pass.h"

namespace midend {

class BasicBlock;
class Function;
class Instruction;
class PHINode;

class SimplifyCFGPass : public FunctionPass {
   public:
    SimplifyCFGPass()
        : FunctionPass("SimplifyCFGPass", "Control Flow Graph Simplification") {
    }

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    std::unordered_set<BasicBlock*> blocksToRemove_;
    std::unordered_map<BasicBlock*, BasicBlock*> blockRedirects_;

    void init() {
        blocksToRemove_.clear();
        blockRedirects_.clear();
    }

    bool removeUnreachableBlocks(Function& function);
    bool mergeBlocks(Function& function);
    bool eliminateEmptyBlocks(Function& function);
    bool removeDuplicatePHINodes(Function& function);

    std::unordered_set<BasicBlock*> findUnreachableBlocks(Function& function);
    bool canMergeWithPredecessor(BasicBlock* block);
    void mergeBlockIntoPredecessor(BasicBlock* block);
    bool canBeEliminate(BasicBlock* block);
    bool arePHINodesIdentical(PHINode* phi1, PHINode* phi2);
};

}  // namespace midend