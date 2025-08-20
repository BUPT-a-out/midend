#include "Pass/Transform/GVNPass.h"

#include <algorithm>
#include <iostream>

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
#include "Pass/Analysis/MemorySSA.h"
#include "Support/Casting.h"

constexpr bool GVN_DEBUG = false;

namespace midend {

unsigned GVNPass::UnionFind::find(unsigned x) {
    if (parent.find(x) == parent.end()) {
        makeSet(x);
        return x;
    }
    if (parent[x] != x) {
        parent[x] = find(parent[x]);
    }
    return parent[x];
}

void GVNPass::UnionFind::unite(unsigned x, unsigned y) {
    unsigned rootX = find(x);
    unsigned rootY = find(y);

    if (rootX == rootY) return;

    if (rank[rootX] < rank[rootY]) {
        parent[rootX] = rootY;
    } else if (rank[rootX] > rank[rootY]) {
        parent[rootY] = rootX;
    } else {
        parent[rootY] = rootX;
        rank[rootX]++;
    }
}

bool GVNPass::UnionFind::connected(unsigned x, unsigned y) {
    return find(x) == find(y);
}

void GVNPass::UnionFind::makeSet(unsigned x) {
    parent[x] = x;
    rank[x] = 0;
}

bool GVNPass::Expression::operator==(const Expression& other) const {
    return opcode == other.opcode && operands == other.operands &&
           constant == other.constant && memoryPtr == other.memoryPtr;
}

std::size_t GVNPass::ExpressionHash::operator()(const Expression& expr) const {
    std::size_t hash = 0;
    hash = hash * 31 + std::hash<unsigned>()(expr.opcode);

    for (unsigned op : expr.operands) {
        hash = hash * 31 + std::hash<unsigned>()(op);
    }

    if (expr.constant) {
        hash = hash * 31 + std::hash<void*>()(expr.constant);
    }

    if (expr.memoryPtr) {
        hash = hash * 31 + std::hash<void*>()(expr.memoryPtr);
    }

    return hash;
}

bool GVNPass::runOnFunction(Function& F, AnalysisManager& AM) {
    if (F.isDeclaration()) {
        return false;
    }
    DI = AM.getAnalysis<DominanceInfo>("DominanceAnalysis", F);
    CG = AM.getAnalysis<CallGraph>("CallGraphAnalysis", *F.getParent());
    AA = AM.getAnalysis<AliasAnalysis::Result>("AliasAnalysis", F);
    MSSA = nullptr;  // AM.getAnalysis<MemorySSA>("MemorySSAAnalysis", F);
    if (!AA || !DI || !CG) {
        std::cerr << "Warning: GVNPass requires DominanceInfo, CallGraph, "
                     "AliasAnalysis, and MemorySSA. Skipping function "
                  << F.getName() << "." << std::endl;
        return false;
    }

    valueNumberToValue.clear();
    expressionToValueNumber.clear();
    valueToNumber.clear();
    nextValueNumber = 1;
    numGVNEliminated = 0;
    numPHIEliminated = 0;
    numLoadEliminated = 0;
    numCallEliminated = 0;
    blockInfoMap.clear();
    equivalenceClasses = UnionFind();

    bool changed = processFunction(F);

    return changed;
}

bool GVNPass::processFunction(Function& F) {
    // Build initial value numbers for constants and arguments
    for (unsigned i = 0; i < F.getNumArgs(); i++) {
        createValueNumber(F.getArg(i));
    }

    // Process blocks in RPO order for better propagation
    auto rpoOrder = DI->computeReversePostOrder();

    bool changed = false;
    bool iterChanged = true;

    while (iterChanged) {
        iterChanged = false;

        for (BasicBlock* BB : rpoOrder) {
            // Merge information from predecessors
            meetOperator(BB);

            // Process instructions in the block
            bool blockChanged = processBlock(BB);
            iterChanged |= blockChanged;
            changed |= blockChanged;
        }
    }

    return changed;
}

bool GVNPass::processBlock(BasicBlock* BB) {
    bool changed = false;

    std::vector<Instruction*> toProcess;
    for (auto& I : *BB) {
        toProcess.push_back(I);
    }

    for (Instruction* I : toProcess) {
        // Skip if instruction was already deleted
        if (!I->getParent()) continue;

        changed |= processInstruction(I);
    }

    return changed;
}

bool GVNPass::processInstruction(Instruction* I) {
    if (auto* PHI = dyn_cast<PHINode>(I)) {
        return eliminatePHIRedundancy(PHI);
    } else if (isa<LoadInst>(I)) {
        return processMemoryInstruction(I);
    } else if (isa<StoreInst>(I)) {
        invalidateLoads(I);
        return false;
    } else if (isa<CallInst>(I)) {
        return processFunctionCall(I);
    } else if (isSafeToEliminate(I)) {
        if (Value* simplified = trySimplifyInstruction(I)) {
            if constexpr (GVN_DEBUG) {
                std::cout << "GVN: Simplified: " << I->getName() << " to "
                          << simplified->getName() << std::endl;
            }
            replaceAndErase(I, simplified);
            numGVNEliminated++;
            return true;
        }

        Expression expr = createExpression(I);
        return eliminateRedundancy(I, expr);
    }

    // Just assign value number for other instructions
    getValueNumber(I);
    return false;
}

unsigned GVNPass::getValueNumber(Value* V) {
    auto it = valueToNumber.find(V);
    if (it != valueToNumber.end()) {
        return equivalenceClasses.find(it->second);
    }

    return createValueNumber(V);
}

unsigned GVNPass::createValueNumber(Value* V) {
    unsigned vn = nextValueNumber++;
    valueToNumber[V] = vn;
    valueNumberToValue[vn] = V;
    equivalenceClasses.makeSet(vn);
    return vn;
}

GVNPass::Expression GVNPass::createExpression(Instruction* I) {
    Expression expr;
    expr.opcode = static_cast<unsigned>(I->getOpcode());

    if (auto* BO = dyn_cast<BinaryOperator>(I)) {
        expr.operands.push_back(getValueNumber(BO->getOperand(0)));
        expr.operands.push_back(getValueNumber(BO->getOperand(1)));
        normalizeCommutativeExpression(expr);
    } else if (auto* CI = dyn_cast<CmpInst>(I)) {
        expr.opcode =
            (expr.opcode << 16) | static_cast<unsigned>(CI->getPredicate());
        expr.operands.push_back(getValueNumber(CI->getOperand(0)));
        expr.operands.push_back(getValueNumber(CI->getOperand(1)));
        // For commutative comparisons (equality)
        auto pred = CI->getPredicate();
        if (pred == CmpInst::ICMP_EQ || pred == CmpInst::ICMP_NE ||
            pred == CmpInst::FCMP_OEQ || pred == CmpInst::FCMP_ONE) {
            normalizeCommutativeExpression(expr);
        }
    } else if (auto* LI = dyn_cast<LoadInst>(I)) {
        expr.memoryPtr = LI->getPointerOperand();
        expr.operands.push_back(getValueNumber(LI->getPointerOperand()));
    } else if (auto* Call = dyn_cast<CallInst>(I)) {
        return createCallExpression(Call);
    } else if (auto* Cast = dyn_cast<CastInst>(I)) {
        expr.operands.push_back(getValueNumber(Cast->getOperand(0)));
    } else if (auto* GEP = dyn_cast<GetElementPtrInst>(I)) {
        auto* resultType = GEP->getType();
        expr.operands.push_back(reinterpret_cast<uintptr_t>(resultType));

        for (unsigned i = 0; i < GEP->getNumOperands(); ++i) {
            expr.operands.push_back(getValueNumber(GEP->getOperand(i)));
        }
    } else {
        for (unsigned i = 0; i < I->getNumOperands(); ++i) {
            expr.operands.push_back(getValueNumber(I->getOperand(i)));
        }
    }

    return expr;
}

void GVNPass::normalizeCommutativeExpression(Expression& expr) {
    if (!isCommutative(expr.opcode & 0xFFFF)) return;

    if (expr.operands.size() == 2 && expr.operands[0] > expr.operands[1]) {
        std::swap(expr.operands[0], expr.operands[1]);
    }
}

bool GVNPass::isCommutative(unsigned opcode) {
    Opcode op = static_cast<Opcode>(opcode);
    switch (op) {
        case Opcode::Add:
        case Opcode::Mul:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::LAnd:
        case Opcode::LOr:
            return true;
        // Floating point operations are not treated as commutative for
        // conservatism due to potential NaN/Inf and rounding differences
        case Opcode::FAdd:
        case Opcode::FMul:
            return false;
        default:
            return false;
    }
}

bool GVNPass::eliminateRedundancy(Instruction* I, const Expression& expr) {
    auto BB = I->getParent();

    // Check if expression already exists
    auto it = expressionToValueNumber.find(expr);
    if (it != expressionToValueNumber.end()) {
        unsigned existingVN = it->second;
        Value* leader = findLeader(existingVN, I->getParent());

        if (leader && leader != I && dominates(leader, I)) {
            // For load instructions, check for intervening stores
            if (auto* LI = dyn_cast<LoadInst>(I)) {
                if (auto* leaderInst = dyn_cast<Instruction>(leader)) {
                    if (hasInterveningStore(leaderInst, I,
                                            LI->getPointerOperand())) {
                        // Don't eliminate - there's an intervening store
                        goto record_new;
                    }
                }
            }

            if (auto* CI = dyn_cast<CallInst>(I)) {
                auto func = CI->getCalledFunction();
                if (isPureFunction(func)) {
                    goto replace_old;
                }

                auto requirements = CG->getRequiredValues(func);
                if (auto* leaderInst = dyn_cast<Instruction>(leader)) {
                    for (auto req : requirements) {
                        if (hasInterveningStore(leaderInst, I, req)) {
                            // If modified, do not eliminate
                            goto record_new;
                        }
                    }
                }
            }

        replace_old:
            if constexpr (GVN_DEBUG) {
                std::cout << "GVN: Eliminated redundant instruction: "
                          << I->getName() << " with " << leader->getName()
                          << std::endl;
            }
            replaceAndErase(I, leader);
            numGVNEliminated++;
            return true;
        }
    }

record_new:

    unsigned vn = getValueNumber(I);
    expressionToValueNumber[expr] = vn;
    blockInfoMap[BB].availableExpressions[expr] = vn;
    blockInfoMap[BB].availableValues.insert(vn);

    return false;
}

bool GVNPass::eliminatePHIRedundancy(PHINode* PHI) {
    if (PHI->getNumIncomingValues() == 0) return false;

    unsigned firstVN = getValueNumber(PHI->getIncomingValue(0));
    bool allSame = true;

    for (unsigned i = 1; i < PHI->getNumIncomingValues(); ++i) {
        unsigned vn = getValueNumber(PHI->getIncomingValue(i));
        if (!equivalenceClasses.connected(firstVN, vn)) {
            allSame = false;
            break;
        }
    }

    if (allSame) {
        // All operands are equivalent, replace PHI with one of them
        Value* replacement = PHI->getIncomingValue(0);
        if constexpr (GVN_DEBUG) {
            std::cout << "GVN: Eliminated PHI: " << PHI->getName() << " with "
                      << replacement->getName() << std::endl;
        }
        replaceAndErase(PHI, replacement);
        numPHIEliminated++;
        return true;
    }

    // Check if this PHI is equivalent to another PHI
    Expression expr;
    expr.opcode = static_cast<unsigned>(PHI->getOpcode());

    std::vector<std::pair<BasicBlock*, Value*>> incomingPairs;
    for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
        incomingPairs.emplace_back(PHI->getIncomingBlock(i),
                                   PHI->getIncomingValue(i));
    }

