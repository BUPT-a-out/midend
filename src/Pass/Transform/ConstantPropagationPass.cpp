#include "Pass/Transform/ConstantPropagationPass.h"

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Support/Casting.h"

namespace midend {

void ConstantPropagationPass::getAnalysisUsage(
    std::unordered_set<std::string>& required,
    std::unordered_set<std::string>& preserved) const {
    (void)required;
    (void)preserved;
}

bool ConstantPropagationPass::runOnFunction(Function& f, AnalysisManager& am) {
    (void)am;

    if (f.empty()) return false;

    latticeValues_.clear();
    workList_.clear();
    inWorkList_.clear();
    instructionsToRemove_.clear();

    initializeLattice(f);
    propagateConstants();
    cleanupDeadInstructions();

    return !instructionsToRemove_.empty();
}

void ConstantPropagationPass::initializeLattice(Function& f) {
    for (auto& bb : f) {
        for (auto* inst : *bb) {
            // Add all instructions to the worklist initially
            addToWorkList(inst);
        }
    }
}

void ConstantPropagationPass::propagateConstants() {
    while (!workList_.empty()) {
        Instruction* inst = workList_.back();
        workList_.pop_back();
        inWorkList_.erase(inst);

        if (processInstruction(inst)) {
            addUsersToWorkList(inst);
        }
    }
}

bool ConstantPropagationPass::processInstruction(Instruction* inst) {
    Constant* oldValue = getLatticeValue(inst);
    Constant* newValue = evaluateInstruction(inst);

    if (newValue && newValue != oldValue) {
        setLatticeValue(inst, newValue);
        replaceWithConstant(inst, newValue);
        return true;
    }

    return false;
}

Constant* ConstantPropagationPass::evaluateInstruction(Instruction* inst) {
    if (auto* binOp = dyn_cast<BinaryOperator>(inst)) {
        return evaluateBinaryOp(binOp);
    } else if (auto* cmp = dyn_cast<CmpInst>(inst)) {
        return evaluateCmpInst(cmp);
    } else if (auto* select = dyn_cast<SelectInst>(inst)) {
        return evaluateSelectInst(select);
    } else if (auto* phi = dyn_cast<PHINode>(inst)) {
        return evaluatePHINode(phi);
    }

    return nullptr;
}

Constant* ConstantPropagationPass::evaluateBinaryOp(BinaryOperator* binOp) {
    Constant* lhs = getLatticeValue(binOp->getOperand1());
    Constant* rhs = getLatticeValue(binOp->getOperand2());

    if (!lhs || !rhs) return nullptr;

    return foldBinaryOp(static_cast<unsigned>(binOp->getOpcode()), lhs, rhs);
}

Constant* ConstantPropagationPass::evaluateCmpInst(CmpInst* cmp) {
    Constant* lhs = getLatticeValue(cmp->getOperand1());
    Constant* rhs = getLatticeValue(cmp->getOperand2());

    if (!lhs || !rhs) return nullptr;

    return foldCmpInst(static_cast<unsigned>(cmp->getPredicate()), lhs, rhs);
}

Constant* ConstantPropagationPass::evaluateSelectInst(SelectInst* select) {
    Constant* cond = getLatticeValue(select->getCondition());
    if (!cond) return nullptr;

    if (auto* condInt = dyn_cast<ConstantInt>(cond)) {
        if (condInt->isTrue()) {
            return getLatticeValue(select->getTrueValue());
        } else {
            return getLatticeValue(select->getFalseValue());
        }
    }

    return nullptr;
}

Constant* ConstantPropagationPass::evaluatePHINode(PHINode* phi) {
    Constant* result = nullptr;

    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        Constant* incomingConst = getLatticeValue(phi->getIncomingValue(i));

        if (!incomingConst) {
            return nullptr;
        }

        if (!result) {
            result = incomingConst;
        } else if (result != incomingConst) {
            return nullptr;
        }
    }

    return result;
}

Constant* ConstantPropagationPass::getLatticeValue(Value* v) {
    if (auto* constant = dyn_cast<Constant>(v)) {
        return constant;
    }

    auto it = latticeValues_.find(v);
    if (it != latticeValues_.end()) {
        return it->second;
    }

    return nullptr;
}

void ConstantPropagationPass::setLatticeValue(Value* v, Constant* c) {
    latticeValues_[v] = c;
}

void ConstantPropagationPass::addToWorkList(Instruction* inst) {
    if (inWorkList_.find(inst) == inWorkList_.end()) {
        workList_.push_back(inst);
        inWorkList_.insert(inst);
    }
}

void ConstantPropagationPass::addUsersToWorkList(Value* v) {
    for (auto* use : v->users()) {
        if (auto* inst = dyn_cast<Instruction>(use->getUser())) {
            addToWorkList(inst);
        }
    }
}

void ConstantPropagationPass::replaceWithConstant(Instruction* inst,
                                                  Constant* c) {
    inst->replaceAllUsesWith(c);
    instructionsToRemove_.push_back(inst);
}

void ConstantPropagationPass::cleanupDeadInstructions() {
    for (auto* inst : instructionsToRemove_) {
        inst->eraseFromParent();
    }
}

