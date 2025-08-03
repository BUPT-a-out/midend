#include "Pass/Transform/LICMPass.h"

#include <algorithm>
#include <iostream>
#include <queue>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Analysis/LoopInfo.h"
#include "Support/Casting.h"

namespace midend {

bool LICMPass::runOnFunction(Function& F, AnalysisManager& AM) {
    if (F.isDeclaration() || F.empty()) {
        return false;
    }

    DI = AM.getAnalysis<DominanceInfo>("DominanceAnalysis", F);
    LI = AM.getAnalysis<LoopInfo>("LoopAnalysis", F);
    AA = AM.getAnalysis<AliasAnalysis::Result>("AliasAnalysis", F);
    CG = AM.getAnalysis<CallGraph>("CallGraphAnalysis", *F.getParent());

    if (!DI || !LI || !AA || !CG) {
        return false;
    }
    loopInvariants_.clear();
    hoistedInstructions_.clear();
    instructionToLoop_.clear();

    bool changed = false;
    auto loopsInPostOrder = getLoopsInPostOrder(LI->getTopLevelLoops());

    for (Loop* L : loopsInPostOrder) {
        identifyLoopInvariants(L);
    }

    for (Loop* L : loopsInPostOrder) {
        if (processLoop(L)) {
            changed = true;
        }
    }

    if (hoistToFunctionEntry()) {
        changed = true;
    }

    return changed;
}

bool LICMPass::processLoop(Loop* L) {
    if (L->getBlocks().empty()) {
        return false;
    }

    bool changed = false;

    // First, simplify invariant PHI nodes
    if (simplifyInvariantPHIs(L)) {
        changed = true;
    }

    identifyLoopInvariants(L);

    BasicBlock* preheader = getOrCreatePreheader(L);
    if (!preheader) {
        return changed;
    }

    if (hoistInstructions(L, preheader)) {
        changed = true;
    }

    return changed;
}

void LICMPass::identifyLoopInvariants(Loop* L) {
    std::queue<Instruction*> worklist;
    std::unordered_set<Instruction*> processed;

    bool changed = true;
    while (changed) {
        changed = false;

        for (BasicBlock* BB : L->getBlocks()) {
            for (auto it = BB->begin(); it != BB->end(); ++it) {
                Instruction* I = *it;

                if (processed.find(I) != processed.end()) {
                    continue;
                }

                if (I->isTerminator()) {
                    processed.insert(I);
                    continue;
                }

                bool allOperandsInvariant = true;
                for (unsigned i = 0; i < I->getNumOperands(); ++i) {
                    Value* operand = I->getOperand(i);

                    if (auto* phi = dyn_cast<PHINode>(operand)) {
                        BasicBlock* phiBB = phi->getParent();
                        Loop* phiLoop = LI->getLoopFor(phiBB);
                        if (phiLoop && phiLoop != L && !phiLoop->contains(L)) {
                            allOperandsInvariant = false;
                            break;
                        }
                    }

                    if (!isLoopInvariant(operand, L)) {
                        allOperandsInvariant = false;
                        break;
                    }
                }

                if (allOperandsInvariant && isMemorySafe(I, L) &&
                    !hasSideEffects(I)) {
                    loopInvariants_.insert(I);
                    processed.insert(I);
                    changed = true;

                    Loop* targetLoop = findOutermostInvariantLoop(I, L);
                    instructionToLoop_[I] = targetLoop;
                }
            }
        }
    }
}

bool LICMPass::hoistInstructions(Loop* L, BasicBlock* preheader) {
    bool changed = false;
    std::vector<Instruction*> toHoist;
    for (BasicBlock* BB : L->getBlocks()) {
        for (auto it = BB->begin(); it != BB->end(); ++it) {
            Instruction* I = *it;
            if (loopInvariants_.find(I) != loopInvariants_.end() &&
                canHoistInstruction(I, L)) {
                auto it_target = instructionToLoop_.find(I);
                if (it_target != instructionToLoop_.end() &&
                    it_target->second == L) {
                    toHoist.push_back(I);
                }
            }
        }
    }

    std::vector<Instruction*> additionalToHoist;
    for (Instruction* I : toHoist) {
        for (unsigned i = 0; i < I->getNumOperands(); ++i) {
            Value* operand = I->getOperand(i);
            if (auto* depInst = dyn_cast<Instruction>(operand)) {
                if (loopInvariants_.find(depInst) != loopInvariants_.end() &&
                    L->contains(depInst->getParent()) &&
                    std::find(toHoist.begin(), toHoist.end(), depInst) ==
                        toHoist.end() &&
                    std::find(additionalToHoist.begin(),
                              additionalToHoist.end(),
                              depInst) == additionalToHoist.end() &&
                    canHoistInstruction(depInst, L)) {
                    additionalToHoist.push_back(depInst);
                }
            }
        }
    }

    toHoist.insert(toHoist.end(), additionalToHoist.begin(),
                   additionalToHoist.end());

    std::sort(toHoist.begin(), toHoist.end(),
              [this](Instruction* A, Instruction* B) {
                  return compareInstructionsForHoisting(A, B);
              });
    for (Instruction* I : toHoist) {
        moveInstructionToPreheader(I, preheader);
        hoistedInstructions_.insert(I);
        changed = true;
    }

    return changed;
}

bool LICMPass::simplifyInvariantPHIs(Loop* L) {
    bool changed = false;
    std::vector<PHINode*> toRemove;

    BasicBlock* header = L->getHeader();
    for (auto it = header->begin(); it != header->end(); ++it) {
        Instruction* I = *it;
        auto* phi = dyn_cast<PHINode>(I);
        if (!phi) {
            break;
        }

        Value* invariantValue = nullptr;
        bool allSameInvariant = true;

        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
            Value* incomingVal = phi->getIncomingValue(i);

            if (!isLoopInvariant(incomingVal, L)) {
                allSameInvariant = false;
                break;
            }

            if (invariantValue == nullptr) {
                invariantValue = incomingVal;
            } else if (invariantValue != incomingVal) {
                allSameInvariant = false;
                break;
            }
        }

        if (allSameInvariant && invariantValue) {
            phi->replaceAllUsesWith(invariantValue);
            toRemove.push_back(phi);
            changed = true;
        }
    }

