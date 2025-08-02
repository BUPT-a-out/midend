#include "Pass/Transform/StrengthReductionPass.h"

#include <cmath>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Type.h"
#include "Support/Casting.h"

namespace midend {

unsigned StrengthReductionPass::mulThreshold = 3;
unsigned StrengthReductionPass::divThreshold = 5;

bool StrengthReductionPass::runOnFunction(Function& function,
                                          AnalysisManager&) {
    if (!function.isDefinition()) {
        return false;
    }

    init();

    std::vector<Instruction*> worklist;
    for (auto* block : function) {
        for (auto* inst : *block) {
            worklist.push_back(inst);
        }
    }

    for (auto* inst : worklist) {
        processInstruction(inst);
    }

    return changed;
}

void StrengthReductionPass::init() { changed = false; }

bool StrengthReductionPass::processInstruction(Instruction* inst) {
    if (auto* binaryOp = dyn_cast<BinaryOperator>(inst)) {
        switch (binaryOp->getOpcode()) {
            case Opcode::Mul:
                return optimizeMultiplication(binaryOp);
            case Opcode::Div:
                return optimizeDivision(binaryOp);
            case Opcode::Rem:
                return optimizeModulo(binaryOp);
            default:
                break;
        }
    }
    return false;
}

bool StrengthReductionPass::optimizeMultiplication(BinaryOperator* mulInst) {
    Value* lhs = mulInst->getOperand(0);
    Value* rhs = mulInst->getOperand(1);
    ConstantInt* constant = nullptr;
    Value* variable = nullptr;

    if (auto* c = dyn_cast<ConstantInt>(lhs)) {
        constant = c;
        variable = rhs;
    } else if (auto* c = dyn_cast<ConstantInt>(rhs)) {
        constant = c;
        variable = lhs;
    }

    if (!constant || !variable) {
        return false;
    }

    int64_t constValue = static_cast<int64_t>(constant->getValue());

    if (constValue == 0) {
        auto* zero =
            ConstantInt::get(dyn_cast<IntegerType>(mulInst->getType()), 0);
        mulInst->replaceAllUsesWith(zero);
        mulInst->eraseFromParent();
        changed = true;
        return true;
    }

    if (constValue == 1) {
        mulInst->replaceAllUsesWith(variable);
        mulInst->eraseFromParent();
        changed = true;
        return true;
    }

    if (constValue == -1) {
        IRBuilder builder(mulInst->getParent());
        builder.setInsertPoint(mulInst);
        auto* zero =
            ConstantInt::get(dyn_cast<IntegerType>(mulInst->getType()), 0);
        Value* replacement = builder.createSub(zero, variable);
        mulInst->replaceAllUsesWith(replacement);
        mulInst->eraseFromParent();
        changed = true;
        return true;
        return true;
    }

    MulDecomposition decomp = decomposeMul(std::abs(constValue));

    if (decomp.numOps > mulThreshold) {
        return false;
    }

    Value* replacement = createMulReplacement(variable, decomp, mulInst);

    if (constValue < 0) {
        IRBuilder builder(mulInst->getParent());
        builder.setInsertPoint(mulInst);
        auto* zero =
            ConstantInt::get(dyn_cast<IntegerType>(replacement->getType()), 0);
        replacement = builder.createSub(zero, replacement);
    }

    mulInst->replaceAllUsesWith(replacement);
    mulInst->eraseFromParent();
    changed = true;
    return true;
}

StrengthReductionPass::MulDecomposition StrengthReductionPass::decomposeMul(
    int64_t constant) {
    MulDecomposition result;
    result.numOps = 0;

    uint64_t value = static_cast<uint64_t>(constant);
    unsigned setBits = countBits(value);

    bool useSubtraction = false;
    unsigned highestBit = 0;
    if (value > 0) {
        highestBit = 63 - __builtin_clzll(value);
        uint64_t nextPowerOf2 = 1ULL << (highestBit + 1);
        uint64_t diff = nextPowerOf2 - value;
        if (countBits(diff) < setBits && diff != value) {
            useSubtraction = true;
        }
    }

    if (useSubtraction) {
        uint64_t nextPowerOf2 = 1ULL << (highestBit + 1);
        result.operations.push_back({highestBit + 1, false});
        result.numOps = 1;

        uint64_t diff = nextPowerOf2 - value;
        unsigned bitPos = 0;
        while (diff > 0) {
            if (diff & 1) {
                result.operations.push_back({bitPos, true});
                result.numOps++;
            }
            diff >>= 1;
            bitPos++;
        }
    } else {
        unsigned bitPos = 0;
        while (value > 0) {
            if (value & 1) {
                result.operations.push_back({bitPos, false});
                result.numOps++;
            }
            value >>= 1;
            bitPos++;
        }
    }

    return result;
}

unsigned StrengthReductionPass::countBits(uint64_t value) {
    unsigned count = 0;
    while (value) {
        count++;
        value &= value - 1;
    }
    return count;
}

Value* StrengthReductionPass::createMulReplacement(
    Value* operand, const MulDecomposition& decomp, Instruction* insertBefore) {
    IRBuilder builder(insertBefore->getParent());
    builder.setInsertPoint(insertBefore);

    Value* result = nullptr;

    for (const auto& op : decomp.operations) {
        Value* shifted;
        if (op.first == 0) {
            shifted = operand;
        } else {
            auto* shiftAmount = ConstantInt::get(
                dyn_cast<IntegerType>(operand->getType()), op.first);
            auto* shlInst =
                BinaryOperator::Create(Opcode::Shl, operand, shiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(),
                                              shlInst);
            shifted = shlInst;
        }

        if (!result) {
            result = shifted;
        } else {
            if (op.second) {
                result = builder.createSub(result, shifted);
            } else {
                result = builder.createAdd(result, shifted);
            }
        }
    }

    return result;
}

bool StrengthReductionPass::optimizeDivision(BinaryOperator* divInst) {
    Value* dividend = divInst->getOperand(0);
    Value* divisor = divInst->getOperand(1);

    auto* constant = dyn_cast<ConstantInt>(divisor);
    if (!constant) {
        return false;
    }

    bool isSigned = true;
    int64_t divisorValue = static_cast<int64_t>(constant->getValue());

    if (divisorValue == 0) {
        return false;
    }

    if (divisorValue == 1) {
        // x / 1 = x
        divInst->replaceAllUsesWith(dividend);
        divInst->eraseFromParent();
        changed = true;
        return true;
    }

    if (divisorValue == -1) {
        // x / -1 = 0 - x
        IRBuilder builder(divInst->getParent());
        builder.setInsertPoint(divInst);
        auto* zero =
            ConstantInt::get(dyn_cast<IntegerType>(divInst->getType()), 0);
        Value* replacement = builder.createSub(zero, dividend);
        divInst->replaceAllUsesWith(replacement);
        divInst->eraseFromParent();
        changed = true;
        return true;
    }

    Value* replacement =
        createDivReplacement(dividend, divisorValue, isSigned, divInst);

    if (!replacement) {
        return false;
    }

    divInst->replaceAllUsesWith(replacement);
    divInst->eraseFromParent();
    changed = true;
    return true;
}

Value* StrengthReductionPass::createDivReplacement(Value* dividend,
                                                   int64_t divisor,
                                                   bool isSigned,
                                                   Instruction* insertBefore) {
    unsigned totalOps = 0;

    if ((divisor & (divisor - 1)) == 0 && divisor > 0) {
        // For signed division by power of 2, we need 4 operations: extract
        // sign, mask, add, shift For unsigned division by power of 2, we need 1
        // operation: shift
        totalOps = isSigned ? 4 : 1;
    } else {
        totalOps = 3;
    }

    if (totalOps > divThreshold) {
        return nullptr;
    }

    IRBuilder builder(insertBefore->getParent());
    builder.setInsertPoint(insertBefore);

    if ((divisor & (divisor - 1)) == 0 && divisor > 0) {
        unsigned shift = __builtin_ctzll(divisor);
        auto* shiftAmount =
            ConstantInt::get(dyn_cast<IntegerType>(dividend->getType()), shift);

        if (isSigned) {
            // For signed division by power of 2, we need to handle negative
            // dividends correctly Formula: (x + (2^n - 1)) >> n for x < 0, x >>
            // n for x >= 0 We can implement this as: (x + ((x >> 31) & (2^n -
            // 1))) >> n

            auto* intType = dyn_cast<IntegerType>(dividend->getType());
            auto* signBitShift =
                ConstantInt::get(intType, intType->getBitWidth() - 1);
            auto* bias = ConstantInt::get(intType, (1ULL << shift) - 1);

            // Extract sign bit: dividend >> 31
            auto* signBit =
                BinaryOperator::Create(Opcode::Shr, dividend, signBitShift);
            insertBefore->getParent()->insert(insertBefore->getIterator(),
                                              signBit);

            // Mask with bias: (dividend >> 31) & (2^n - 1)
            auto* maskedBias =
                BinaryOperator::Create(Opcode::And, signBit, bias);
            insertBefore->getParent()->insert(insertBefore->getIterator(),
                                              maskedBias);

            // Add bias: dividend + ((dividend >> 31) & (2^n - 1))
            auto* biasedDividend =
                BinaryOperator::Create(Opcode::Add, dividend, maskedBias);
            insertBefore->getParent()->insert(insertBefore->getIterator(),
                                              biasedDividend);

            // Final shift: (dividend + bias) >> n
            auto* shrInst = BinaryOperator::Create(Opcode::Shr, biasedDividend,
                                                   shiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(),
                                              shrInst);
            return shrInst;
        } else {
            auto* shrInst =
                BinaryOperator::Create(Opcode::Shr, dividend, shiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(),
                                              shrInst);
            return shrInst;
        }
    }
    return nullptr;
}

bool StrengthReductionPass::optimizeModulo(BinaryOperator* remInst) {
    Value* dividend = remInst->getOperand(0);
    Value* divisor = remInst->getOperand(1);

    auto* constant = dyn_cast<ConstantInt>(divisor);
    if (!constant) {
        return false;
    }

    int64_t divisorValue = static_cast<int64_t>(constant->getValue());

    if (divisorValue == 0) {
        return false;
    }

    uint64_t absDivisor = static_cast<uint64_t>(std::abs(divisorValue));

    if ((absDivisor & (absDivisor - 1)) != 0) {
        return false;
    }

    IRBuilder builder(remInst->getParent());
    builder.setInsertPoint(remInst);

    auto* intType = dyn_cast<IntegerType>(remInst->getType());
    auto* maskValue = ConstantInt::get(intType, absDivisor - 1);

    // For signed modulo by power of 2, we need the bias+mask+unbias approach
    // Formula: temp = (x + ((x >> 31) & (2^n - 1))) & (2^n - 1); result = temp
    // - ((x >> 31) & (2^n - 1))

    auto* signBitShift = ConstantInt::get(intType, intType->getBitWidth() - 1);

    // Extract sign bit: dividend >> 31 (or 63 for 64-bit)
    auto* signBit = BinaryOperator::Create(Opcode::Shr, dividend, signBitShift);
    remInst->getParent()->insert(remInst->getIterator(), signBit);

    // Get bias: (dividend >> 31) & (2^n - 1)
    auto* bias = BinaryOperator::Create(Opcode::And, signBit, maskValue);
    remInst->getParent()->insert(remInst->getIterator(), bias);

    // Add bias: dividend + bias
    auto* biasedDividend = BinaryOperator::Create(Opcode::Add, dividend, bias);
    remInst->getParent()->insert(remInst->getIterator(), biasedDividend);

    // Apply mask: (dividend + bias) & (2^n - 1)
    auto* maskedResult =
        BinaryOperator::Create(Opcode::And, biasedDividend, maskValue);
    remInst->getParent()->insert(remInst->getIterator(), maskedResult);

    // Remove bias: result - bias
    auto* finalResult = BinaryOperator::Create(Opcode::Sub, maskedResult, bias);
    remInst->getParent()->insert(remInst->getIterator(), finalResult);

    remInst->replaceAllUsesWith(finalResult);
    remInst->eraseFromParent();
    changed = true;
    return true;
}

REGISTER_PASS(StrengthReductionPass, "strength-reduction")

}  // namespace midend