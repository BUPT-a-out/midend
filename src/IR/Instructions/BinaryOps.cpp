#include "IR/Instructions/BinaryOps.h"

#include "IR/BasicBlock.h"

namespace midend {

UnaryOperator* UnaryOperator::Create(Opcode op, Value* operand,
                                     const std::string& name,
                                     BasicBlock* parent) {
    Type* resultType = operand->getType();

    if (op == Opcode::Not) {
        resultType = IntegerType::get(operand->getType()->getContext(), 1);
    }

    auto* inst = new UnaryOperator(op, operand, resultType, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

BinaryOperator* BinaryOperator::Create(Opcode op, Value* lhs, Value* rhs,
                                       const std::string& name,
                                       BasicBlock* parent) {
    Type* resultType = lhs->getType();

    if (op == Opcode::LAnd || op == Opcode::LOr) {
        resultType = IntegerType::get(lhs->getType()->getContext(), 1);
    }

    auto* inst = new BinaryOperator(op, lhs, rhs, resultType, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

CmpInst* CmpInst::Create(Opcode op, Predicate pred, Value* lhs, Value* rhs,
                         const std::string& name, BasicBlock* parent) {
    auto* inst = new CmpInst(op, pred, lhs, rhs, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

}  // namespace midend