    std::sort(incomingPairs.begin(), incomingPairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& pair : incomingPairs) {
        expr.operands.push_back(getValueNumber(pair.second));
        expr.operands.push_back(reinterpret_cast<uintptr_t>(pair.first));
    }

    return eliminateRedundancy(PHI, expr);
}

bool GVNPass::processMemoryInstruction(Instruction* I) {
    auto* Load = cast<LoadInst>(I);

    // Try to eliminate redundant load
    if (eliminateLoadRedundancy(Load)) {
        numLoadEliminated++;
        return true;
    }

    // Record this load as available
    recordAvailableLoad(Load);

    // Assign value number
    Expression expr = createExpression(I);
    return eliminateRedundancy(I, expr);
}

bool GVNPass::eliminateLoadRedundancy(Instruction* Load) {
    auto* LI = cast<LoadInst>(Load);

    // Look for available loads in current and dominating blocks
    Value* availLoad = findAvailableLoad(LI, LI->getParent());
    if (availLoad && availLoad != LI) {
        if (auto* availInst = dyn_cast<LoadInst>(availLoad)) {
            if (AA && AA->alias(LI->getPointerOperand(),
                                availInst->getPointerOperand()) ==
                          AliasAnalysis::AliasResult::MustAlias) {
                return false;
            }
        }

        if constexpr (GVN_DEBUG) {
            std::cout << "GVN: Eliminated redundant load: " << LI->getName()
                      << " with " << availLoad->getName() << std::endl;
        }
        replaceAndErase(LI, availLoad);
        return true;
    }

    return false;
}

