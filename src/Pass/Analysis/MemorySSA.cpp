#include "Pass/Analysis/MemorySSA.h"

#include <algorithm>
#include <iostream>
#include <queue>

#include "IR/Function.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/DominanceInfo.h"

namespace midend {

// Static member initialization
unsigned MemoryAccess::nextId_ = 0;

//===----------------------------------------------------------------------===//
// MemoryAccess implementations
//===----------------------------------------------------------------------===//

void MemoryUse::print() const {
    std::cout << "MemoryUse(" << getID() << ") = ";
    if (getDefiningAccess()) {
        std::cout << getDefiningAccess()->getID();
    } else {
        std::cout << "null";
    }
    std::cout << " ; ";
    if (memoryInst_) {
        std::cout << memoryInst_->getName();
    } else {
        std::cout << "null";
    }
    std::cout << std::endl;
}

void MemoryDef::print() const {
    std::cout << "MemoryDef(" << getID() << ") = ";
    if (getDefiningAccess()) {
        std::cout << getDefiningAccess()->getID();
    } else {
        std::cout << "null";
    }
    std::cout << " ; ";
    if (memoryInst_) {
        std::cout << memoryInst_->getName();
    } else {
        std::cout << "null";
    }
    std::cout << std::endl;
}

void MemoryPhi::print() const {
    std::cout << "MemoryPhi(" << getID() << ") = phi(";
    for (size_t i = 0; i < operands_.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << operands_[i].first->getID() << ":"
                  << operands_[i].second->getName();
    }
    std::cout << ")" << std::endl;
}

//===----------------------------------------------------------------------===//
// CachingWalker implementation
//===----------------------------------------------------------------------===//

MemoryAccess* CachingWalker::getClobberingMemoryAccess(MemoryAccess* ma) {
    auto it = walkCache_.find(ma);
    if (it != walkCache_.end()) {
        return it->second;
    }

    MemoryAccess* result = nullptr;

    if (auto* memUse = dyn_cast<MemoryUse>(ma)) {
        result = walkToNextClobber(memUse->getDefiningAccess(),
                                   memUse->getMemoryInst());
    } else if (auto* memDef = dyn_cast<MemoryDef>(ma)) {
        result = walkToNextClobber(memDef->getDefiningAccess(),
                                   memDef->getMemoryInst());
    } else if (auto* memPhi = dyn_cast<MemoryPhi>(ma)) {
        // For phi nodes, the clobbering access is the phi itself
        result = ma;
    }

    walkCache_[ma] = result;
    return result;
}

MemoryAccess* CachingWalker::getClobberingMemoryAccess(
    Instruction* inst, MemoryAccess* startAccess) {
    return walkToNextClobber(startAccess, inst);
}

MemoryAccess* CachingWalker::walkToNextClobber(MemoryAccess* start,
                                               Instruction* inst) {
    if (!start || !inst) {
        return start;
    }

    // Track visited accesses to prevent infinite loops
    std::unordered_set<MemoryAccess*> visited;
    MemoryAccess* current = start;

    while (current && visited.find(current) == visited.end()) {
        visited.insert(current);

        if (clobbers(current, inst)) {
            return current;
        }

        // Move to the next access in the chain
        if (auto* memDef = dyn_cast<MemoryDef>(current)) {
            current = memDef->getDefiningAccess();
        } else if (auto* memUse = dyn_cast<MemoryUse>(current)) {
            current = memUse->getDefiningAccess();
        } else if (auto* memPhi = dyn_cast<MemoryPhi>(current)) {
            // For phi nodes, we need to check all incoming values
            // For simplicity, return the phi as the clobber
            return current;
        } else {
            break;
        }
    }

    return current;
}

bool CachingWalker::clobbers(MemoryAccess* access, Instruction* inst) {
    if (!access || !inst) {
        return false;
    }

    // Memory defs always clobber (conservatively)
    if (auto* memDef = dyn_cast<MemoryDef>(access)) {
        Instruction* defInst = memDef->getMemoryInst();

        // If defInst is null (e.g., live-on-entry), conservatively clobber
        if (!defInst) {
            return true;
        }

        // If they're the same instruction, it's the defining access
        if (defInst == inst) {
            return true;
        }

        // Use alias analysis to check if they may alias
        if (aliasAnalysis_) {
            // Alloca instructions clobber any memory operation using their
            // result
            if (auto* alloca = dyn_cast<AllocaInst>(defInst)) {
                if (auto* load = dyn_cast<LoadInst>(inst)) {
                    auto aliasResult =
                        const_cast<AliasAnalysis::Result*>(aliasAnalysis_)
                            ->alias(alloca, load->getPointerOperand());
                    return aliasResult != AliasAnalysis::AliasResult::NoAlias;
                } else if (auto* store = dyn_cast<StoreInst>(inst)) {
                    auto aliasResult =
                        const_cast<AliasAnalysis::Result*>(aliasAnalysis_)
                            ->alias(alloca, store->getPointerOperand());
                    return aliasResult != AliasAnalysis::AliasResult::NoAlias;
                }
            }

            // For stores and loads, check if they may alias
            if (auto* store = dyn_cast<StoreInst>(defInst)) {
                if (auto* load = dyn_cast<LoadInst>(inst)) {
                    auto aliasResult =
                        const_cast<AliasAnalysis::Result*>(aliasAnalysis_)
                            ->alias(store->getPointerOperand(),
                                    load->getPointerOperand());
                    return aliasResult != AliasAnalysis::AliasResult::NoAlias;
                } else if (auto* storeInst = dyn_cast<StoreInst>(inst)) {
                    auto aliasResult =
                        const_cast<AliasAnalysis::Result*>(aliasAnalysis_)
                            ->alias(store->getPointerOperand(),
                                    storeInst->getPointerOperand());
                    return aliasResult != AliasAnalysis::AliasResult::NoAlias;
                } else if (isa<CallInst>(inst)) {
                    // Stores clobber function calls (conservative)
                    return true;
                }
            }

            // Other memory definitions also clobber function calls
            if (isa<CallInst>(inst) &&
                (isa<AllocaInst>(defInst) || isa<StoreInst>(defInst))) {
                return true;
            }

            // Function calls conservatively clobber everything
            if (isa<CallInst>(defInst)) {
                return true;
            }
        } else {
            // Without alias analysis, be conservative
            return true;
        }
    }

    return false;
}

void CachingWalker::invalidateCache() { walkCache_.clear(); }

//===----------------------------------------------------------------------===//
// MemorySSA implementation
//===----------------------------------------------------------------------===//

MemorySSA::MemorySSA(Function* f, const DominanceInfo* di,
                     const AliasAnalysis::Result* aa)
    : function_(f), domInfo_(di), aliasAnalysis_(aa) {
    // Create the live-on-entry def
    liveOnEntry_ =
        std::make_unique<MemoryDef>(nullptr, nullptr, &f->getEntryBlock());

    // Create the walker
    walker_ = std::make_unique<CachingWalker>(this, di, aa);

    // Build the Memory SSA form
    buildMemorySSA();
}

MemorySSA::~MemorySSA() = default;

MemoryAccess* MemorySSA::getMemoryAccess(Instruction* inst) const {
    auto it = memoryAccesses_.find(inst);
    return it != memoryAccesses_.end() ? it->second : nullptr;
}

MemoryPhi* MemorySSA::getMemoryPhi(BasicBlock* block) const {
    auto it = memoryPhis_.find(block);
    return it != memoryPhis_.end() ? it->second : nullptr;
}

MemoryAccess* MemorySSA::getClobberingMemoryAccess(MemoryAccess* ma) {
    return walker_->getClobberingMemoryAccess(ma);
}

MemoryAccess* MemorySSA::getClobberingMemoryAccess(Instruction* inst) {
    MemoryAccess* startAccess = getMemoryAccess(inst);
    if (!startAccess) {
        return nullptr;
    }
    // Should behave the same as getClobberingMemoryAccess(startAccess)
    return getClobberingMemoryAccess(startAccess);
}

void MemorySSA::buildMemorySSA() {
    // Phase 1: Insert Memory Phi nodes where needed
    insertMemoryPhis();

    // Phase 2: Rename memory accesses to establish def-use chains
    renameMemoryAccesses();
}

void MemorySSA::insertMemoryPhis() {
    findPhiBlocks();
    insertPhiNodes();
}

void MemorySSA::findPhiBlocks() {
    // Find blocks that define memory (have memory-writing instructions)
    std::unordered_set<BasicBlock*> defBlocks;

    for (auto* block : *function_) {
        for (auto* inst : *block) {
            if (definesMemory(inst)) {
                defBlocks.insert(block);
                break;
            }
        }
    }

    // Calculate dominance frontier and insert phi blocks
    // Using a worklist algorithm similar to SSA construction
    std::queue<BasicBlock*> worklist;
    std::unordered_set<BasicBlock*> processed;

    for (BasicBlock* defBlock : defBlocks) {
        worklist.push(defBlock);
    }

    while (!worklist.empty()) {
        BasicBlock* block = worklist.front();
        worklist.pop();

        if (processed.count(block)) {
            continue;
        }
        processed.insert(block);

        // Get dominance frontier of this block
        const auto& domFrontier = domInfo_->getDominanceFrontier(block);

        for (BasicBlock* frontierBlock : domFrontier) {
            if (phiBlocks_.count(frontierBlock)) {
                continue;
            }

            // Check if this block has multiple predecessors
            const auto& preds = frontierBlock->getPredecessors();
            if (preds.size() > 1) {
                phiBlocks_.insert(frontierBlock);
                worklist.push(frontierBlock);
            }
        }
    }
}

void MemorySSA::insertPhiNodes() {
    for (BasicBlock* block : phiBlocks_) {
        createMemoryPhi(block);
    }
}

void MemorySSA::renameMemoryAccesses() {
    // Initialize with live-on-entry
    blockLastDef_[&function_->getEntryBlock()] = liveOnEntry_.get();

    // First pass: Create memory accesses for all instructions
    // Use a worklist to process blocks in the right order
    std::queue<BasicBlock*> worklist;
    std::unordered_set<BasicBlock*> visited;

    worklist.push(&function_->getEntryBlock());

    while (!worklist.empty()) {
        BasicBlock* block = worklist.front();
        worklist.pop();

        if (visited.count(block)) {
            continue;
        }
        visited.insert(block);

        // Get the current memory state for this block
        MemoryAccess* currentDef = blockLastDef_[block];
        if (!currentDef) {
            // Find the defining access from predecessors
            const auto& preds = block->getPredecessors();
            if (preds.size() == 1) {
                auto predIt = blockLastDef_.find(preds[0]);
                if (predIt != blockLastDef_.end()) {
                    currentDef = predIt->second;
                }
            } else if (preds.size() > 1) {
                // Multiple predecessors - will be handled by phi nodes
                // For now, use live-on-entry and let phi filling fix this
                currentDef = liveOnEntry_.get();
            }

            if (!currentDef) {
                currentDef = liveOnEntry_.get();
            }
            blockLastDef_[block] = currentDef;
        }

        // Process instructions in this block
        for (auto* inst : *block) {
            if (isMemoryOperation(inst)) {
                if (definesMemory(inst)) {
                    // Create a MemoryDef
                    MemoryDef* memDef = createMemoryDef(inst, currentDef);
                    currentDef = memDef;
                    blockLastDef_[block] = currentDef;
                } else if (usesMemory(inst)) {
                    // Create a MemoryUse
                    createMemoryUse(inst, currentDef);
                }
            }
        }

        // Add successors to worklist
        for (BasicBlock* succ : block->getSuccessors()) {
            if (!visited.count(succ)) {
                // Set initial def for successor if not set
                if (blockLastDef_.find(succ) == blockLastDef_.end()) {
                    blockLastDef_[succ] = currentDef;
                }
                worklist.push(succ);
            }
        }
    }

    // Second pass: Fill in phi node operands
    for (const auto& [block, phi] : memoryPhis_) {
        const auto& preds = block->getPredecessors();
        for (BasicBlock* pred : preds) {
            auto it = blockLastDef_.find(pred);
            MemoryAccess* predDef =
                (it != blockLastDef_.end()) ? it->second : liveOnEntry_.get();
            phi->addIncoming(predDef, pred);
        }
    }

    // Third pass: Fix uses in blocks with memory phis and their successors
    for (const auto& [block, phi] : memoryPhis_) {
        // Update all memory uses in this block to use the phi as their defining
        // access
        MemoryAccess* currentDef = phi;
        for (auto* inst : *block) {
            if (auto* memAccess = getMemoryAccess(inst)) {
                if (auto* memUse = dyn_cast<MemoryUse>(memAccess)) {
                    // Memory uses in phi blocks should use the phi
                    memUse->setDefiningAccess(currentDef);
                } else if (auto* memDef = dyn_cast<MemoryDef>(memAccess)) {
                    // Update currentDef for subsequent uses
                    currentDef = memDef;
                }
            }
        }

        // Update blockLastDef to reflect the phi
        if (currentDef == phi) {
            blockLastDef_[block] = phi;
        } else {
            blockLastDef_[block] = currentDef;
        }
    }

    // Fourth pass: Propagate memory state from phi blocks to successors
    for (const auto& [block, phi] : memoryPhis_) {
        MemoryAccess* finalDef = blockLastDef_[block];
        for (BasicBlock* succ : block->getSuccessors()) {
            const auto& preds = succ->getPredecessors();
            if (preds.size() == 1 && preds[0] == block) {
                // Single predecessor that has a phi - update successor's uses
                for (auto* inst : *succ) {
                    if (auto* memAccess = getMemoryAccess(inst)) {
                        if (auto* memUse = dyn_cast<MemoryUse>(memAccess)) {
                            // Check if this use doesn't have a defining def in
                            // the same block
                            bool hasDefInBlock = false;
                            for (auto* prevInst : *succ) {
                                if (prevInst == inst) break;
                                if (auto* prevAccess =
                                        getMemoryAccess(prevInst)) {
                                    if (isa<MemoryDef>(prevAccess)) {
                                        hasDefInBlock = true;
                                        break;
                                    }
                                }
                            }
                            if (!hasDefInBlock) {
                                memUse->setDefiningAccess(finalDef);
                            }
                        }
                    }
                }
            }
        }
    }
}

MemoryUse* MemorySSA::createMemoryUse(Instruction* inst,
                                      MemoryAccess* definingAccess) {
    auto memUse =
        std::make_unique<MemoryUse>(inst, definingAccess, inst->getParent());
    MemoryUse* result = memUse.get();
    memoryAccesses_[inst] = result;
    accessStorage_.push_back(std::move(memUse));
    return result;
}

MemoryDef* MemorySSA::createMemoryDef(Instruction* inst,
                                      MemoryAccess* definingAccess) {
    auto memDef =
        std::make_unique<MemoryDef>(inst, definingAccess, inst->getParent());
    MemoryDef* result = memDef.get();
    memoryAccesses_[inst] = result;
    accessStorage_.push_back(std::move(memDef));
    return result;
}

MemoryPhi* MemorySSA::createMemoryPhi(BasicBlock* block) {
    auto memPhi = std::make_unique<MemoryPhi>(block);
    MemoryPhi* result = memPhi.get();
    memoryPhis_[block] = result;
    accessStorage_.push_back(std::move(memPhi));
    return result;
}

bool MemorySSA::isMemoryOperation(Instruction* inst) const {
    return isa<LoadInst>(inst) || isa<StoreInst>(inst) || isa<CallInst>(inst) ||
           isa<AllocaInst>(inst);
}

bool MemorySSA::definesMemory(Instruction* inst) const {
    return isa<StoreInst>(inst) || isa<CallInst>(inst) || isa<AllocaInst>(inst);
}

bool MemorySSA::usesMemory(Instruction* inst) const {
    return isa<LoadInst>(inst);
}

void MemorySSA::print() const {
    std::cout << "Memory SSA for function " << function_->getName() << ":\n";

    // Print live on entry
    std::cout << "LiveOnEntry:\n  ";
    liveOnEntry_->print();

    // Print memory phis
    if (!memoryPhis_.empty()) {
        std::cout << "\nMemory Phis:\n";
        for (const auto& [block, phi] : memoryPhis_) {
            std::cout << "  " << block->getName() << ": ";
            phi->print();
        }
    }

    // Print memory accesses by block
    std::cout << "\nMemory Accesses:\n";
    for (auto* block : *function_) {
        bool hasAccesses = false;
        for (auto* inst : *block) {
            MemoryAccess* access = getMemoryAccess(inst);
            if (access) {
                if (!hasAccesses) {
                    std::cout << "  " << block->getName() << ":\n";
                    hasAccesses = true;
                }
                std::cout << "    ";
                access->print();
            }
        }
    }
}

bool MemorySSA::verify() const {
    // Basic verification of Memory SSA invariants

    // 1. Every memory instruction in reachable blocks should have a
    // corresponding memory access
    std::unordered_set<BasicBlock*> reachable;
    std::queue<BasicBlock*> worklist;
    worklist.push(&function_->getEntryBlock());
    reachable.insert(&function_->getEntryBlock());

    while (!worklist.empty()) {
        BasicBlock* block = worklist.front();
        worklist.pop();

        for (BasicBlock* succ : block->getSuccessors()) {
            if (reachable.insert(succ).second) {
                worklist.push(succ);
            }
        }
    }

    for (auto* block : *function_) {
        if (!reachable.count(block)) {
            continue;  // Skip unreachable blocks
        }

        for (auto* inst : *block) {
            if (isMemoryOperation(inst)) {
                if (!getMemoryAccess(inst)) {
                    std::cerr << "Missing memory access for instruction: "
                              << inst->getName() << std::endl;
                    return false;
                }
            }
        }
    }

    // 2. Every memory access should have a valid defining access
    for (const auto& [inst, access] : memoryAccesses_) {
        if (auto* memUse = dyn_cast<MemoryUse>(access)) {
            if (!memUse->getDefiningAccess()) {
                std::cerr << "MemoryUse without defining access" << std::endl;
                return false;
            }
        } else if (auto* memDef = dyn_cast<MemoryDef>(access)) {
            if (!memDef->getDefiningAccess() && memDef != liveOnEntry_.get()) {
                std::cerr << "MemoryDef without defining access" << std::endl;
                return false;
            }
        }
    }

    // 3. Every memory phi should have the correct number of operands
    for (const auto& [block, phi] : memoryPhis_) {
        const auto& preds = block->getPredecessors();
        if (phi->getNumIncomingValues() != preds.size()) {
            std::cerr << "MemoryPhi operand count mismatch" << std::endl;
            return false;
        }
    }

    return true;
}

//===----------------------------------------------------------------------===//
// MemorySSAAnalysis implementation
//===----------------------------------------------------------------------===//

std::unique_ptr<AnalysisResult> MemorySSAAnalysis::runOnFunction(
    Function& f, AnalysisManager& AM) {
    auto* domInfo =
        AM.getAnalysis<DominanceInfo>(DominanceAnalysis::getName(), f);
    auto* aliasAnalysis =
        AM.getAnalysis<AliasAnalysis::Result>(AliasAnalysis::getName(), f);

    return std::make_unique<MemorySSA>(&f, domInfo, aliasAnalysis);
}

MemorySSAAnalysis::Result MemorySSAAnalysis::run(Function& F,
                                                 AnalysisManager& AM) {
    auto* domInfo =
        AM.getAnalysis<DominanceInfo>(DominanceAnalysis::getName(), F);
    auto* aliasAnalysis =
        AM.getAnalysis<AliasAnalysis::Result>(AliasAnalysis::getName(), F);

    return std::make_unique<MemorySSA>(&F, domInfo, aliasAnalysis);
}

}  // namespace midend