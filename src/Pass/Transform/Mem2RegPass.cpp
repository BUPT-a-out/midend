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
    std::vector<std::unordered_map<AllocaInst*, Value*>> valueStack;

    std::unordered_map<AllocaInst*, Value*> initialValues;
    for (auto* alloca : promotableAllocas_) {
        initialValues[alloca] = UndefValue::get(alloca->getAllocatedType());
    }
    valueStack.push_back(initialValues);

    renameVariables(&function.front(), valueStack);
}

Value* Mem2RegContext::decideVariableValue(
    AllocaInst* alloca, const Mem2RegContext::ValueStack& valueStack) {
    for (auto it = valueStack.rbegin(); it != valueStack.rend(); ++it) {
        const auto& currentMap = *it;
        auto found = currentMap.find(alloca);
        if (found != currentMap.end()) {
            return found->second;
        }
    }
    std::cerr << "Warning: No value found for alloca " << alloca->getName()
              << ". Returning undef." << std::endl;

    return UndefValue::get(alloca->getAllocatedType());
}

void Mem2RegContext::renameVariables(
    BasicBlock* block,
    std::vector<std::unordered_map<AllocaInst*, Value*>>& valueStack) {
    valueStack.push_back({});

    for (auto it = block->begin(); it != block->end() && isa<PHINode>(*it);
         ++it) {
        auto* phi = cast<PHINode>(*it);
        auto itAlloca = phiToAlloca_.find(phi);
        if (itAlloca != phiToAlloca_.end()) {
            AllocaInst* alloca = itAlloca->second;
            valueStack.back()[alloca] = phi;
        }
    }

    if (visitedBlocks_.find(block) != visitedBlocks_.end()) {
        valueStack.pop_back();
        return;
    }
    visitedBlocks_.insert(block);

    for (auto it = block->begin(); it != block->end(); ++it) {
        if (auto* load = dyn_cast<LoadInst>(*it)) {
            if (auto* alloca =
                    dyn_cast<AllocaInst>(load->getPointerOperand())) {
                if (promotableAllocas_.find(alloca) !=
                    promotableAllocas_.end()) {
                    load->replaceAllUsesWith(
                        decideVariableValue(alloca, valueStack));
                    instructionsToRemove_.push_back(load);
                }
            }
        } else if (auto* store = dyn_cast<StoreInst>(*it)) {
            if (auto* alloca =
                    dyn_cast<AllocaInst>(store->getPointerOperand())) {
                if (promotableAllocas_.find(alloca) !=
                    promotableAllocas_.end()) {
                    valueStack.back()[alloca] = store->getValueOperand();
                    instructionsToRemove_.push_back(store);
                }
            }
        } else if (auto* branch = dyn_cast<BranchInst>(*it)) {
            for (unsigned i = 0; i < branch->getNumSuccessors(); ++i) {
                BasicBlock* successor = branch->getSuccessor(i);

                // Set incoming values for phi nodes in successor
                for (auto it = successor->begin();
                     it != successor->end() && isa<PHINode>(*it); ++it) {
                    auto* phi = cast<PHINode>(*it);
                    auto itAlloca = phiToAlloca_.find(phi);
                    if (itAlloca != phiToAlloca_.end()) {
                        AllocaInst* alloca = itAlloca->second;
                        auto value = decideVariableValue(alloca, valueStack);
                        phi->addIncoming(value, block);
                    }
                }

                renameVariables(successor, valueStack);
            }
        }
    }

    valueStack.pop_back();
}

void Mem2RegContext::cleanupInstructions() {
    for (auto* inst : instructionsToRemove_) {
        inst->eraseFromParent();
    }

    for (auto* alloca : promotableAllocas_) {
        alloca->eraseFromParent();
    }
}

REGISTER_PASS(Mem2RegPass, "mem2reg")

}  // namespace midend