// TODO: do something
bool GVNPass::hasInterveningStore(Instruction* availLoad,
                                  Instruction* currentLoad, Value* ptr) {
    BasicBlock* availBB = availLoad->getParent();
    BasicBlock* currentBB = currentLoad->getParent();

    if (availBB == currentBB) {
        bool foundAvailLoad = false;

        for (auto* I : *availBB) {
            if (I == availLoad) {
                foundAvailLoad = true;
                continue;
            }

            if (I == currentLoad) {
                break;
            }

            if (foundAvailLoad && AA->mayModify(I, ptr)) {
                return true;
            }
        }
        return false;
    }

    // Use MemorySSA for cross-block stores analysis
    if (!MSSA) {
        return true;  // Conservative fallback
    }

    // Get the memory access for the current load
    auto* currentMemAccess = MSSA->getMemoryAccess(currentLoad);
    if (!currentMemAccess) {
        return true;  // Conservative fallback
    }

    // Find the clobbering memory access for the current load
    auto* clobberingAccess = MSSA->getClobberingMemoryAccess(currentMemAccess);
    if (!clobberingAccess) {
        return false;  // No clobbering access found
    }

    // Check if the available load is clobbered by any memory definition
    // between the available load and current load
    auto* availMemAccess = MSSA->getMemoryAccess(availLoad);
    if (!availMemAccess) {
        return true;  // Conservative fallback
    }

    // Walk the memory SSA chain to see if there's a clobber between
    // the available load and current load
    return walkMemorySSAForClobber(availMemAccess, currentMemAccess, ptr);
}

