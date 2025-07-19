#include "Pass/Transform/InstCombinePass.h"

#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions/BinaryOps.h"
#include "Support/Casting.h"

namespace midend {

bool InstCombinePass::runOnFunction(Function& function, AnalysisManager&) {
    return combineInstructions(function);
}

bool InstCombinePass::combineInstructions(Function& function) {
    changed_ = false;
    bool localChanged = true;

    while (localChanged) {
        localChanged = false;
        std::vector<Instruction*> toErase;

        for (auto& BB : function) {
            for (auto* inst : *BB) {
                Value* simplified = nullptr;

                if (auto* binOp = dyn_cast<BinaryOperator>(inst)) {
                    simplified = simplifyBinaryOp(binOp);
                } else if (auto* unaryOp = dyn_cast<UnaryOperator>(inst)) {
                    simplified = simplifyUnaryOp(unaryOp);
                } else if (auto* cmpInst = dyn_cast<CmpInst>(inst)) {
                    simplified = simplifyCmpInst(cmpInst);
                }

                if (simplified && simplified != inst) {
                    inst->replaceAllUsesWith(simplified);
                    toErase.push_back(inst);
                    localChanged = true;
                    changed_ = true;
                }
            }
        }

        for (auto* inst : toErase) {
            inst->eraseFromParent();
        }
    }

    return changed_;
}

Value* InstCombinePass::simplifyBinaryOp(BinaryOperator* binOp) {
    Value* lhs = binOp->getOperand1();
    Value* rhs = binOp->getOperand2();

    switch (binOp->getOpcode()) {
        case Opcode::Add:
            return simplifyAdd(lhs, rhs);
        case Opcode::Sub:
            return simplifySub(lhs, rhs);
        case Opcode::Mul:
            return simplifyMul(lhs, rhs);
        case Opcode::Div:
            return simplifyDiv(lhs, rhs);
        case Opcode::Rem:
            return simplifyRem(lhs, rhs);
        case Opcode::And:
            return simplifyAnd(lhs, rhs);
        case Opcode::Or:
            return simplifyOr(lhs, rhs);
        case Opcode::Xor:
            return simplifyXor(lhs, rhs);
        default:
            return nullptr;
    }
}

Value* InstCombinePass::simplifyUnaryOp(UnaryOperator* unaryOp) {
    Value* operand = unaryOp->getOperand();

    switch (unaryOp->getOpcode()) {
        case Opcode::UAdd:
            return operand;
        case Opcode::USub:
            if (auto* constInt = dyn_cast<ConstantInt>(operand)) {
                return ConstantInt::get(
                    static_cast<IntegerType*>(constInt->getType()),
                    -constInt->getSignedValue());
            }
            break;
        case Opcode::Not:
            if (auto* constInt = dyn_cast<ConstantInt>(operand)) {
                return ConstantInt::get(
                    static_cast<IntegerType*>(constInt->getType()),
                    ~constInt->getValue());
            }
            break;
        default:
            break;
    }

    return nullptr;
}

Value* InstCombinePass::simplifyCmpInst(CmpInst* cmpInst) {
    Value* lhs = cmpInst->getOperand1();
    Value* rhs = cmpInst->getOperand2();

    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        bool result = false;
        switch (cmpInst->getPredicate()) {
            case CmpInst::ICMP_EQ:
                result = lhsConst->getValue() == rhsConst->getValue();
                break;
            case CmpInst::ICMP_NE:
                result = lhsConst->getValue() != rhsConst->getValue();
                break;
            case CmpInst::ICMP_SLT:
                result =
                    lhsConst->getSignedValue() < rhsConst->getSignedValue();
                break;
            case CmpInst::ICMP_SLE:
                result =
                    lhsConst->getSignedValue() <= rhsConst->getSignedValue();
                break;
            case CmpInst::ICMP_SGT:
                result =
                    lhsConst->getSignedValue() > rhsConst->getSignedValue();
                break;
            case CmpInst::ICMP_SGE:
                result =
                    lhsConst->getSignedValue() >= rhsConst->getSignedValue();
                break;
            default:
                return nullptr;
        }
        return result ? ConstantInt::getTrue(lhs->getType()->getContext())
                      : ConstantInt::getFalse(lhs->getType()->getContext());
    }

