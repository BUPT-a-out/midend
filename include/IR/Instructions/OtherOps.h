#pragma once

#include <vector>

#include "IR/Instruction.h"
#include "IR/Type.h"

namespace midend {

class Function;
class FunctionType;

class CallInst : public Instruction {
   private:
    FunctionType* functionType_;

    CallInst(FunctionType* fnTy, Value* fn, const std::vector<Value*>& args,
             const std::string& name = "")
        : Instruction(fnTy->getReturnType(), Opcode::Call, 1 + args.size()),
          functionType_(fnTy) {
        setOperand(0, fn);
        for (size_t i = 0; i < args.size(); ++i) {
            setOperand(i + 1, args[i]);
        }
        setName(name);
    }

   public:
    static CallInst* Create(FunctionType* fnTy, Value* fn,
                            const std::vector<Value*>& args,
                            const std::string& name = "",
                            BasicBlock* parent = nullptr);

    static CallInst* Create(Function* fn, const std::vector<Value*>& args,
                            const std::string& name = "",
                            BasicBlock* parent = nullptr);

    FunctionType* getFunctionType() const { return functionType_; }

    Value* getCalledValue() const { return getOperand(0); }
    Function* getCalledFunction() const {
        Value* val = getCalledValue();
        if (val && val->getValueKind() == ValueKind::Function) {
            return reinterpret_cast<Function*>(val);
        }
        return nullptr;
    }

    unsigned getNumArgOperands() const { return getNumOperands() - 1; }

    Value* getArgOperand(unsigned i) const {
        return i < getNumArgOperands() ? getOperand(i + 1) : nullptr;
    }

    Instruction* clone() const override;

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Call;
    }
};

class CastInst : public Instruction {
   public:
    enum CastOps {
        Trunc,     // Truncate integers
        ZExt,      // Zero extend integers
        SExt,      // Sign extend integers
        FPToSI,    // FP to signed integer
        SIToFP,    // Signed integer to FP
        PtrToInt,  // Pointer to integer
        IntToPtr,  // Integer to pointer
        BitCast,   // Type cast
    };

   private:
    CastOps castOp_;

    CastInst(CastOps op, Value* val, Type* destTy, const std::string& name = "")
        : Instruction(destTy, Opcode::Cast, 1), castOp_(op) {
        setOperand(0, val);
        setName(name);
    }

   public:
    static CastInst* Create(CastOps op, Value* val, Type* destTy,
                            const std::string& name = "",
                            BasicBlock* parent = nullptr);

    CastOps getCastOpcode() const { return castOp_; }

    Type* getSrcType() const { return getOperand(0)->getType(); }
    Type* getDestType() const { return getType(); }

    static bool isCastable(Type* srcTy, Type* destTy);
    static CastOps getCastOpcode(Type* srcTy, Type* destTy);

    Instruction* clone() const override {
        return Create(getCastOpcode(), getOperand(0), getType(), getName());
    }

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Cast;
    }
};

class MoveInst : public Instruction {
   private:
    MoveInst(Value* val, const std::string& name = "")
        : Instruction(val->getType(), Opcode::Move, 1) {
        setOperand(0, val);
        setName(name);
    }

   public:
    static MoveInst* Create(Value* val, const std::string& name = "",
                            BasicBlock* parent = nullptr);

    Value* getValue() const { return getOperand(0); }

    Instruction* clone() const override {
        return Create(getValue(), getName());
    }

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Move;
    }
};

}  // namespace midend