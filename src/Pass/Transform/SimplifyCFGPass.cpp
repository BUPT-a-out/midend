#include "Pass/Transform/SimplifyCFGPass.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <unordered_set>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRPrinter.h"
#include "IR/Instruction.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Support/Casting.h"

namespace midend {

bool isUnconditional(const BranchInst* br) {
    return br->isUnconditional() || br->isConstantUnconditional() ||
           br->isSameTarget();
}

bool SimplifyCFGPass::convertConstantConditionalBranches(Function& function) {
    bool changed = false;

    for (auto* block : function) {
        auto* terminator = block->getTerminator();
        if (!terminator) continue;

        auto* branch = dyn_cast<BranchInst>(terminator);
        if (!branch || !branch->isConstantUnconditional()) continue;

        auto* constantCond = cast<ConstantInt>(branch->getCondition());
        BasicBlock* targetBB =
            constantCond->isZero() ? branch->getFalseBB() : branch->getTrueBB();
        BasicBlock* deadBB =
            constantCond->isZero() ? branch->getTrueBB() : branch->getFalseBB();

        // Remove PHI incoming values from the dead edge
        if (deadBB) {
            for (auto& inst : *deadBB) {
                if (auto* phi = dyn_cast<PHINode>(inst)) {
                    phi->deleteIncoming(block);
                } else {
                    break;
                }
            }
        }

        BranchInst* newBranch = BranchInst::Create(targetBB);

        branch->replaceAllUsesWith(newBranch);
        newBranch->insertBefore(branch);
        branch->eraseFromParent();

        targetBB->invalidatePredecessorCache();
        if (deadBB) {
            deadBB->invalidatePredecessorCache();
        }

        changed = true;
    }

    return changed;
}

bool SimplifyCFGPass::runOnFunction(Function& function, AnalysisManager&) {
    if (function.isDeclaration()) {
        return false;
    }

    init();

    bool overallChanged = false;
    bool iterChanged = true;

    while (iterChanged) {
        iterChanged = convertConstantConditionalBranches(function);
        iterChanged |= removeUnreachableBlocks(function);
        iterChanged |= mergeBlocks(function);
        iterChanged |= eliminateEmptyBlocks(function);
        iterChanged |= removeDuplicatePHINodes(function);
        overallChanged |= iterChanged;
    }

    return overallChanged;
}

bool SimplifyCFGPass::removeUnreachableBlocks(Function& function) {
    auto unreachableBlocks = findUnreachableBlocks(function);

    if (unreachableBlocks.empty()) {
        return false;
    }

    for (auto* block : unreachableBlocks) {
        while (!block->empty()) {
            auto& inst = block->back();
            inst.dropAllReferences();
            inst.eraseFromParent();
        }
        block->replaceAllUsesBy([&](Use*& use) {
            auto user = use->getUser();
            if (auto phi = dyn_cast<PHINode>(user)) {
                phi->deleteIncoming(block);
            } else if (auto br = dyn_cast<BranchInst>(user)) {
                if (unreachableBlocks.find(br->getParent()) !=
                    unreachableBlocks.end()) {
                    use->set(nullptr);
                }
            } else {
                std::cerr << "Warning: Unreachable block " << block->getName()
                          << " has users (" << user->getName()
                          << ") that are not PHI nodes. "
                             "This may lead to undefined behavior."
                          << std::endl
                          << "DEBUG:\n"
                          << IRPrinter::toString(&function) << std::endl;
            }
        });
        block->eraseFromParent();
    }
    return true;
}

std::unordered_set<BasicBlock*> SimplifyCFGPass::findUnreachableBlocks(
    Function& function) {
    std::unordered_set<BasicBlock*> unreachableBlocks;
    std::unordered_set<BasicBlock*> reachableBlocks;
    std::queue<BasicBlock*> worklist;

    BasicBlock* entryBlock = &function.getEntryBlock();
    worklist.push(entryBlock);
    reachableBlocks.insert(entryBlock);

    while (!worklist.empty()) {
        BasicBlock* current = worklist.front();
        worklist.pop();

        auto successors = current->getSuccessors();
        for (auto* successor : successors) {
            if (reachableBlocks.insert(successor).second) {
                worklist.push(successor);
            }
        }
    }

    for (auto& block : function) {
        if (reachableBlocks.find(block) == reachableBlocks.end()) {
            unreachableBlocks.insert(block);
        }
    }
    return unreachableBlocks;
}

bool removeUnreachablePhiIncomingEdges(Function& func) {
    bool changed = false;

    for (auto* block : func) {
        for (auto& inst : *block) {
            if (auto* phi = dyn_cast<PHINode>(inst)) {
                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                    auto incoming = phi->getIncomingBlock(i);
                    auto succ = incoming->getSuccessors();
                    if (std::find(succ.begin(), succ.end(), block) ==
                        succ.end()) {
                        phi->deleteIncoming(incoming);
                        changed = true;
                    }
                }
            }
        }
    }

