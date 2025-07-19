#include "Pass/Transform/StrengthReductionPass.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Type.h"
#include "Support/Casting.h"

namespace midend {

unsigned StrengthReductionPass::mulThreshold = 3;
unsigned StrengthReductionPass::divThreshold = 4;

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

void StrengthReductionPass::init() {
    changed = false;
}

bool StrengthReductionPass::processInstruction(Instruction* inst) {
    if (auto* binaryOp = dyn_cast<BinaryOperator>(inst)) {
        switch (binaryOp->getOpcode()) {
            case Opcode::Mul:
                return optimizeMultiplication(binaryOp);
            case Opcode::Div:
                return optimizeDivision(binaryOp);
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
    
    if (constValue == 0 || constValue == 1 || constValue == -1) {
        return false;
    }

    MulDecomposition decomp = decomposeMul(std::abs(constValue));
    
    if (decomp.numOps > mulThreshold) {
        return false;
    }

    Value* replacement = createMulReplacement(variable, decomp, mulInst);
    
    if (constValue < 0) {
        IRBuilder builder(mulInst->getParent());
        builder.setInsertPoint(mulInst);
        auto* zero = ConstantInt::get(dyn_cast<IntegerType>(replacement->getType()), 0);
        replacement = builder.createSub(zero, replacement);
    }

    mulInst->replaceAllUsesWith(replacement);
    mulInst->eraseFromParent();
    changed = true;
    return true;
}

StrengthReductionPass::MulDecomposition 
StrengthReductionPass::decomposeMul(int64_t constant) {
    MulDecomposition result;
    result.numOps = 0;
    
    uint64_t value = static_cast<uint64_t>(constant);
    unsigned setBits = countBits(value);
    
    // Check if we should use subtraction (e.g., 15 = 16 - 1)
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
        // Use subtraction form (e.g., 15 = 16 - 1)
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
        // Use addition form (e.g., 10 = 8 + 2)
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
            auto* shiftAmount = ConstantInt::get(dyn_cast<IntegerType>(operand->getType()), op.first);
            auto* shlInst = BinaryOperator::Create(Opcode::Shl, operand, shiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(), shlInst);
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
    
    bool isSigned = false;
    int64_t divisorValue = static_cast<int64_t>(constant->getValue());
    
    if (divisorValue == 0 || divisorValue == 1 || 
        (isSigned && divisorValue == -1)) {
        return false;
    }
    
    Value* replacement = createDivReplacement(dividend, divisorValue, 
                                              isSigned, divInst);
    
    if (!replacement) {
        return false;
    }
    
    divInst->replaceAllUsesWith(replacement);
    divInst->eraseFromParent();
    changed = true;
    return true;
}

StrengthReductionPass::DivMagicNumbers 
StrengthReductionPass::computeUnsignedDivMagic(uint64_t divisor) {
    DivMagicNumbers result;
    result.preShift = 0;
    result.add = false;
    
    unsigned bitWidth = 64;
    
    if ((divisor & (divisor - 1)) == 0) {
        result.magic = 0;
        result.postShift = __builtin_ctzll(divisor);
        return result;
    }
    
    if ((divisor & 1) == 0) {
        result.preShift = __builtin_ctzll(divisor);
        divisor >>= result.preShift;
    }
    
    uint64_t nc = -1ULL - (-1ULL) % divisor;
    unsigned p = bitWidth - 1;
    uint64_t q1 = 0x8000000000000000ULL / nc;
    uint64_t r1 = 0x8000000000000000ULL - q1 * nc;
    uint64_t q2 = 0x7FFFFFFFFFFFFFFFULL / divisor;
    uint64_t r2 = 0x7FFFFFFFFFFFFFFFULL - q2 * divisor;
    
    do {
        p++;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= nc) {
            q1++;
            r1 -= nc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= divisor) {
            q2++;
            r2 -= divisor;
        }
    } while (q1 < (divisor - r2));
    
    result.magic = q2 + 1;
    result.postShift = p - bitWidth;
    result.add = true;
    
    return result;
}

StrengthReductionPass::DivMagicNumbers 
StrengthReductionPass::computeSignedDivMagic(int64_t divisor) {
    DivMagicNumbers result;
    result.preShift = 0;
    result.add = false;
    
    unsigned bitWidth = 64;
    int64_t absDivisor = std::abs(divisor);
    
    if ((absDivisor & (absDivisor - 1)) == 0) {
        result.magic = 0;
        result.postShift = __builtin_ctzll(absDivisor);
        return result;
    }
    
    int64_t signedMin = std::numeric_limits<int64_t>::min();
    int64_t nc = signedMin + (divisor >> (bitWidth - 1));
    int64_t anc = nc - 1 - nc % absDivisor;
    unsigned p = bitWidth - 1;
    int64_t q1 = signedMin / anc;
    int64_t r1 = signedMin - q1 * anc;
    int64_t q2 = signedMin / absDivisor;
    int64_t r2 = signedMin - q2 * absDivisor;
    
    do {
        p++;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= anc) {
            q1++;
            r1 -= anc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= absDivisor) {
            q2++;
            r2 -= absDivisor;
        }
    } while (q1 < (absDivisor - r2));
    
    result.magic = q2 + 1;
    if (divisor < 0) {
        result.magic = -result.magic;
    }
    result.postShift = p - bitWidth;
    
    return result;
}

Value* StrengthReductionPass::createDivReplacement(
    Value* dividend, int64_t divisor, bool isSigned, Instruction* insertBefore) {
    
    unsigned totalOps = 0;
    
    if ((divisor & (divisor - 1)) == 0 && divisor > 0) {
        totalOps = 1;
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
        auto* shiftAmount = ConstantInt::get(dyn_cast<IntegerType>(dividend->getType()), shift);
        if (isSigned) {
            auto* shrInst = BinaryOperator::Create(Opcode::Shr, dividend, shiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(), shrInst);
            return shrInst;
        } else {
            auto* shrInst = BinaryOperator::Create(Opcode::Shr, dividend, shiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(), shrInst);
            return shrInst;
        }
    }
    
    DivMagicNumbers magic;
    if (isSigned) {
        magic = computeSignedDivMagic(divisor);
    } else {
        magic = computeUnsignedDivMagic(divisor);
    }
    
    Value* result = dividend;
    
    if (magic.preShift > 0) {
        auto* preShiftAmount = ConstantInt::get(dyn_cast<IntegerType>(dividend->getType()), 
                                                 magic.preShift);
        auto* shrInst = BinaryOperator::Create(Opcode::Shr, result, preShiftAmount);
        insertBefore->getParent()->insert(insertBefore->getIterator(), shrInst);
        result = shrInst;
    }
    
    if (magic.magic != 0) {
        auto* magicConst = ConstantInt::get(dyn_cast<IntegerType>(dividend->getType()), magic.magic);
        Value* mulHigh;
        
        if (isSigned) {
            mulHigh = builder.createMul(result, magicConst);
        } else {
            mulHigh = builder.createMul(result, magicConst);
        }
        
        if (magic.add) {
            result = builder.createAdd(mulHigh, result);
        } else {
            result = mulHigh;
        }
    }
    
    if (magic.postShift > 0) {
        auto* postShiftAmount = ConstantInt::get(dyn_cast<IntegerType>(dividend->getType()),
                                                  magic.postShift);
        if (isSigned) {
            auto* shrInst = BinaryOperator::Create(Opcode::Shr, result, postShiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(), shrInst);
            result = shrInst;
        } else {
            auto* shrInst = BinaryOperator::Create(Opcode::Shr, result, postShiftAmount);
            insertBefore->getParent()->insert(insertBefore->getIterator(), shrInst);
            result = shrInst;
        }
    }
    
    if (isSigned && divisor < 0) {
        result = builder.createUSub(result);
    }
    
    return result;
}

REGISTER_PASS(StrengthReductionPass, "strength-reduction")

}  // namespace midend