bool GVNPass::walkMemorySSAForClobber(MemoryAccess* availAccess,
                                      MemoryAccess* currentAccess, Value* ptr) {
    if (!availAccess || !currentAccess || !ptr) {
        return true;  // Conservative fallback
    }

    // Find the clobbering access for the current load
    auto* clobberingAccess = MSSA->getClobberingMemoryAccess(currentAccess);
    if (!clobberingAccess) {
        return false;  // No clobbering access
    }

    // Walk backwards from the clobbering access to see if it comes after
    // the available load access
    auto* currentClobber = clobberingAccess;
    std::unordered_set<MemoryAccess*> visited;

    while (currentClobber && visited.find(currentClobber) == visited.end()) {
        visited.insert(currentClobber);

        // If we reached the available access, there's no intervening store
        if (currentClobber == availAccess) {
            return false;
        }

        // If this is a memory def, check if it may clobber our pointer
        if (auto* memDef = dyn_cast<MemoryDef>(currentClobber)) {
            Instruction* defInst = memDef->getMemoryInst();
            if (!defInst || !defInst->getParent()) return false;

            // If this def may modify our pointer location, it's a clobber
            if (defInst && AA->mayModify(defInst, ptr)) {
                return true;
            }

            // Move to the defining access
            currentClobber = memDef->getDefiningAccess();
        } else if (auto* memPhi = dyn_cast<MemoryPhi>(currentClobber)) {
            // For phi nodes, conservatively check all incoming values
            for (unsigned i = 0; i < memPhi->getNumIncomingValues(); ++i) {
                auto* incomingAccess = memPhi->getIncomingValue(i);
                if (walkMemorySSAForClobber(availAccess, incomingAccess, ptr)) {
                    return true;
                }
            }
            break;
        } else {
            break;
        }
    }

    return false;  // No clobbering store found
}