Constant* ConstantPropagationPass::foldBinaryOp(unsigned opcode, Constant* lhs,
                                                Constant* rhs) {
    auto* lhsInt = dyn_cast<ConstantInt>(lhs);
    auto* rhsInt = dyn_cast<ConstantInt>(rhs);

    if (lhsInt && rhsInt) {
        uint64_t lhsVal = lhsInt->getValue();
        uint64_t rhsVal = rhsInt->getValue();
        uint64_t result = 0;

        switch (static_cast<Opcode>(opcode)) {
            case Opcode::Add:
                result = lhsVal + rhsVal;
                break;
            case Opcode::Sub:
                result = lhsVal - rhsVal;
                break;
            case Opcode::Mul:
                result = lhsVal * rhsVal;
                break;
            case Opcode::Div:
                if (rhsVal != 0) {
                    result = static_cast<int64_t>(lhsVal) /
                             static_cast<int64_t>(rhsVal);
                } else {
                    return nullptr;
                }
                break;
            case Opcode::Rem:
                if (rhsVal != 0) {
                    result = static_cast<int64_t>(lhsVal) %
                             static_cast<int64_t>(rhsVal);
                } else {
                    return nullptr;
                }
                break;
            case Opcode::And:
                result = lhsVal & rhsVal;
                break;
            case Opcode::Or:
                result = lhsVal | rhsVal;
                break;
            case Opcode::Xor:
                result = lhsVal ^ rhsVal;
                break;
            case Opcode::LAnd:
                result = (lhsVal != 0) && (rhsVal != 0) ? 1 : 0;
                break;
            case Opcode::LOr:
                result = (lhsVal != 0) || (rhsVal != 0) ? 1 : 0;
                break;
            default:
                return nullptr;
        }

        return ConstantInt::get(cast<IntegerType>(lhs->getType()), result);
    }

    auto* lhsFP = dyn_cast<ConstantFP>(lhs);
    auto* rhsFP = dyn_cast<ConstantFP>(rhs);

    if (lhsFP && rhsFP) {
        float lhsVal = lhsFP->getValue();
        float rhsVal = rhsFP->getValue();
        float result = 0.0f;

        switch (static_cast<Opcode>(opcode)) {
            case Opcode::FAdd:
                result = lhsVal + rhsVal;
                break;
            case Opcode::FSub:
                result = lhsVal - rhsVal;
                break;
            case Opcode::FMul:
                result = lhsVal * rhsVal;
                break;
            case Opcode::FDiv:
                if (rhsVal != 0.0f) {
                    result = lhsVal / rhsVal;
                } else {
                    return nullptr;
                }
                break;
            default:
                return nullptr;
        }

        return ConstantFP::get(cast<FloatType>(lhs->getType()), result);
    }

    return nullptr;
}

Constant* ConstantPropagationPass::foldCmpInst(unsigned predicate,
                                               Constant* lhs, Constant* rhs) {
    auto* lhsInt = dyn_cast<ConstantInt>(lhs);
    auto* rhsInt = dyn_cast<ConstantInt>(rhs);

    if (lhsInt && rhsInt) {
        int64_t lhsVal = lhsInt->getSignedValue();
        int64_t rhsVal = rhsInt->getSignedValue();
        bool result = false;

        switch (static_cast<CmpInst::Predicate>(predicate)) {
            case CmpInst::ICMP_EQ:
                result = lhsVal == rhsVal;
                break;
            case CmpInst::ICMP_NE:
                result = lhsVal != rhsVal;
                break;
            case CmpInst::ICMP_SLT:
                result = lhsVal < rhsVal;
                break;
            case CmpInst::ICMP_SLE:
                result = lhsVal <= rhsVal;
                break;
            case CmpInst::ICMP_SGT:
                result = lhsVal > rhsVal;
                break;
            case CmpInst::ICMP_SGE:
                result = lhsVal >= rhsVal;
                break;
            default:
                return nullptr;
        }

        return result ? ConstantInt::getTrue(lhs->getType()->getContext())
                      : ConstantInt::getFalse(lhs->getType()->getContext());
    }

    auto* lhsFP = dyn_cast<ConstantFP>(lhs);
    auto* rhsFP = dyn_cast<ConstantFP>(rhs);

    if (lhsFP && rhsFP) {
        float lhsVal = lhsFP->getValue();
        float rhsVal = rhsFP->getValue();
        bool result = false;

        switch (static_cast<CmpInst::Predicate>(predicate)) {
            case CmpInst::FCMP_OEQ:
                result = lhsVal == rhsVal;
                break;
            case CmpInst::FCMP_ONE:
                result = lhsVal != rhsVal;
                break;
            case CmpInst::FCMP_OLT:
                result = lhsVal < rhsVal;
                break;
            case CmpInst::FCMP_OLE:
                result = lhsVal <= rhsVal;
                break;
            case CmpInst::FCMP_OGT:
                result = lhsVal > rhsVal;
                break;
            case CmpInst::FCMP_OGE:
                result = lhsVal >= rhsVal;
                break;
            default:
                return nullptr;
        }

        return result ? ConstantInt::getTrue(lhs->getType()->getContext())
                      : ConstantInt::getFalse(lhs->getType()->getContext());
    }

    return nullptr;
}

REGISTER_PASS(ConstantPropagationPass, "constant-propagation")

}  // namespace midend