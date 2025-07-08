#pragma once

#include "IR/Instruction.h"
#include "IR/Type.h"

namespace midend {

class UnaryOperator : public Instruction {
   private:
    UnaryOperator(Opcode op, Value* operand, Type* ty,
                  const std::string& name = "")
        : Instruction(ty, op, 1) {
        setOperand(0, operand);
        setName(name);
    }

   public:
    static UnaryOperator* Create(Opcode op, Value* operand,
                                 const std::string& name = "",
                                 BasicBlock* parent = nullptr);

    static UnaryOperator* CreateUAdd(Value* operand,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::UAdd, operand, name, parent);
    }

    static UnaryOperator* CreateUSub(Value* operand,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::USub, operand, name, parent);
    }

    static UnaryOperator* CreateNot(Value* operand,
                                    const std::string& name = "",
                                    BasicBlock* parent = nullptr) {
        return Create(Opcode::Not, operand, name, parent);
    }

    Value* getOperand() const { return User::getOperand(0); }

    Instruction* clone() const override {
        return Create(getOpcode(), getOperand(), getName());
    }

    static bool classof(const Value* v) {
        return isa<Instruction>(v) &&
               static_cast<const Instruction*>(v)->isUnaryOp();
    }
};

class BinaryOperator : public Instruction {
   private:
    BinaryOperator(Opcode op, Value* lhs, Value* rhs, Type* ty,
                   const std::string& name = "")
        : Instruction(ty, op, 2) {
        setOperand(0, lhs);
        setOperand(1, rhs);
        setName(name);
    }

   public:
    static BinaryOperator* Create(Opcode op, Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr);

    static BinaryOperator* CreateAdd(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::Add, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateSub(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::Sub, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateMul(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::Mul, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateDiv(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::Div, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateRem(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::Rem, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateAnd(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::And, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateOr(Value* lhs, Value* rhs,
                                    const std::string& name = "",
                                    BasicBlock* parent = nullptr) {
        return Create(Opcode::Or, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateXor(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::Xor, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateLAnd(Value* lhs, Value* rhs,
                                      const std::string& name = "",
                                      BasicBlock* parent = nullptr) {
        return Create(Opcode::LAnd, lhs, rhs, name, parent);
    }

    static BinaryOperator* CreateLOr(Value* lhs, Value* rhs,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr) {
        return Create(Opcode::LOr, lhs, rhs, name, parent);
    }

    Value* getOperand1() const { return getOperand(0); }
    Value* getOperand2() const { return getOperand(1); }

    Instruction* clone() const override {
        return Create(getOpcode(), getOperand1(), getOperand2(), getName());
    }

    static bool classof(const Value* v) {
        return isa<Instruction>(v) &&
               static_cast<const Instruction*>(v)->isBinaryOp();
    }
};

class CmpInst : public Instruction {
   public:
    enum Predicate {
        ICMP_EQ,
        ICMP_NE,
        ICMP_SLT,
        ICMP_SLE,
        ICMP_SGT,
        ICMP_SGE,
        FCMP_OEQ,
        FCMP_ONE,
        FCMP_OLT,
        FCMP_OLE,
        FCMP_OGT,
        FCMP_OGE,
    };

   private:
    Predicate predicate_;

    CmpInst(Opcode op, Predicate pred, Value* lhs, Value* rhs,
            const std::string& name = "")
        : Instruction(IntegerType::get(lhs->getType()->getContext(), 1), op, 2),
          predicate_(pred) {
        setOperand(0, lhs);
        setOperand(1, rhs);
        setName(name);
    }

   public:
    static CmpInst* Create(Opcode op, Predicate pred, Value* lhs, Value* rhs,
                           const std::string& name = "",
                           BasicBlock* parent = nullptr);

    // Integer comparison convenience methods
    static CmpInst* CreateICmpEQ(Value* lhs, Value* rhs,
                                 const std::string& name = "",
                                 BasicBlock* parent = nullptr) {
        return Create(Opcode::ICmpEQ, ICMP_EQ, lhs, rhs, name, parent);
    }

    static CmpInst* CreateICmpNE(Value* lhs, Value* rhs,
                                 const std::string& name = "",
                                 BasicBlock* parent = nullptr) {
        return Create(Opcode::ICmpNE, ICMP_NE, lhs, rhs, name, parent);
    }

    static CmpInst* CreateICmpSLT(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::ICmpSLT, ICMP_SLT, lhs, rhs, name, parent);
    }

    static CmpInst* CreateICmpSLE(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::ICmpSLE, ICMP_SLE, lhs, rhs, name, parent);
    }

    static CmpInst* CreateICmpSGT(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::ICmpSGT, ICMP_SGT, lhs, rhs, name, parent);
    }

    static CmpInst* CreateICmpSGE(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::ICmpSGE, ICMP_SGE, lhs, rhs, name, parent);
    }

    // Float comparison convenience methods
    static CmpInst* CreateFCmpOEQ(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::FCmpOEQ, FCMP_OEQ, lhs, rhs, name, parent);
    }

    static CmpInst* CreateFCmpONE(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::FCmpONE, FCMP_ONE, lhs, rhs, name, parent);
    }

    static CmpInst* CreateFCmpOLT(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::FCmpOLT, FCMP_OLT, lhs, rhs, name, parent);
    }

    static CmpInst* CreateFCmpOLE(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::FCmpOLE, FCMP_OLE, lhs, rhs, name, parent);
    }

    static CmpInst* CreateFCmpOGT(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::FCmpOGT, FCMP_OGT, lhs, rhs, name, parent);
    }

    static CmpInst* CreateFCmpOGE(Value* lhs, Value* rhs,
                                  const std::string& name = "",
                                  BasicBlock* parent = nullptr) {
        return Create(Opcode::FCmpOGE, FCMP_OGE, lhs, rhs, name, parent);
    }

    Predicate getPredicate() const { return predicate_; }

    Value* getOperand1() const { return getOperand(0); }
    Value* getOperand2() const { return getOperand(1); }

    bool isIntegerComparison() const {
        return getOpcode() >= Opcode::ICmpEQ && getOpcode() <= Opcode::ICmpSGE;
    }

    bool isFloatComparison() const {
        return getOpcode() >= Opcode::FCmpOEQ && getOpcode() <= Opcode::FCmpOGE;
    }

    Instruction* clone() const override {
        return Create(getOpcode(), getPredicate(), getOperand1(), getOperand2(),
                      getName());
    }

    static bool classof(const Value* v) {
        return isa<Instruction>(v) &&
               static_cast<const Instruction*>(v)->isComparison();
    }
};

}  // namespace midend