Value* GVNPass::findAvailableLoad(Instruction* Load, BasicBlock* BB) {
    auto* LI = cast<LoadInst>(Load);
    Value* ptr = LI->getPointerOperand();

    // Check current block first
    auto& blockInfo = blockInfoMap[BB];
    for (auto& [availPtr, availLoad] : blockInfo.availableLoads) {
        if (availPtr == ptr ||
            (AA && AA->alias(ptr, availPtr) ==
                       AliasAnalysis::AliasResult::MustAlias)) {
            if (dominates(availLoad, Load) &&
                !hasInterveningStore(availLoad, Load, ptr)) {
                return availLoad;
            }
        }
    }

    // Check dominating blocks
    BasicBlock* idom = DI->getImmediateDominator(BB);
    if (idom && idom != BB) {
        return findAvailableLoad(Load, idom);
    }

    return nullptr;
}

void GVNPass::recordAvailableLoad(Instruction* Load) {
    auto* LI = cast<LoadInst>(Load);
    Value* ptr = LI->getPointerOperand();

    blockInfoMap[LI->getParent()].availableLoads.push_back({ptr, LI});
}

void GVNPass::invalidateLoads(Instruction* Store) {
    auto* SI = cast<StoreInst>(Store);
    Value* storePtr = SI->getPointerOperand();
    BasicBlock* BB = SI->getParent();

    // Invalidate loads that may alias with this store
    auto& blockInfo = blockInfoMap[BB];
    auto newEnd = std::remove_if(
        blockInfo.availableLoads.begin(), blockInfo.availableLoads.end(),
        [this, storePtr](const std::pair<Value*, Instruction*>& loadInfo) {
            if (!AA) return true;  // Conservative: invalidate all
            return AA->alias(loadInfo.first, storePtr) !=
                   AliasAnalysis::AliasResult::NoAlias;
        });
    blockInfo.availableLoads.erase(newEnd, blockInfo.availableLoads.end());
}

bool GVNPass::processFunctionCall(Instruction* Call) {
    auto* CI = cast<CallInst>(Call);
    Function* callee = CI->getCalledFunction();

    auto affectedValue = CG->getAffectedValues(callee);

    // It has side effect! Cannot reuse.
    if (CG->hasSideEffects(callee) || !affectedValue.empty()) {
        auto BB = Call->getParent();
        auto& blockInfo = blockInfoMap[BB];
        for (auto mod : affectedValue) {
            auto newEnd = std::remove_if(
                blockInfo.availableLoads.begin(),
                blockInfo.availableLoads.end(),
                [this, mod](const std::pair<Value*, Instruction*>& loadInfo) {
                    if (!AA) return true;  // Conservative: invalidate all
                    return AA->alias(loadInfo.first, mod) !=
                           AliasAnalysis::AliasResult::NoAlias;
                });
            blockInfo.availableLoads.erase(newEnd,
                                           blockInfo.availableLoads.end());
        }
        getValueNumber(CI);  // Still assign value number
        return false;
    }

    Expression expr = createCallExpression(CI);
    if (eliminateRedundancy(CI, expr)) {
        numCallEliminated++;
        return true;
    }

    return false;
}

