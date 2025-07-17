#include "Pass/Transform/ADCEPass.h"

#include <iostream>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRPrinter.h"
#include "IR/Instruction.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Support/Casting.h"

namespace midend {

bool ADCEPass::runOnFunction(Function& function, AnalysisManager& am) {
    init();

    // Get post-dominance information
    PDT_ = am.getAnalysis<PostDominanceInfo>(PostDominanceAnalysis::getName(),
                                             function);
    if (!PDT_) {
        std::cerr << "Warning: PostDominanceAnalysis not available for "
                     "ADCEPass. Skipping function: "
                  << function.getName() << std::endl;
        return false;
    }

    initialize(function);
    markLiveInstructions();
    return removeDeadInstructions(function);
}

bool ADCEPass::isUnconditionalBranch(Instruction* Term) {
    auto* BR = dyn_cast<BranchInst>(Term);
    return BR && BR->isUnconditional();
}

bool ADCEPass::isAlwaysLive(Instruction* inst) {
    // Store instructions have side effects
    if (isa<StoreInst>(inst)) {
        return true;
    }

    // Call instructions might have side effects
    if (isa<CallInst>(inst)) {
        // TODO: check if the function is pure
        return true;
    }

    if (isa<ReturnInst>(inst)) {
        return true;
    }

    if (!inst->isTerminator()) {
        return false;
    }

    if (isa<BranchInst>(inst)) {
        return false;
    }

    return true;
}

bool ADCEPass::isLive(Instruction* inst) {
    auto it = InstInfo_.find(inst);
    return it != InstInfo_.end() && it->second.Live;
}

void ADCEPass::markLive(Instruction* inst) {
    auto& Info = InstInfo_[inst];
    if (Info.Live) {
        return;
    }

    Info.Live = true;
    Worklist_.push_back(inst);

    // Mark the containing block live
    auto& BBInfo = *Info.Block;
    if (BBInfo.Terminator == inst) {
        BlocksWithDeadTerminators_.erase(BBInfo.BB);
        // For live terminators, mark destination blocks
        // live to preserve this control flow edges.
        if (!BBInfo.UnconditionalBranch) {
            for (auto* successor : BBInfo.BB->getSuccessors()) {
                markLive(successor);
            }
        }
    }
    markLive(BBInfo);
}

void ADCEPass::markLive(BasicBlock* block) { markLive(BlockInfo_[block]); }

void ADCEPass::markLive(BlockInfoType& blockInfo) {
    if (blockInfo.Live) {
        return;
    }

    blockInfo.Live = true;
    if (!blockInfo.CFLive) {
        blockInfo.CFLive = true;
        NewLiveBlocks_.insert(blockInfo.BB);
    }

    // Mark unconditional branches at the end of live
    // blocks as live since there is no work to do for them later
    if (blockInfo.UnconditionalBranch) {
        markLive(blockInfo.Terminator);
    }
}

void ADCEPass::markPhiLive(Instruction* phi) {
    auto& Info = BlockInfo_[phi->getParent()];
    // Only need to check this once per block.
    if (Info.HasLivePhiNodes) {
        return;
    }
    Info.HasLivePhiNodes = true;

    // If a predecessor block is not live, mark it as control-flow live
    // which will trigger marking live branches upon which
    // that block is control dependent.
    for (auto* PredBB : phi->getParent()->getPredecessors()) {
        auto& PredInfo = BlockInfo_[PredBB];
        if (!PredInfo.CFLive) {
            PredInfo.CFLive = true;
            NewLiveBlocks_.insert(PredBB);
        }
    }
}

void ADCEPass::initialize(Function& function) {
    auto NumBlocks = function.size();

    // Initialize block info
    BlockInfo_.reserve(NumBlocks);
    size_t NumInsts = 0;

    // Iterate over blocks and initialize BlockInfo entries
    for (auto& BB : function) {
        NumInsts += BB->size();
        auto& Info = BlockInfo_[BB];
        Info.BB = BB;
        Info.Terminator = BB->getTerminator();
        if (!Info.Terminator) {
            std::cerr << "Warning: Block " << BB->getName()
                      << " has no terminator instruction." << std::endl;
            Info.Terminator = nullptr;
        } else {
            Info.TerminatorLiveInfo = &InstInfo_[Info.Terminator];
        }
        Info.UnconditionalBranch = isUnconditionalBranch(Info.Terminator);
    }

    // Initialize instruction map and set pointers to block info
    InstInfo_.reserve(NumInsts);
    for (auto& BBInfoPair : BlockInfo_) {
        for (auto* inst : *BBInfoPair.second.BB) {
            InstInfo_[inst].Block = &BBInfoPair.second;
        }
    }

    // Set up terminator live info pointers
    for (auto& BBInfoPair : BlockInfo_) {
        BBInfoPair.second.TerminatorLiveInfo =
            &InstInfo_[BBInfoPair.second.Terminator];
    }

    // Collect the set of "root" instructions that are known live
    for (auto& BB : function) {
        for (auto* inst : *BB) {
            if (isAlwaysLive(inst)) {
                markLive(inst);
            }
        }
    }

    // TODO(high): mark loops as live

    // TODO(low): mark non-return exit blocks as live

    // Treat the entry block as always live
    auto& EntryInfo = BlockInfo_[&function.front()];
    EntryInfo.Live = true;
    if (EntryInfo.UnconditionalBranch) {
        markLive(EntryInfo.Terminator);
    }

    // Build initial collection of blocks with dead terminators
    for (auto& BBInfoPair : BlockInfo_) {
        if (!BBInfoPair.second.terminatorIsLive()) {
            BlocksWithDeadTerminators_.insert(BBInfoPair.second.BB);
        }
    }
}

void ADCEPass::markLiveInstructions() {
    // Propagate liveness backwards to operands.
    do {
        // Worklist holds newly discovered live instructions
        // where we need to mark the inputs as live.
        while (!Worklist_.empty()) {
            Instruction* LiveInst = Worklist_.back();
            Worklist_.pop_back();

            // Mark all operands as live
            for (unsigned i = 0; i < LiveInst->getNumOperands(); ++i) {
                Value* operand = LiveInst->getOperand(i);
                if (auto* inst = dyn_cast<Instruction>(operand)) {
                    markLive(inst);
                }
            }

            if (auto* phi = dyn_cast<PHINode>(LiveInst)) {
                markPhiLive(phi);
            }
        }

        // After data flow liveness has been identified, examine which branch
        // decisions are required to determine live instructions are executed.
        markLiveBranchesFromControlDependences();

    } while (!Worklist_.empty());
}

void ADCEPass::markLiveBranchesFromControlDependences() {
    if (BlocksWithDeadTerminators_.empty()) {
        return;
    }
    // The dominance frontier of a live block X in the reverse
    // control graph is the set of blocks upon which X is control
    // dependent. We need to find blocks that control-depend on NewLiveBlocks_

    for (auto* liveBlock : NewLiveBlocks_) {
        const auto& postDomFrontier = PDT_->getDominanceFrontier(liveBlock);

        for (auto* controllingBlock : postDomFrontier) {
            if (BlocksWithDeadTerminators_.count(controllingBlock)) {
                markLive(controllingBlock->getTerminator());
            }
        }
    }

    NewLiveBlocks_.clear();
}

// TODO: review
void ADCEPass::updateDeadRegions(Function& function) {
    if (BlocksWithDeadTerminators_.empty()) {
        return;
    }
    bool havePostOrder = false;
    bool changed = false;

    for (auto* BB : BlocksWithDeadTerminators_) {
        auto& Info = BlockInfo_[BB];

        // Skip if this is an unconditional branch
        if (Info.UnconditionalBranch) {
            InstInfo_[Info.Terminator].Live = true;
            continue;
        }

        if (!havePostOrder) {
            auto postOrder = PDT_->computeReversePostOrder();
            for (size_t i = 0; i < postOrder.size(); ++i) {
                BlockInfo_[postOrder[i]].PostOrder = i + 1;
            }
            havePostOrder = true;
        }

        // Find the successor closest to the function exit
        BlockInfoType* PreferredSucc = nullptr;
        for (auto* Succ : BB->getSuccessors()) {
            auto* SuccInfo = &BlockInfo_[Succ];
            if (!PreferredSucc ||
                PreferredSucc->PostOrder > SuccInfo->PostOrder) {
                PreferredSucc = SuccInfo;
            }
        }

        if (!PreferredSucc) {
            std::cerr << "Warning: No successors found for block "
                      << BB->getName() << " in function " << function.getName()
                      << std::endl;
            continue;
        }

        // Convert conditional branch to unconditional branch
        auto* BrInst = dyn_cast<BranchInst>(Info.Terminator);
        if (BrInst && BrInst->isConditional()) {
            // Create new unconditional branch to preferred successor
            auto* NewBr = BranchInst::Create(PreferredSucc->BB, BB);

            // Replace the old terminator
            Info.Terminator->replaceAllUsesWith(NewBr);
            Info.Terminator->eraseFromParent();

            // Update block info
            Info.Terminator = NewBr;
            Info.UnconditionalBranch = true;
            InstInfo_[NewBr].Block = &Info;
            InstInfo_[Info.Terminator].Live = true;

            // Mark the new branch as live

            markLive(NewBr);

            changed = true;
        }
    }

    if (changed) {
        // Clear dead terminators since we've processed them
        BlocksWithDeadTerminators_.clear();

        // Mark that we've updated the CFG structure
        updatedCFG = true;

        // Invalidate predecessor caches since we've changed the CFG
        for (auto& BB : function) {
            BB->invalidatePredecessorCache();
        }
    }
}

bool ADCEPass::removeDeadInstructions(Function& function) {
    // Updates control and dataflow around dead blocks
    updateDeadRegions(function);

    Worklist_.clear();

    for (auto& BB : function) {
        for (auto* inst : *BB) {
            // Check if the instruction is alive.
            if (isLive(inst)) {
                continue;
            }

            // Prepare to delete.
            Worklist_.push_back(inst);
            inst->dropAllReferences();
        }
    }

    // Then erase the instructions
    for (auto* inst : Worklist_) {
        inst->eraseFromParent();
    }

    return !Worklist_.empty();
}

REGISTER_PASS(ADCEPass, "adce")

}  // namespace midend