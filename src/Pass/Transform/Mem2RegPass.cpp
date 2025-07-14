#include "Pass/Transform/Mem2RegPass.h"

#include <iostream>
#include <string>

#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Support/Casting.h"

namespace midend {

bool Mem2RegContext::runOnFunction(Function& function, AnalysisManager& am) {
    if (function.empty()) return false;

    dominanceInfo_ =
        am.getAnalysis<DominanceInfo>("DominanceAnalysis", function);
    if (!dominanceInfo_) {
        std::cerr << "Warning: DominanceInfo not available for Mem2RegPass. "
                     "Skipping function: "
                  << function.getName() << std::endl;
        return false;
    }

    collectPromotableAllocas(function);

    if (promotableAllocas_.empty()) return false;

    insertPhiNodes(function);
    performSSAConstruction(function);
    cleanupInstructions();

    return true;
}

bool Mem2RegContext::isPromotable(AllocaInst* alloca) {
    if (alloca->isArrayAllocation()) {
        return false;
    }

    for (auto* use : alloca->users()) {
        auto* user = use->getUser();
        if (dyn_cast<LoadInst>(user)) {
            continue;
        } else if (auto* store = dyn_cast<StoreInst>(user)) {
            if (store->getValueOperand() == alloca) {
                return false;
            }
            continue;
        } else {
            return false;
        }
    }

    return true;
}

void Mem2RegContext::collectPromotableAllocas(Function& function) {
    auto& entryBlock = function.front();
    for (auto it = entryBlock.begin(); it != entryBlock.end(); ++it) {
        if (auto* alloca = dyn_cast<AllocaInst>(*it)) {
            if (isPromotable(alloca)) {
                promotableAllocas_.insert(alloca);
            }
        }
    }
}

void Mem2RegContext::insertPhiNodes(Function&) {
    std::unordered_set<BasicBlock*> phiInserted;
    std::unordered_set<BasicBlock*> workList;
    int phiCount = 0;

    for (auto* alloca : promotableAllocas_) {
        phiInserted.clear();
        workList.clear();

        for (auto* use : alloca->users()) {
            auto* user = use->getUser();
            if (auto* store = dyn_cast<StoreInst>(user)) {
                workList.insert(store->getParent());
            }
        }

        while (!workList.empty()) {
            auto it = workList.begin();
            BasicBlock* block = *it;
            workList.erase(it);
            auto frontierBlocks = dominanceInfo_->getDominanceFrontier(block);

            for (auto* frontierBlock : frontierBlocks) {
                if (phiInserted.find(frontierBlock) == phiInserted.end()) {
                    auto* phi = PHINode::Create(alloca->getAllocatedType(),
                                                alloca->getName() + ".phi." +
                                                    std::to_string(++phiCount));

                    auto it = frontierBlock->begin();
                    while (it != frontierBlock->end() && isa<PHINode>(*it)) {
                        ++it;
                    }

                    if (it != frontierBlock->end()) {
                        phi->insertBefore(*it);
                    } else {
                        frontierBlock->push_back(phi);
                    }

                    phiInserted.insert(frontierBlock);
                    workList.insert(frontierBlock);
                    phiToAlloca_[phi] = alloca;
                }
            }
        }
    }
}

void Mem2RegContext::performSSAConstruction(Function& function) {
    std::unordered_map<AllocaInst*, Value*> currentValues;

    for (auto* alloca : promotableAllocas_) {
        currentValues[alloca] = UndefValue::get(alloca->getAllocatedType());
    }

    renameVariables(&function.front(), currentValues);
}

void Mem2RegContext::renameVariables(
    BasicBlock* block, std::unordered_map<AllocaInst*, Value*>& currentValues) {
    for (auto it = block->begin(); it != block->end() && isa<PHINode>(*it);
         ++it) {
        auto* phi = cast<PHINode>(*it);
        auto itAlloca = phiToAlloca_.find(phi);
        if (itAlloca != phiToAlloca_.end()) {
            AllocaInst* alloca = itAlloca->second;
            currentValues[alloca] = phi;
        }
    }

    if (visitedBlocks_.find(block) != visitedBlocks_.end()) {
        return;
    }
    visitedBlocks_.insert(block);

    for (auto it = block->begin(); it != block->end(); ++it) {
        if (auto* load = dyn_cast<LoadInst>(*it)) {
            if (auto* alloca =
                    dyn_cast<AllocaInst>(load->getPointerOperand())) {
                if (promotableAllocas_.find(alloca) !=
                    promotableAllocas_.end()) {
                    auto itValue = currentValues.find(alloca);
                    assert(itValue != currentValues.end() &&
                           "Alloca should have a current value");
                    load->replaceAllUsesWith(itValue->second);
                    instructionsToRemove_.push_back(load);
                }
            }
        } else if (auto* store = dyn_cast<StoreInst>(*it)) {
            if (auto* alloca =
                    dyn_cast<AllocaInst>(store->getPointerOperand())) {
                if (promotableAllocas_.find(alloca) !=
                    promotableAllocas_.end()) {
                    currentValues[alloca] = store->getValueOperand();
                    instructionsToRemove_.push_back(store);
                }
            }
        } else if (auto* branch = dyn_cast<BranchInst>(*it)) {
            for (unsigned i = 0; i < branch->getNumSuccessors(); ++i) {
                BasicBlock* successor = branch->getSuccessor(i);
                auto savedValues = currentValues;
                renameVariables(successor, currentValues);
                currentValues = savedValues;
            }
        }
    }
}

void Mem2RegContext::cleanupInstructions() {
    for (auto* inst : instructionsToRemove_) {
        inst->eraseFromParent();
    }

    for (auto* alloca : promotableAllocas_) {
        alloca->eraseFromParent();
    }
}

}  // namespace midend