    for (PHINode* phi : toRemove) {
        phi->removeFromParent();
        delete phi;
    }

    return changed;
}

bool LICMPass::hoistToFunctionEntry() {
    bool changed = false;
    std::vector<Instruction*> toHoist;

    for (const auto& pair : instructionToLoop_) {
        if (pair.second == nullptr &&
            loopInvariants_.find(pair.first) != loopInvariants_.end()) {
            toHoist.push_back(pair.first);
        }
    }

    if (toHoist.empty()) {
        return false;
    }

    std::vector<Instruction*> sortedInstructions;
    std::unordered_set<Instruction*> remaining(toHoist.begin(), toHoist.end());

    while (!remaining.empty()) {
        Instruction* next = nullptr;
        for (Instruction* candidate : remaining) {
            bool hasDependency = false;
            for (unsigned i = 0; i < candidate->getNumOperands(); ++i) {
                if (auto* operandInst =
                        dyn_cast<Instruction>(candidate->getOperand(i))) {
                    if (remaining.find(operandInst) != remaining.end()) {
                        hasDependency = true;
                        break;
                    }
                }
            }
            if (!hasDependency) {
                next = candidate;
                break;
            }
        }

        if (!next) {
            next = *remaining.begin();
        }

        sortedInstructions.push_back(next);
        remaining.erase(next);
    }

    toHoist = sortedInstructions;

    Function* F = toHoist[0]->getParent()->getParent();
    BasicBlock* entryBB = &F->front();

    for (Instruction* I : toHoist) {
        I->removeFromParent();

        auto* terminator = entryBB->getTerminator();
        if (terminator) {
            I->insertBefore(terminator);
        } else {
            entryBB->push_back(I);
        }
        hoistedInstructions_.insert(I);
        changed = true;
    }

    return changed;
}

bool LICMPass::isLoopInvariant(Value* V, Loop* L) const {
    if (dyn_cast<Constant>(V)) {
        return true;
    }

    if (dyn_cast<Argument>(V)) {
        return true;
    }

    if (auto* I = dyn_cast<Instruction>(V)) {
        if (!L->contains(I->getParent())) {
            return true;
        }

        return loopInvariants_.find(I) != loopInvariants_.end();
    }

    return false;
}

bool LICMPass::canHoistInstruction(Instruction* I, Loop* L) const {
    if (loopInvariants_.find(I) == loopInvariants_.end()) {
        return false;
    }

    if (hasSideEffects(I)) {
        return false;
    }

    if (!isMemorySafe(I, L)) {
        return false;
    }

    bool dominatesExits = isDominatedByLoop(I, L);
    bool alwaysExec = isAlwaysExecuted(I, L);
    bool safeToSpeculate = isSafeToSpeculate(I);

    return dominatesExits || alwaysExec || safeToSpeculate;
}