    if (lhs == rhs) {
        switch (cmpInst->getPredicate()) {
            case CmpInst::ICMP_EQ:
            case CmpInst::ICMP_SLE:
            case CmpInst::ICMP_SGE:
                return ConstantInt::getTrue(lhs->getType()->getContext());
            case CmpInst::ICMP_NE:
            case CmpInst::ICMP_SLT:
            case CmpInst::ICMP_SGT:
                return ConstantInt::getFalse(lhs->getType()->getContext());
            default:
                break;
        }
    }

    return nullptr;
}

Value* InstCombinePass::simplifyAdd(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()),
                                lhsConst->getValue() + rhsConst->getValue());
    }

    if (lhsConst && lhsConst->isZero()) {
        return rhs;
    }

    if (rhsConst && rhsConst->isZero()) {
        return lhs;
    }

    return nullptr;
}

Value* InstCombinePass::simplifySub(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()),
                                lhsConst->getValue() - rhsConst->getValue());
    }

    if (rhsConst && rhsConst->isZero()) {
        return lhs;
    }

    if (lhs == rhs) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()), 0);
    }

    return nullptr;
}

Value* InstCombinePass::simplifyMul(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()),
                                lhsConst->getValue() * rhsConst->getValue());
    }

    if (lhsConst && lhsConst->isZero()) {
        return lhsConst;
    }

    if (rhsConst && rhsConst->isZero()) {
        return rhsConst;
    }

    if (lhsConst && lhsConst->isOne()) {
        return rhs;
    }

    if (rhsConst && rhsConst->isOne()) {
        return lhs;
    }

    return nullptr;
}

Value* InstCombinePass::simplifyDiv(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst && !rhsConst->isZero()) {
        return ConstantInt::get(
            static_cast<IntegerType*>(lhs->getType()),
            lhsConst->getSignedValue() / rhsConst->getSignedValue());
    }

    if (rhsConst && rhsConst->isOne()) {
        return lhs;
    }

    if (lhs == rhs) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()), 1);
    }

    return nullptr;
}

Value* InstCombinePass::simplifyRem(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst && !rhsConst->isZero()) {
        return ConstantInt::get(
            static_cast<IntegerType*>(lhs->getType()),
            lhsConst->getSignedValue() % rhsConst->getSignedValue());
    }

    if (rhsConst && rhsConst->isOne()) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()), 0);
    }

    if (lhs == rhs) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()), 0);
    }

    return nullptr;
}

Value* InstCombinePass::simplifyAnd(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()),
                                lhsConst->getValue() & rhsConst->getValue());
    }

    if (lhsConst && lhsConst->isZero()) {
        return lhsConst;
    }

    if (rhsConst && rhsConst->isZero()) {
        return rhsConst;
    }

    if (lhs == rhs) {
        return lhs;
    }

    return nullptr;
}

Value* InstCombinePass::simplifyOr(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()),
                                lhsConst->getValue() | rhsConst->getValue());
    }

    if (lhsConst && lhsConst->isZero()) {
        return rhs;
    }

    if (rhsConst && rhsConst->isZero()) {
        return lhs;
    }

    if (lhs == rhs) {
        return lhs;
    }

    return nullptr;
}

Value* InstCombinePass::simplifyXor(Value* lhs, Value* rhs) {
    auto* lhsConst = dyn_cast<ConstantInt>(lhs);
    auto* rhsConst = dyn_cast<ConstantInt>(rhs);

    if (lhsConst && rhsConst) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()),
                                lhsConst->getValue() ^ rhsConst->getValue());
    }

    if (lhsConst && lhsConst->isZero()) {
        return rhs;
    }

    if (rhsConst && rhsConst->isZero()) {
        return lhs;
    }

    if (lhs == rhs) {
        return ConstantInt::get(static_cast<IntegerType*>(lhs->getType()), 0);
    }

    return nullptr;
}

REGISTER_PASS(InstCombinePass, "instcombine")

}  // namespace midend