    return changed;
}

bool SimplifyCFGPass::mergeBlocks(Function& function) {
    bool changed = false;
    std::vector<BasicBlock*> blocksToProcess;

    for (auto* block : function) {
        if (canMergeWithPredecessor(block)) {
            blocksToProcess.push_back(block);
        }
    }

    for (auto* block : blocksToProcess) {
        mergeBlockIntoPredecessor(block);
        changed |= removeUnreachablePhiIncomingEdges(function);
        changed = true;
    }

    return changed;
}

bool SimplifyCFGPass::canMergeWithPredecessor(BasicBlock* block) {
    // Skip entry block
    if (block == &block->getParent()->getEntryBlock()) {
        return false;
    }

    auto predecessors = block->getPredecessors();

    // Block must have exactly one predecessor
    if (predecessors.size() != 1) {
        return false;
    }

    BasicBlock* pred = predecessors[0];
    auto predSuccessors = pred->getSuccessors();

    // Predecessor must have exactly one successor
    if (predSuccessors.size() != 1) {
        return false;
    }

    // The successor must be this block
    if (predSuccessors[0] != block) {
        return false;
    }

    return true;
}

void SimplifyCFGPass::mergeBlockIntoPredecessor(BasicBlock* block) {
    BasicBlock* pred = block->getPredecessors()[0];

    // Remove the terminator from predecessor
    if (auto* term = pred->getTerminator()) {
        term->eraseFromParent();
    }

    // Move all instructions from block to predecessor
    while (!block->empty()) {
        auto& inst = block->front();

        // Check if this is a PHI node with only one incoming value
        if (auto* phi = dyn_cast<PHINode>(&inst)) {
            if (phi->getNumIncomingValues() == 1) {
                // Replace the PHI with its single incoming value
                Value* incomingValue = phi->getIncomingValue(0);
                phi->replaceAllUsesWith(incomingValue);
                phi->eraseFromParent();
                continue;
            } else {
                std::cerr << "Warning: PHI node " << phi->getName()
                          << " (in function " << block->getParent()->getName()
                          << ") has multiple incoming values and cannot be "
                             "simplified."
                          << std::endl;
            }
        }

        auto parent = inst.getParent();
        inst.removeFromParent();
        parent->invalidatePredecessorCache();
        pred->push_back(&inst);
    }

    std::vector<Use*> users;
    for (auto* use : block->users()) {
        users.push_back(use);
    }

    for (auto* use : users) {
        auto* user = use->getUser();
        if (auto* phi = dyn_cast<PHINode>(user)) {
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                if (phi->getIncomingBlock(i) == block) {
                    phi->setIncomingBlock(i, pred);
                }
            }
        }
    }

    block->eraseFromParent();
}

