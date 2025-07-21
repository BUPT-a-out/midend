#include "IR/Instructions/OtherOps.h"

#include "IR/BasicBlock.h"
#include "IR/Function.h"

namespace midend {

CallInst* CallInst::Create(FunctionType* fnTy, Value* fn,
                           const std::vector<Value*>& args,
                           const std::string& name, BasicBlock* parent) {
    auto* inst = new CallInst(fnTy, fn, args, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

CallInst* CallInst::Create(Function* fn, const std::vector<Value*>& args,
                           const std::string& name, BasicBlock* parent) {
    return Create(fn->getFunctionType(), fn, args, name, parent);
}

Instruction* CallInst::clone() const {
    std::vector<Value*> args;
    for (unsigned i = 0; i < getNumArgOperands(); ++i) {
        args.push_back(getArgOperand(i));
    }
    return Create(getFunctionType(), getCalledValue(), args, getName());
}

CastInst* CastInst::Create(CastOps op, Value* val, Type* destTy,
                           const std::string& name, BasicBlock* parent) {
    auto* inst = new CastInst(op, val, destTy, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

bool CastInst::isCastable(Type* srcTy, Type* destTy) {
    if (srcTy == destTy) return true;

    if (srcTy->isIntegerType() && destTy->isIntegerType()) return true;
    if (srcTy->isFloatType() && destTy->isFloatType()) return true;
    if (srcTy->isPointerType() && destTy->isPointerType()) return true;
    if (srcTy->isIntegerType() && destTy->isPointerType()) return true;
    if (srcTy->isPointerType() && destTy->isIntegerType()) return true;

    return false;
}

CastInst::CastOps CastInst::getCastOpcode(Type* srcTy, Type* destTy) {
    if (srcTy->isIntegerType() && destTy->isIntegerType()) {
        auto* srcInt = static_cast<IntegerType*>(srcTy);
        auto* destInt = static_cast<IntegerType*>(destTy);
        if (srcInt->getBitWidth() > destInt->getBitWidth()) {
            return Trunc;
        } else if (srcInt->getBitWidth() < destInt->getBitWidth()) {
            return SExt;
        }
    }

    if (srcTy->isPointerType() && destTy->isIntegerType()) {
        return PtrToInt;
    }

    if (srcTy->isIntegerType() && destTy->isPointerType()) {
        return IntToPtr;
    }

    return BitCast;
}

MoveInst* MoveInst::Create(Value* val, const std::string& name,
                           BasicBlock* parent) {
    auto* inst = new MoveInst(val, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

}  // namespace midend