bool LICMPass::isSafeToSpeculate(Instruction* I) const {
    if (I->isBinaryOp()) {
        auto opcode = I->getOpcode();
        if (opcode == Opcode::Div || opcode == Opcode::Rem) {
            Value* divisor = I->getOperand(1);

            if (auto* constant = dyn_cast<Constant>(divisor)) {
                return true;
            }

            return false;
        }
        return true;
    }

    // Comparison operations are safe
    if (I->getOpcode() >= Opcode::ICmpEQ && I->getOpcode() <= Opcode::FCmpOGE) {
        return true;
    }

    // Cast operations are generally safe
    if (I->getOpcode() == Opcode::Cast) {
        return true;
    }

    if (I->getOpcode() == Opcode::GetElementPtr) {
        return true;
    }

    if (dyn_cast<LoadInst>(I)) {
        return true;
    }

    if (auto* call = dyn_cast<CallInst>(I)) {
        return isPureFunction(call->getCalledFunction());
    }
    return false;
}

bool LICMPass::isMemorySafe(Instruction* I, Loop* L) const {
    auto* load = dyn_cast<LoadInst>(I);
    auto* store = dyn_cast<StoreInst>(I);

    if (!load && !store) {
        return true;  // Non-memory instruction
    }

    if (store) {
        // Never hoist stores (they have side effects)
        return false;
    }

    if (load) {
        Value* loadPtr = load->getPointerOperand();

        // Check for aliasing with any stores in the loop
        for (BasicBlock* BB : L->getBlocks()) {
            for (auto it = BB->begin(); it != BB->end(); ++it) {
                Instruction* inst = *it;
                if (auto* storeInst = dyn_cast<StoreInst>(inst)) {
                    Value* storePtr = storeInst->getPointerOperand();

                    // Use alias analysis to check for potential aliasing
                    auto aliasResult = AA->alias(loadPtr, storePtr);

                    if (aliasResult != AliasAnalysis::AliasResult::NoAlias) {
                        // Special case: if both pointers are distinct function
                        // arguments, assume no alias
                        if (dyn_cast<Argument>(loadPtr) &&
                            dyn_cast<Argument>(storePtr) &&
                            loadPtr != storePtr) {
                            // Different function arguments are assumed not to
                            // alias
                            continue;
                        }

                        // Special case: GEP from function argument vs local
                        // alloca should not alias
                        bool loadFromArray = false;
                        bool storeToAlloca = false;

                        // Check if load pointer is derived from GEP of a
                        // function argument
                        if (auto* gep = dyn_cast<GetElementPtrInst>(loadPtr)) {
                            if (dyn_cast<Argument>(gep->getPointerOperand())) {
                                loadFromArray = true;
                            }
                        }

                        // Check if store pointer is a local alloca
                        if (auto* alloca = dyn_cast<AllocaInst>(storePtr)) {
                            storeToAlloca = true;
                        }

                        if (loadFromArray && storeToAlloca) {
                            // GEP from function argument array should not alias
                            // with local alloca
                            continue;
                        }
                        return false;
                    }
                }

                // Also check function calls that might have memory effects
                if (auto* call = dyn_cast<CallInst>(inst)) {
                    if (!isPureFunction(call->getCalledFunction())) {
                        // Be more conservative: only reject if call could
                        // modify global state For now, reject all non-pure
                        // calls
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool LICMPass::hasSideEffects(Instruction* I) const {
    // Stores have side effects
    if (dyn_cast<StoreInst>(I)) {
        return true;
    }

    // Function calls may have side effects
    if (auto* call = dyn_cast<CallInst>(I)) {
        return !isPureFunction(call->getCalledFunction());
    }

    return false;
}

bool LICMPass::isDominatedByLoop(Instruction* I, Loop* L) const {
    // Check if instruction dominates all loop exits
    for (BasicBlock* exitBlock : L->getExitBlocks()) {
        if (!DI->dominates(I->getParent(), exitBlock)) {
            return false;
        }
    }
    return true;
}

bool LICMPass::isAlwaysExecuted(Instruction* I, Loop* L) const {
    BasicBlock* BB = I->getParent();

    // Must be in the loop header and dominate all blocks in the loop
    if (BB != L->getHeader()) {
        return false;
    }

    // Check if instruction's block dominates all blocks in the loop
    for (BasicBlock* loopBB : L->getBlocks()) {
        if (!DI->dominates(BB, loopBB)) {
            return false;
        }
    }

    return true;
}

BasicBlock* LICMPass::getOrCreatePreheader(Loop* L) {
    BasicBlock* preheader = L->getPreheader();

    if (preheader) {
        return preheader;
    }

    // Create a new preheader
    BasicBlock* header = L->getHeader();
    Function* F = header->getParent();

    // Find the predecessor that comes from outside the loop
    BasicBlock* outsidePred = nullptr;
    auto preds = header->getPredecessors();
    for (BasicBlock* pred : preds) {
        if (!L->contains(pred)) {
            if (outsidePred) {
                // Multiple outside predecessors, can't create simple preheader
                return nullptr;
            }
            outsidePred = pred;
        }
    }

    if (!outsidePred) {
        return nullptr;
    }

    // Create new preheader block
    Context* ctx = F->getParent()->getContext();
    preheader = BasicBlock::Create(ctx, header->getName() + ".preheader", F);

    // Update CFG: outsidePred -> preheader -> header
    // Find the branch to header in outsidePred and redirect it
    auto* terminator = outsidePred->getTerminator();
    if (auto* br = dyn_cast<BranchInst>(terminator)) {
        if (br->isUnconditional()) {
            br->setOperand(0, preheader);
        } else {
            for (unsigned i = 0; i < br->getNumSuccessors(); ++i) {
                if (br->getSuccessor(i) == header) {
                    br->setOperand(i + 1, preheader);
                }
            }
        }
    }

    // Create branch from preheader to header
    auto* newBranch = BranchInst::Create(header, preheader);
    preheader->push_back(newBranch);

    // Update PHI nodes in header
    for (auto it = header->begin(); it != header->end(); ++it) {
        Instruction* I = *it;
        if (auto* phi = dyn_cast<PHINode>(I)) {
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                if (phi->getIncomingBlock(i) == outsidePred) {
                    phi->setIncomingBlock(i, preheader);
                }
            }
        }
    }

    return preheader;
}

bool LICMPass::isPureFunction(Function* F) const {
    if (!F) {
        return false;
    }

    // Use CallGraph analysis to determine if function has side effects
    return !CG->hasSideEffects(F);
}

void LICMPass::moveInstructionToPreheader(Instruction* I,
                                          BasicBlock* preheader) {
    I->removeFromParent();

    auto* terminator = preheader->getTerminator();
    if (terminator) {
        I->insertBefore(terminator);
    } else {
        preheader->push_back(I);
    }
}

std::vector<Loop*> LICMPass::getLoopsInPostOrder(
    const std::vector<std::unique_ptr<Loop>>& topLevelLoops) {
    std::vector<Loop*> postOrder;
    std::unordered_set<Loop*> visited;

    for (const auto& L : topLevelLoops) {
        addLoopsToPostOrder(L.get(), postOrder, visited);
    }

    return postOrder;
}

void LICMPass::addLoopsToPostOrder(Loop* L, std::vector<Loop*>& postOrder,
                                   std::unordered_set<Loop*>& visited) {
    if (visited.find(L) != visited.end()) {
        return;
    }

    visited.insert(L);

    // First process all subloops
    for (const auto& subLoop : L->getSubLoops()) {
        addLoopsToPostOrder(subLoop.get(), postOrder, visited);
    }

    // Then add this loop
    postOrder.push_back(L);
}

Loop* LICMPass::findOutermostInvariantLoop(Instruction* I, Loop* currentLoop) {
    Loop* outermostLoop = currentLoop;

    Loop* parentLoop = currentLoop->getParentLoop();
    while (parentLoop) {
        bool invariantInParent = true;

        for (unsigned i = 0; i < I->getNumOperands(); ++i) {
            Value* operand = I->getOperand(i);
            if (!isLoopInvariant(operand, parentLoop)) {
                invariantInParent = false;
                break;
            }
        }

        if (invariantInParent && isMemorySafe(I, parentLoop) &&
            !hasSideEffects(I)) {
            outermostLoop = parentLoop;
            parentLoop = parentLoop->getParentLoop();
        } else {
            break;
        }
    }

    return outermostLoop;
}

bool LICMPass::compareInstructionsForHoisting(Instruction* A, Instruction* B) {
    std::function<bool(Instruction*, Instruction*,
                       std::unordered_set<Instruction*>&)>
        dependsOn = [&](Instruction* user, Instruction* def,
                        std::unordered_set<Instruction*>& visited) -> bool {
        if (visited.find(user) != visited.end()) {
            return false;
        }
        visited.insert(user);

        for (unsigned i = 0; i < user->getNumOperands(); ++i) {
            Value* operand = user->getOperand(i);
            if (operand == def) {
                return true;
            }

            if (auto* operandInst = dyn_cast<Instruction>(operand)) {
                if (dependsOn(operandInst, def, visited)) {
                    return true;
                }
            }
        }
        return false;
    };

    std::unordered_set<Instruction*> visitedA;
    bool AdependsOnB = dependsOn(A, B, visitedA);

    std::unordered_set<Instruction*> visitedB;
    bool BdependsOnA = dependsOn(B, A, visitedB);

    if (AdependsOnB && !BdependsOnA) {
        return false;
    }
    if (BdependsOnA && !AdependsOnB) {
        return true;
    }

    if (A->getParent() != B->getParent()) {
        return DI->dominates(A->getParent(), B->getParent());
    }

    BasicBlock* BB = A->getParent();
    for (auto it = BB->begin(); it != BB->end(); ++it) {
        if (*it == A) return true;
        if (*it == B) return false;
    }
    return false;
}

}  // namespace midend
