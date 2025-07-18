#pragma once

#include "IR/Constant.h"
#include "IR/Instruction.h"
#include "IR/Type.h"

namespace midend {

class Context;
class ConstantInt;
class BasicBlock;

class ReturnInst : public Instruction {
   private:
    ReturnInst(Context* ctx, Value* retVal = nullptr)
        : Instruction(ctx->getVoidType(), Opcode::Ret, retVal ? 1 : 0) {
        if (retVal) {
            setOperand(0, retVal);
        }
    }

   public:
    static ReturnInst* Create(Context* ctx, Value* retVal = nullptr,
                              BasicBlock* parent = nullptr);

    Value* getReturnValue() const {
        return getNumOperands() > 0 ? getOperand(0) : nullptr;
    }

    Instruction* clone() const override {
        return Create(getType()->getContext(), getReturnValue());
    }

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Ret;
    }
};

class BranchInst : public Instruction {
   private:
    BranchInst(BasicBlock* dest);
    BranchInst(Value* cond, BasicBlock* trueDest, BasicBlock* falseDest);

   public:
    static BranchInst* Create(BasicBlock* dest, BasicBlock* parent = nullptr);
    static BranchInst* Create(Value* cond, BasicBlock* trueDest,
                              BasicBlock* falseDest,
                              BasicBlock* parent = nullptr);

    bool isConstantUnconditional() const {
        return getNumOperands() == 3 && isa<ConstantInt>(getOperand(0));
    }

    bool isSameTarget() const {
        if (getNumOperands() < 2) return false;
        return getOperand(1) == getOperand(2);
    }

    bool isUnconditional() const { return getNumOperands() == 1; }
    bool isConditional() const { return getNumOperands() == 3; }

    Value* getCondition() const {
        return isConditional() ? getOperand(0) : nullptr;
    }

    BasicBlock* getSuccessor(unsigned i) const {
        Value* val = nullptr;
        if (isUnconditional()) {
            val = i == 0 ? getOperand(0) : nullptr;
        } else {
            val = i < 2 ? getOperand(i + 1) : nullptr;
        }
        if (val && val->getValueKind() == ValueKind::BasicBlock) {
            return reinterpret_cast<BasicBlock*>(val);
        }
        return nullptr;
    }

    unsigned getNumSuccessors() const { return isUnconditional() ? 1 : 2; }

    BasicBlock* getTrueBB() const {
        return isConditional() ? getSuccessor(0) : nullptr;
    }

    BasicBlock* getFalseBB() const {
        return isConditional() ? getSuccessor(1) : nullptr;
    }

    BasicBlock* getTargetBB() const {
        if (isConstantUnconditional()) {
            return dyn_cast<ConstantInt>(getOperand(0))->isZero() ? getFalseBB()
                                                                  : getTrueBB();
        }
        if (getNumOperands() == 1 || isSameTarget()) return getSuccessor(0);
        return nullptr;
    }

    void setOperand(unsigned i, Value* v);

    Instruction* clone() const override;

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Br;
    }
};

class PHINode : public Instruction {
   private:
    PHINode(Type* ty, const std::string& name = "")
        : Instruction(ty, Opcode::PHI, 0) {
        setName(name);
    }

   public:
    static PHINode* Create(Type* ty, const std::string& name = "",
                           BasicBlock* parent = nullptr);

    void addIncoming(Value* val, BasicBlock* bb);

    unsigned getNumIncomingValues() const { return getNumOperands() / 2; }

    Value* getIncomingValue(unsigned i) const {
        return i < getNumIncomingValues() ? getOperand(i * 2) : nullptr;
    }

    BasicBlock* getIncomingBlock(unsigned i) const {
        if (i >= getNumIncomingValues()) return nullptr;
        Value* val = getOperand(i * 2 + 1);
        if (val && val->getValueKind() == ValueKind::BasicBlock) {
            return reinterpret_cast<BasicBlock*>(val);
        }
        return nullptr;
    }

    bool isTrival() {
        if (getNumIncomingValues() <= 1) return true;
        for (unsigned i = 1; i < getNumIncomingValues(); ++i) {
            if (getIncomingBlock(i) != getIncomingBlock(0)) {
                return false;
            }
        }
        return true;
    }

    int getBasicBlockIndex(const BasicBlock* bb) const;
    Value* getIncomingValueForBlock(const BasicBlock* bb) const;

    Instruction* clone() const override;

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::PHI;
    }
};

}  // namespace midend