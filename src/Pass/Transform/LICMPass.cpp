#include "Pass/Transform/LICMPass.h"

#include <algorithm>
#include <functional>
#include <vector>

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

    bool changed = false;

    // Process top-level loops recursively (DFS order processes innermost loops
    // first)
    for (const auto& L : LI->getTopLevelLoops()) {
        if (processLoopRecursive(L.get())) {
            changed = true;
        }
    }

    return changed;
}

bool LICMPass::processLoopRecursive(Loop* L) {
    bool changed = false;

    // First process all subloops recursively (innermost first)
    for (const auto& subLoop : L->getSubLoops()) {
        if (processLoopRecursive(subLoop.get())) {
            changed = true;
        }
    }

    // Then process this loop
    if (processLoop(L)) {
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

    // Then hoist loop invariant instructions
    if (hoistLoopInvariants(L)) {
        changed = true;
    }

    return changed;
}

bool LICMPass::hoistLoopInvariants(Loop* L) {
    BasicBlock* preheader = L->getPreheader();
    if (!preheader) {
        return false;
    }

    bool changed = false;
    bool foundInvariant = true;

    // Iteratively find and hoist loop-invariant instructions until no more can
    // be found
    while (foundInvariant) {
        foundInvariant = false;
        std::vector<Instruction*> toHoist;

        // Find all instructions that are loop invariant
        for (BasicBlock* BB : L->getBlocks()) {
            for (auto it = BB->begin(); it != BB->end(); ++it) {
                Instruction* I = *it;

                // Check if all operands are loop invariant
                bool allOperandsInvariant = true;
                for (unsigned i = 0; i < I->getNumOperands(); ++i) {
                    Value* operand = I->getOperand(i);
                    if (!isLoopInvariant(operand, L)) {
                        allOperandsInvariant = false;
                        break;
                    }
                }

                // If all operands are invariant and instruction can be hoisted
                if (allOperandsInvariant && canHoistInstruction(I, L)) {
                    toHoist.push_back(I);
                }
            }
        }

        // Hoist the instructions
        for (Instruction* I : toHoist) {
            moveInstructionToPreheader(I, preheader);
            changed = true;
            foundInvariant = true;
        }
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

bool LICMPass::isLoopInvariant(Value* V, Loop* L) const {
    if (dyn_cast<Constant>(V)) {
        return true;
    }

    if (dyn_cast<Argument>(V)) {
        return true;
    }

    if (auto* I = dyn_cast<Instruction>(V)) {
        // Check if instruction is in this loop
        if (!L->contains(I->getParent())) {
            // Instruction is outside the loop, so it's invariant
            return true;
        }
        // Otherwise, it's not invariant for this loop
        return false;
    }

    return false;
}

bool LICMPass::canHoistInstruction(Instruction* I, Loop* L) const {
    if (hasSideEffects(I)) {
        return false;
    }

    if (!isMemorySafe(I, L)) {
        return false;
    }

    // Check if any operand is a PHI node defined in the current loop header or
    // any nested loop header. PHI nodes from parent loops are safe to use.
    std::unordered_set<Value*> visited;
    std::function<bool(Value*, Loop*)> usesPHIFromLoopHeader =
        [&](Value* V, Loop* currentL) -> bool {
        if (visited.find(V) != visited.end()) {
            return false;
        }
        visited.insert(V);

        if (auto* phi = dyn_cast<PHINode>(V)) {
            BasicBlock* phiBB = phi->getParent();
            Loop* phiLoop = LI->getLoopFor(phiBB);

            if (phiLoop &&
                (phiLoop == currentL || currentL->contains(phiLoop))) {
                return true;
            }
        }

        if (auto* inst = dyn_cast<Instruction>(V)) {
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                if (usesPHIFromLoopHeader(inst->getOperand(i), currentL)) {
                    return true;
                }
            }
        }

        return false;
    };

    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
        Value* operand = I->getOperand(i);
        visited.clear();
        if (usesPHIFromLoopHeader(operand, L)) {
            return false;
        }
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
    auto* call = dyn_cast<CallInst>(I);

    if (!load && !store && !call) {
        return true;  // Non-memory instruction
    }

    if (store) {
        // Never hoist stores (they have side effects)
        return false;
    }

    if (call) {
        // For function calls, check if any instruction in the loop may modify
        // the arguments passed to the function
        if (!isPureFunction(call->getCalledFunction())) {
            return false;
        }

        for (unsigned i = 0; i < call->getNumArgOperands(); ++i) {
            Value* arg = call->getArgOperand(i);

            if (!arg->getType()->isPointerType()) {
                continue;
            }

            std::function<bool(Loop*)> checkLoopAndSubloops =
                [&](Loop* loop) -> bool {
                for (BasicBlock* BB : loop->getBlocks()) {
                    for (auto it = BB->begin(); it != BB->end(); ++it) {
                        Instruction* inst = *it;

                        if (AA->mayModify(inst, arg)) {
                            return true;
                        }
                    }
                }

                for (const auto& subLoop : loop->getSubLoops()) {
                    if (checkLoopAndSubloops(subLoop.get())) {
                        return true;
                    }
                }

                return false;
            };

            if (checkLoopAndSubloops(L)) {
                return false;
            }
        }
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

}  // namespace midend