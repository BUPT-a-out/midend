#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"

namespace midend {

class Instruction;
class Function;
class BasicBlock;

class ADCEPass : public FunctionPass {
   public:
    ADCEPass() : FunctionPass("ADCEPass", "Aggressive Dead Code Elimination") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    struct BlockInfoType;

    struct InstInfoType {
        bool Live = false;
        BlockInfoType* Block = nullptr;
    };

    struct BlockInfoType {
        bool Live = false;
        bool UnconditionalBranch = false;
        bool HasLivePhiNodes = false;
        bool CFLive = false;
        InstInfoType* TerminatorLiveInfo = nullptr;
        BasicBlock* BB = nullptr;
        Instruction* Terminator = nullptr;
        unsigned PostOrder = 0;

        bool terminatorIsLive() const {
            return TerminatorLiveInfo && TerminatorLiveInfo->Live;
        }
    };

    std::unordered_map<BasicBlock*, BlockInfoType> BlockInfo_;
    std::unordered_map<Instruction*, InstInfoType> InstInfo_;
    std::unordered_set<BasicBlock*> BlocksWithDeadTerminators_;
    std::unordered_set<BasicBlock*> NewLiveBlocks_;
    std::vector<Instruction*> Worklist_;
    PostDominanceInfo* PDT_;
    bool updatedCFG;

    void init() {
        BlockInfo_.clear();
        InstInfo_.clear();
        BlocksWithDeadTerminators_.clear();
        NewLiveBlocks_.clear();
        Worklist_.clear();
        PDT_ = nullptr;
        updatedCFG = false;
    }

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        required.insert(PostDominanceAnalysis::getName());
        if (!updatedCFG) {
            preserved.insert(PostDominanceAnalysis::getName());
        }
    }

    static bool isUnconditionalBranch(Instruction* Term);
    bool isAlwaysLive(Instruction* inst);
    bool isLive(Instruction* inst);
    void markLive(Instruction* inst);
    void markLive(BasicBlock* block);
    void markLive(BlockInfoType& blockInfo);
    void markPhiLive(Instruction* phi);
    void initialize(Function& function);
    void markLiveInstructions();
    void markLiveBranchesFromControlDependences();
    bool removeDeadInstructions(Function& function);
    void updateDeadRegions(Function& function);
};

}  // namespace midend