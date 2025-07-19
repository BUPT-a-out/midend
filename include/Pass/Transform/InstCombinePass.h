#pragma once

#include "Pass/Pass.h"

namespace midend {

class Function;
class Instruction;
class Value;
class BinaryOperator;
class UnaryOperator;
class CmpInst;

class InstCombinePass : public FunctionPass {
   public:
    InstCombinePass()
        : FunctionPass("InstCombinePass", "Instruction Combining") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    bool combineInstructions(Function& function);
    Value* simplifyBinaryOp(BinaryOperator* binOp);
    Value* simplifyUnaryOp(UnaryOperator* unaryOp);
    Value* simplifyCmpInst(CmpInst* cmpInst);

    Value* simplifyAdd(Value* lhs, Value* rhs);
    Value* simplifySub(Value* lhs, Value* rhs);
    Value* simplifyMul(Value* lhs, Value* rhs);
    Value* simplifyDiv(Value* lhs, Value* rhs);
    Value* simplifyRem(Value* lhs, Value* rhs);
    Value* simplifyAnd(Value* lhs, Value* rhs);
    Value* simplifyOr(Value* lhs, Value* rhs);
    Value* simplifyXor(Value* lhs, Value* rhs);

    bool changed_;
};

}  // namespace midend