bool GVNPass::isPureFunction(Function* F) {
    if (!F || !CG) return false;
    return CG->isPureFunction(F);
}

GVNPass::Expression GVNPass::createCallExpression(Instruction* Call) {
    auto* CI = cast<CallInst>(Call);
    Expression expr;
    expr.opcode = static_cast<unsigned>(CI->getOpcode());

    // Include called function in expression
    if (Function* F = CI->getCalledFunction()) {
        expr.operands.push_back(getValueNumber(F));
    } else {
        expr.operands.push_back(getValueNumber(CI->getCalledValue()));
    }

    // Include all arguments
    for (unsigned i = 0; i < CI->getNumArgOperands(); ++i) {
        expr.operands.push_back(getValueNumber(CI->getArgOperand(i)));
    }

    return expr;
}

Value* GVNPass::findLeader(unsigned valueNumber, BasicBlock* BB) {
    unsigned root = equivalenceClasses.find(valueNumber);

    // Check if we have a value for this number
    auto it = valueNumberToValue.find(root);
    if (it != valueNumberToValue.end()) {
        Value* V = it->second;
        if (isValueAvailable(V, BB)) {
            return V;
        }
    }

    // Check all values with same equivalence class
    for (auto& [val, vn] : valueToNumber) {
        if (equivalenceClasses.find(vn) == root && isValueAvailable(val, BB)) {
            return val;
        }
    }

    return nullptr;
}

bool GVNPass::isValueAvailable(Value* V, BasicBlock* BB) {
    if (isa<Constant>(V) || isa<Argument>(V)) {
        return true;
    }

    if (auto* I = dyn_cast<Instruction>(V)) {
        // Check if instruction has been deleted
        if (!I->getParent()) {
            return false;
        }
        return DI->dominates(I->getParent(), BB);
    }

    return false;
}

void GVNPass::meetOperator(BasicBlock* BB) {
    auto& info = blockInfoMap[BB];
    info.availableExpressions.clear();
    info.availableValues.clear();
    info.availableLoads.clear();

    auto preds = BB->getPredecessors();
    if (preds.empty()) return;

    if (preds.size() == 1) {
        // Single predecessor - copy its info
        auto predIt = blockInfoMap.find(preds[0]);
        if (predIt != blockInfoMap.end()) {
            info = predIt->second;
        }
    } else {
        // Multiple predecessors - intersect available values
        bool first = true;
        std::unordered_set<unsigned> commonValues;

        for (BasicBlock* pred : preds) {
            auto predIt = blockInfoMap.find(pred);
            if (predIt == blockInfoMap.end()) continue;

            if (first) {
                commonValues = predIt->second.availableValues;
                first = false;
            } else {
                // Intersect
                std::unordered_set<unsigned> newCommon;
                std::set_intersection(
                    commonValues.begin(), commonValues.end(),
                    predIt->second.availableValues.begin(),
                    predIt->second.availableValues.end(),
                    std::inserter(newCommon, newCommon.begin()));
                commonValues = std::move(newCommon);
            }
        }

        info.availableValues = commonValues;
    }
}

bool GVNPass::isSafeToEliminate(Instruction* I) {
    // Check if the instruction has side effects
    if (isa<StoreInst>(I) || isa<AllocaInst>(I)) return false;
    if (auto* CI = dyn_cast<CallInst>(I)) {
        if (!isPureFunction(CI->getCalledFunction())) return false;
    }
    if (I->isTerminator()) return false;
    if (I->getType()->isVoidType()) return false;

    // Handle floating point conservatively
    if (I->getType()->getKind() == TypeKind::Float) {
        // Be conservative with floating point due to NaN/Inf
        // Allow elimination of FP binary ops with identical operands/order
        // but not commutative variants
        return isa<CastInst>(I) || isa<BinaryOperator>(I);
    }

    return true;
}

