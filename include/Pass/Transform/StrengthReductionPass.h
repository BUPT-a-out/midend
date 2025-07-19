#pragma once

#include <cstdint>
#include <vector>

#include "Pass/Pass.h"

namespace midend {

class BasicBlock;
class Function;
class BinaryOperator;
class Value;
class Instruction;

class StrengthReductionPass : public FunctionPass {
   public:
    StrengthReductionPass()
        : FunctionPass("StrengthReductionPass",
                       "Strength Reduction Optimization") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

    static unsigned mulThreshold;
    static unsigned divThreshold;

   private:
    bool changed;

    void init();
    bool processInstruction(Instruction* inst);
    bool optimizeMultiplication(BinaryOperator* mulInst);
    bool optimizeDivision(BinaryOperator* divInst);

    struct MulDecomposition {
        std::vector<std::pair<unsigned, bool>> operations;
        unsigned numOps;
    };

    MulDecomposition decomposeMul(int64_t constant);
    unsigned countBits(uint64_t value);
    Value* createMulReplacement(Value* operand, const MulDecomposition& decomp,
                                Instruction* insertBefore);
    
    struct DivMagicNumbers {
        uint64_t magic;
        unsigned preShift;
        unsigned postShift;
        bool add;
    };

    DivMagicNumbers computeUnsignedDivMagic(uint64_t divisor);
    DivMagicNumbers computeSignedDivMagic(int64_t divisor);
    Value* createDivReplacement(Value* dividend, int64_t divisor,
                                bool isSigned, Instruction* insertBefore);
};

}  // namespace midend