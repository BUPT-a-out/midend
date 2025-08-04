#include "Pass/Analysis/AliasAnalysis.h"

#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/CallGraph.h"
#include "Support/Casting.h"

namespace midend {

using AliasResult = AliasAnalysis::AliasResult;
using Location = AliasAnalysis::Location;

AliasResult AliasAnalysis::Result::alias(const Location& loc1,
                                         const Location& loc2) {
    auto key = std::make_pair(loc1, loc2);
    auto it = aliasCache.find(key);
    if (it != aliasCache.end()) {
        return it->second;
    }

    auto reverseKey = std::make_pair(loc2, loc1);
    it = aliasCache.find(reverseKey);
    if (it != aliasCache.end()) {
        return it->second;
    }

    aliasCache[key] = AliasResult::MayAlias;

    auto result = aliasInternal(loc1, loc2);

    aliasCache[key] = result;

    return result;
}

AliasResult AliasAnalysis::Result::alias(Value* v1, Value* v2) {
    return alias(Location(v1, 0), Location(v2, 0));
}

AliasResult AliasAnalysis::Result::alias(Value* v1, uint64_t size1, Value* v2,
                                         uint64_t size2) {
    return alias(Location(v1, size1), Location(v2, size2));
}

bool AliasAnalysis::Result::mayModify(Instruction* inst, const Location& loc) {
    if (auto store = dyn_cast<StoreInst>(inst)) {
        auto storePtr = store->getPointerOperand();
        return alias(Location(storePtr, 0), loc) != AliasResult::NoAlias;
    }

    if (auto call = dyn_cast<CallInst>(inst)) {
        if (analysisManager) {
            if (auto cg = analysisManager->getAnalysis<CallGraph>(
                    "CallGraphAnalysis", *function->getParent())) {
                if (auto calledFunction = call->getCalledFunction()) {
                    if (!cg->hasSideEffects(calledFunction)) {
                        return false;
                    }
                }
            }
        }

        if (isGlobalObject(loc.ptr)) {
            return true;
        }

        for (size_t i = 0; i < call->getNumArgOperands(); ++i) {
            Value* arg = call->getArgOperand(i);
            if (alias(Location(arg, 0), loc) != AliasResult::NoAlias) {
                return true;
            }
        }
        return true;
    }

    return false;
}

bool AliasAnalysis::Result::mayModify(Instruction* inst, Value* ptr) {
    return mayModify(inst, Location(ptr, 0));
}

bool AliasAnalysis::Result::mayRef(Instruction* inst, const Location& loc) {
    if (auto load = dyn_cast<LoadInst>(inst)) {
        auto loadPtr = load->getPointerOperand();
        return alias(Location(loadPtr, 0), loc) != AliasResult::NoAlias;
    }

    if (auto store = dyn_cast<StoreInst>(inst)) {
        auto storePtr = store->getPointerOperand();
        return alias(Location(storePtr, 0), loc) != AliasResult::NoAlias;
    }

    if (isa<CallInst>(inst)) {
        return true;
    }

    return false;
}

bool AliasAnalysis::Result::mayRef(Instruction* inst, Value* ptr) {
    return mayRef(inst, Location(ptr, 0));
}

std::vector<Value*> AliasAnalysis::Result::getMayAliasSet(Value* v) {
    std::vector<Value*> result;

    auto it = pointsToSets.find(v);
    if (it != pointsToSets.end()) {
        for (auto* target : it->second) {
            result.push_back(target);
        }
    }

    for (auto* alloc : allocSites) {
        if (alias(v, alloc) != AliasResult::NoAlias) {
            result.push_back(alloc);
        }
    }

    return result;
}

Value* AliasAnalysis::Result::getUnderlyingObject(Value* v) {
    if (!v) return nullptr;

    std::unordered_set<Value*> visited;

    while (v) {
        if (visited.count(v)) {
            return v;
        }
        visited.insert(v);

        // Check if this is an allocation site
        if (isa<AllocaInst>(v) || isa<GlobalVariable>(v)) {
            return v;
        }

        // Look through GEPs
        if (auto gep = dyn_cast<GetElementPtrInst>(v)) {
            v = gep->getPointerOperand();
            continue;
        }

        // Look through bitcasts
        if (auto cast = dyn_cast<CastInst>(v)) {
            if (cast->getCastOpcode() == CastInst::BitCast ||
                cast->getCastOpcode() == CastInst::IntToPtr) {
                v = cast->getOperand(0);
                continue;
            }
        }

        // Look through PHI nodes (conservatively pick first operand)
        if (auto phi = dyn_cast<PHINode>(v)) {
            if (phi->getNumOperands() > 0) {
                v = phi->getOperand(0);
                continue;
            }
        }
        break;
    }

    return v;
}

bool AliasAnalysis::Result::isStackObject(Value* v) {
    auto underlying = getUnderlyingObject(v);
    return underlying && isa<AllocaInst>(underlying);
}

bool AliasAnalysis::Result::isGlobalObject(Value* v) {
    auto underlying = getUnderlyingObject(v);
    return underlying && isa<GlobalVariable>(underlying);
}

bool AliasAnalysis::Result::isDifferentObject(Value* v1, Value* v2) {
    auto obj1 = getUnderlyingObject(v1);
    auto obj2 = getUnderlyingObject(v2);

    if (!obj1 || !obj2 || obj1 == obj2) {
        return false;
    }

    if ((isa<AllocaInst>(obj1) || isa<GlobalVariable>(obj1)) &&
        (isa<AllocaInst>(obj2) || isa<GlobalVariable>(obj2))) {
        return true;
    }

    return false;
}

AliasResult AliasAnalysis::Result::aliasInternal(const Location& loc1,
                                                 const Location& loc2) {
    Value* v1 = loc1.ptr;
    Value* v2 = loc2.ptr;
    uint64_t size1 = loc1.size;
    uint64_t size2 = loc2.size;

    if (v1 == v2) {
        return AliasResult::MustAlias;
    }

    if (isa<ConstantPointerNull>(v1) || isa<ConstantPointerNull>(v2)) {
        return AliasResult::NoAlias;
    }

    if (isa<PHINode>(v1) || isa<PHINode>(v2)) {
        return aliasPHI(v1, size1, v2, size2);
    }

    if (isDifferentObject(v1, v2)) {
        return AliasResult::NoAlias;
    }

    if (auto cast1 = dyn_cast<CastInst>(v1)) {
        if (cast1->getCastOpcode() == CastInst::BitCast) {
            return alias(Location(cast1->getOperand(0), size1),
                         Location(v2, size2));
        }
    }

    if (auto cast2 = dyn_cast<CastInst>(v2)) {
        if (cast2->getCastOpcode() == CastInst::BitCast) {
            return alias(Location(v1, size1),
                         Location(cast2->getOperand(0), size2));
        }
    }

    // Handle move instructions - they alias with their operand
    if (auto move1 = dyn_cast<MoveInst>(v1)) {
        return alias(Location(move1->getOperand(0), size1),
                     Location(v2, size2));
    }

    if (auto move2 = dyn_cast<MoveInst>(v2)) {
        return alias(Location(v1, size1),
                     Location(move2->getOperand(0), size2));
    }

    if (auto gep1 = dyn_cast<GetElementPtrInst>(v1)) {
        if (auto gep2 = dyn_cast<GetElementPtrInst>(v2)) {
            return aliasGEP(gep1, size1, gep2, size2);
        }
    }

    auto it1 = pointsToSets.find(v1);
    auto it2 = pointsToSets.find(v2);
    if (it1 != pointsToSets.end() && it2 != pointsToSets.end()) {
        for (auto* target1 : it1->second) {
            if (it2->second.count(target1)) {
                return AliasResult::MayAlias;
            }
        }
        return AliasResult::NoAlias;
    }

    return AliasResult::MayAlias;
}

AliasResult AliasAnalysis::Result::aliasGEP(GetElementPtrInst* gep1,
                                            uint64_t size1,
                                            GetElementPtrInst* gep2,
                                            uint64_t size2) {
    Value* base1 = gep1->getPointerOperand();
    Value* base2 = gep2->getPointerOperand();

    if (base1 != base2) {
        return alias(Location(base1, 0), Location(base2, 0));
    }

    // Same base pointer - check if we can prove different offsets
    int64_t offset1, offset2;
    bool const1 = computeGEPOffset(gep1, offset1);
    bool const2 = computeGEPOffset(gep2, offset2);

    if (const1 && const2) {
        if (size1 == 0 || size2 == 0) {
            return offset1 == offset2 ? AliasResult::MustAlias
                                      : AliasResult::NoAlias;
        } else {
            // Check if ranges overlap
            int64_t end1 = offset1 + size1;
            int64_t end2 = offset2 + size2;
            if (offset1 >= end2 || offset2 >= end1) {
                return AliasResult::NoAlias;
            }
            if (offset1 == offset2 && size1 == size2) {
                return AliasResult::MustAlias;
            }
            return AliasResult::MayAlias;
        }
    }

    return AliasResult::MayAlias;
}

AliasResult AliasAnalysis::Result::aliasPHI(Value* v1, uint64_t size1,
                                            Value* v2, uint64_t size2) {
    PHINode* phi1 = dyn_cast<PHINode>(v1);
    PHINode* phi2 = dyn_cast<PHINode>(v2);

    if (phi1 && !phi2) {
        bool allNoAlias = true;
        bool allMustAlias = true;

        for (size_t i = 0; i < phi1->getNumOperands(); ++i) {
            auto result = alias(Location(phi1->getOperand(i), size1),
                                Location(v2, size2));
            if (result != AliasResult::NoAlias) allNoAlias = false;
            if (result != AliasResult::MustAlias) allMustAlias = false;
        }

        if (allNoAlias) return AliasResult::NoAlias;
        if (allMustAlias) return AliasResult::MustAlias;
        return AliasResult::MayAlias;
    }

    if (!phi1 && phi2) {
        return aliasPHI(v2, size2, v1, size1);
    }

    if (phi1 && phi2) {
        return AliasResult::MayAlias;
    }

    return AliasResult::MayAlias;
}

bool AliasAnalysis::Result::computeGEPOffset(GetElementPtrInst* gep,
                                             int64_t& offset) {
    offset = 0;

    auto strides = gep->getStrides();

    for (size_t i = 0; i < gep->getNumIndices(); ++i) {
        Value* idx = gep->getIndex(i);

        auto constIdx = dyn_cast<ConstantInt>(idx);
        if (!constIdx) {
            return false;
        }

        int64_t idxValue = constIdx->getSignedValue();
        offset += idxValue * static_cast<int64_t>(strides[i]);
    }

    return true;
}

void AliasAnalysis::Result::buildPointsToSets() {
    for (auto& bb : *function) {
        for (auto* inst : *bb) {
            if (isa<AllocaInst>(inst)) {
                allocSites.insert(inst);
                pointsToSets[inst].insert(inst);
            }
        }
    }

    addConstraints();
}

void AliasAnalysis::Result::addConstraints() {
    struct Constraint {
        enum Type { ADDR_OF, COPY, LOAD, STORE };
        Type type;
        Value* lhs;
        Value* rhs;
    };

    std::vector<Constraint> constraints;

    for (auto& bb : *function) {
        for (auto* inst : *bb) {
            if (auto load = dyn_cast<LoadInst>(inst)) {
                // x = *p: x gets what p points to
                constraints.push_back(
                    {Constraint::LOAD, inst, load->getPointerOperand()});
            } else if (auto store = dyn_cast<StoreInst>(inst)) {
                // *p = x: what p points to gets x
                constraints.push_back({Constraint::STORE,
                                       store->getPointerOperand(),
                                       store->getValueOperand()});
            } else if (auto gep = dyn_cast<GetElementPtrInst>(inst)) {
                // GEP preserves points-to relationship
                constraints.push_back(
                    {Constraint::COPY, inst, gep->getPointerOperand()});
            } else if (auto cast = dyn_cast<CastInst>(inst)) {
                if (cast->getCastOpcode() == CastInst::BitCast) {
                    // Bitcast preserves points-to relationship
                    constraints.push_back(
                        {Constraint::COPY, inst, cast->getOperand(0)});
                }
            } else if (auto move = dyn_cast<MoveInst>(inst)) {
                // Move preserves points-to relationship
                constraints.push_back(
                    {Constraint::COPY, inst, move->getOperand(0)});
            } else if (auto phi = dyn_cast<PHINode>(inst)) {
                // PHI node: result points to union of all operands
                for (size_t i = 0; i < phi->getNumOperands(); ++i) {
                    constraints.push_back(
                        {Constraint::COPY, inst, phi->getOperand(i)});
                }
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (const auto& c : constraints) {
            switch (c.type) {
                case Constraint::COPY: {
                    // lhs = rhs: lhs points to everything rhs points to
                    auto rhsIt = pointsToSets.find(c.rhs);
                    if (rhsIt != pointsToSets.end()) {
                        auto& lhsSet = pointsToSets[c.lhs];
                        for (auto* target : rhsIt->second) {
                            if (lhsSet.insert(target).second) {
                                changed = true;
                            }
                        }
                    }
                    break;
                }

                case Constraint::LOAD: {
                    // lhs = *rhs: lhs points to everything that
                    // anything rhs points to points to
                    auto rhsIt = pointsToSets.find(c.rhs);
                    if (rhsIt != pointsToSets.end()) {
                        auto& lhsSet = pointsToSets[c.lhs];
                        for (auto* intermediate : rhsIt->second) {
                            auto intIt = pointsToSets.find(intermediate);
                            if (intIt != pointsToSets.end()) {
                                for (auto* target : intIt->second) {
                                    if (lhsSet.insert(target).second) {
                                        changed = true;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }

                case Constraint::STORE: {
                    // *lhs = rhs: everything lhs points to points to
                    // everything rhs points to
                    auto lhsIt = pointsToSets.find(c.lhs);
                    auto rhsIt = pointsToSets.find(c.rhs);
                    if (lhsIt != pointsToSets.end() &&
                        rhsIt != pointsToSets.end()) {
                        for (auto* intermediate : lhsIt->second) {
                            auto& intSet = pointsToSets[intermediate];
                            for (auto* target : rhsIt->second) {
                                if (intSet.insert(target).second) {
                                    changed = true;
                                }
                            }
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
}

std::unique_ptr<AnalysisResult> AliasAnalysis::runOnFunction(
    Function& f, AnalysisManager& am) {
    auto result = std::make_unique<Result>(&f, &am);
    result->buildPointsToSets();
    return result;
}

std::unique_ptr<AnalysisResult> AliasAnalysis::runOnModule(
    Module& m, AnalysisManager& am) {
    for (auto& f : m) {
        if (!f->isDeclaration()) {
            return runOnFunction(*f, am);
        }
    }
    return nullptr;
}

}  // namespace midend