bool GVNPass::hasMemoryEffects(Instruction* I) {
    return isa<LoadInst>(I) || isa<StoreInst>(I) || isa<CallInst>(I) ||
           isa<AllocaInst>(I);
}

bool GVNPass::dominates(Value* V, Instruction* I) {
    if (isa<Constant>(V) || isa<Argument>(V)) {
        return true;
    }

    if (auto* VI = dyn_cast<Instruction>(V)) {
        if (VI->getParent() == I->getParent()) {
            // Same block - check order
            for (auto* inst : *I->getParent()) {
                if (inst == VI) return true;
                if (inst == I) return false;
            }
        }
        return DI->dominates(VI->getParent(), I->getParent());
    }

    return false;
}

void GVNPass::replaceAndErase(Instruction* I, Value* replacement) {
    if (!I || !replacement) return;

    // Update value numbering before replacing uses
    unsigned oldVN = getValueNumber(I);
    unsigned newVN = getValueNumber(replacement);
    propagateEquivalence(oldVN, newVN);

    // Clean up value numbering maps
    valueToNumber.erase(I);
    valueNumberToValue.erase(oldVN);

    I->replaceAllUsesWith(replacement);
    I->eraseFromParent();
}

void GVNPass::propagateEquivalence(unsigned vn1, unsigned vn2) {
    equivalenceClasses.unite(vn1, vn2);
}

Value* GVNPass::trySimplifyInstruction(Instruction* I) {
    // Handle simple algebraic identities
    if (auto* BO = dyn_cast<BinaryOperator>(I)) {
        Value* LHS = BO->getOperand(0);
        Value* RHS = BO->getOperand(1);

        if (auto* CI = dyn_cast<ConstantInt>(RHS)) {
            switch (BO->getOpcode()) {
                case Opcode::Add:
                    // x + 0 = x
                    if (CI->isZero()) {
                        return LHS;
                    }
                    break;
                case Opcode::Sub:
                    // x - 0 = x
                    if (CI->isZero()) {
                        return LHS;
                    }
                    break;
                case Opcode::Mul:
                    // x * 1 = x
                    if (CI->isOne()) {
                        return LHS;
                    }
                    // x * 0 = 0
                    if (CI->isZero()) {
                        return RHS;
                    }
                    break;
                case Opcode::Or:
                    // x | 0 = x
                    if (CI->isZero()) {
                        return LHS;
                    }
                    break;
                case Opcode::And:
                    // x & 0 = 0
                    if (CI->isZero()) {
                        return RHS;
                    }
                    break;
                default:
                    break;
            }
        }

        if (auto* CI = dyn_cast<ConstantInt>(LHS)) {
            switch (BO->getOpcode()) {
                case Opcode::Add:
                    // 0 + x = x
                    if (CI->isZero()) {
                        return RHS;
                    }
                    break;
                case Opcode::Mul:
                    // 1 * x = x
                    if (CI->isOne()) {
                        return RHS;
                    }
                    // 0 * x = 0
                    if (CI->isZero()) {
                        return LHS;
                    }
                    break;
                case Opcode::Or:
                    // 0 | x = x
                    if (CI->isZero()) {
                        return RHS;
                    }
                    break;
                case Opcode::And:
                    // 0 & x = 0
                    if (CI->isZero()) {
                        return LHS;
                    }
                    break;
                default:
                    break;
            }
        }

        // x - x = 0, x ^ x = 0
        if (LHS == RHS) {
            switch (BO->getOpcode()) {
                case Opcode::Sub:
                case Opcode::Xor: {
                    Context* ctx =
                        I->getParent()->getParent()->getParent()->getContext();
                    return ConstantInt::get(ctx, 32, 0);
                }
                default:
                    break;
            }
        }
    }

    return nullptr;
}

}  // namespace midend