bool SimplifyCFGPass::eliminateEmptyBlocks(Function& function) {
    bool changed = false;

    bool iterChanged = true;
    while (iterChanged) {
        iterChanged = false;
        std::vector<BasicBlock*> emptyBlocks;

        for (auto* block : function) {
            if (canBeEliminate(block)) {
                emptyBlocks.push_back(block);
            }
        }

        for (auto* block : emptyBlocks) {
            if (block->getParent() == nullptr) {
                continue;
            }

            // Get the target of the empty block's branch
            auto* emptyBlockBranch =
                dyn_cast<BranchInst>(block->getTerminator());
            if (!emptyBlockBranch) {
                continue;
            }
            auto* target = emptyBlockBranch->getTargetBB();
            if (!target) {
                continue;
            }

            auto successors = block->getSuccessors();
            auto predecessors = block->getPredecessors();

            // Handle only simple cases to avoid complexity issues
            if (predecessors.size() != 1) {
                continue;  // Skip complex cases with multiple predecessors
            }

            // Collect all uses before modifying to avoid iterator invalidation
            std::vector<Use*> blockUses;
            for (auto* use : block->users()) {
                blockUses.push_back(use);
            }

            // Update each use safely
            for (auto* use : blockUses) {
                auto* user = use->getUser();
                if (auto* phi = dyn_cast<PHINode>(user)) {
                    // Simple case: just update the incoming block reference
                    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                        if (phi->getIncomingBlock(i) == block) {
                            phi->setIncomingBlock(i, predecessors[0]);
                            break;
                        }
                    }
                } else if (auto* branch = dyn_cast<BranchInst>(user)) {
                    // Update branch targets
                    for (unsigned i = 0; i < branch->getNumOperands(); ++i) {
                        if (branch->getOperand(i) == block) {
                            branch->setOperand(i, target);
                        }
                    }
                }
            }

            block->eraseFromParent();
            for (auto* succ : successors) {
                succ->invalidatePredecessorCache();
            }
            iterChanged = true;
            changed = true;
        }
    }

    return changed;
}

bool SimplifyCFGPass::canBeEliminate(BasicBlock* block) {
    if (block->size() != 1) {
        return false;
    }

    auto* inst = &block->front();
    if (!isa<BranchInst>(inst)) {
        return false;
    }

    auto* branch = cast<BranchInst>(inst);
    if (!isUnconditional(branch)) {
        return false;
    }

    for (auto use : block->users()) {
        auto phi = dyn_cast<PHINode>(use->getUser());
        if (phi && !phi->isTrival()) {
            return false;
        }
    }

    return true;
}

bool SimplifyCFGPass::removeDuplicatePHINodes(Function& function) {
    bool changed = false;

    for (auto* block : function) {
        std::vector<PHINode*> phiNodes;
        std::vector<PHINode*> toRemove;

        for (auto& inst : *block) {
            if (auto* phi = dyn_cast<PHINode>(inst)) {
                phiNodes.push_back(phi);
            } else {
                break;
            }
        }

        for (size_t i = 0; i < phiNodes.size(); ++i) {
            if (std::find(toRemove.begin(), toRemove.end(), phiNodes[i]) !=
                toRemove.end()) {
                continue;
            }

            for (size_t j = i + 1; j < phiNodes.size(); ++j) {
                if (std::find(toRemove.begin(), toRemove.end(), phiNodes[j]) !=
                    toRemove.end()) {
                    continue;
                }

                if (arePHINodesIdentical(phiNodes[i], phiNodes[j])) {
                    phiNodes[j]->replaceAllUsesWith(phiNodes[i]);
                    toRemove.push_back(phiNodes[j]);
                    changed = true;
                }
            }
        }

        for (auto* phi : toRemove) {
            phi->dropAllReferences();
            phi->eraseFromParent();
        }
    }

    return changed;
}

bool SimplifyCFGPass::arePHINodesIdentical(PHINode* phi1, PHINode* phi2) {
    if (phi1->getType() != phi2->getType() ||
        phi1->getNumIncomingValues() != phi2->getNumIncomingValues()) {
        return false;
    }

    for (unsigned i = 0; i < phi1->getNumIncomingValues(); ++i) {
        if (phi1->getIncomingBlock(i) != phi2->getIncomingBlock(i) ||
            phi1->getIncomingValue(i) != phi2->getIncomingValue(i)) {
            return false;
        }
    }

    return true;
}

}  // namespace midend