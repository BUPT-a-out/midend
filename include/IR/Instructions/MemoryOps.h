#pragma once

#include "IR/Instruction.h"
#include "IR/Type.h"

namespace midend {

class AllocaInst : public Instruction {
   private:
    Type* allocatedType_;
    Value* arraySize_;

    AllocaInst(Type* ty, Value* arraySize, const std::string& name = "")
        : Instruction(PointerType::get(ty), Opcode::Alloca, arraySize ? 1 : 0),
          allocatedType_(ty),
          arraySize_(arraySize) {
        if (arraySize) {
            setOperand(0, arraySize);
        }
        setName(name);
    }

   public:
    static AllocaInst* Create(Type* ty, Value* arraySize = nullptr,
                              const std::string& name = "",
                              BasicBlock* parent = nullptr);

    Type* getAllocatedType() const { return allocatedType_; }

    bool isArrayAllocation() const { return arraySize_ != nullptr; }
    Value* getArraySize() const { return arraySize_; }

    Instruction* clone() const override {
        return Create(getAllocatedType(), getArraySize(), getName());
    }

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() ==
               Opcode::Alloca;
    }
};

class LoadInst : public Instruction {
   private:
    LoadInst(Value* ptr, const std::string& name = "")
        : Instruction(
              static_cast<PointerType*>(ptr->getType())->getElementType(),
              Opcode::Load, 1) {
        setOperand(0, ptr);
        setName(name);
    }

   public:
    static LoadInst* Create(Value* ptr, const std::string& name = "",
                            BasicBlock* parent = nullptr);

    Value* getPointerOperand() const { return getOperand(0); }

    Instruction* clone() const override {
        return Create(getPointerOperand(), getName());
    }

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Load;
    }
};

class StoreInst : public Instruction {
   private:
    StoreInst(Value* val, Value* ptr)
        : Instruction(val->getType()->getContext()->getVoidType(),
                      Opcode::Store, 2) {
        setOperand(0, val);
        setOperand(1, ptr);
    }

   public:
    static StoreInst* Create(Value* val, Value* ptr,
                             BasicBlock* parent = nullptr);

    Value* getValueOperand() const { return getOperand(0); }
    Value* getPointerOperand() const { return getOperand(1); }

    Instruction* clone() const override {
        return Create(getValueOperand(), getPointerOperand());
    }

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() == Opcode::Store;
    }
};

class GetElementPtrInst : public Instruction {
   private:
    Type* sourceElementType_;

    GetElementPtrInst(Type* pointeeType, Value* ptr,
                      const std::vector<Value*>& indices,
                      const std::string& name = "")
        : Instruction(
              ptr ? PointerType::get(
                        pointeeType->getMultiLevelElementType(indices.size()))
                  : nullptr,
              Opcode::GetElementPtr, 1 + indices.size()),
          sourceElementType_(pointeeType) {
        setOperand(0, ptr);
        for (size_t i = 0; i < indices.size(); ++i) {
            setOperand(i + 1, indices[i]);
        }
        setName(name);
    }

   public:
    static GetElementPtrInst* Create(Type* pointeeType, Value* ptr,
                                     const std::vector<Value*>& indices,
                                     const std::string& name = "",
                                     BasicBlock* parent = nullptr);

    Type* getSourceElementType() const { return sourceElementType_; }
    Value* getPointerOperand() const { return getOperand(0); }

    unsigned getNumIndices() const { return getNumOperands() - 1; }

    Value* getIndex(unsigned i) const {
        return i < getNumIndices() ? getOperand(i + 1) : nullptr;
    }
    
    // Get stride for each dimension (for backend code generation)
    std::vector<unsigned> getStrides() const;

    Instruction* clone() const override;

    static bool classof(const Value* v) {
        if (!isa<Instruction>(v)) return false;
        return static_cast<const Instruction*>(v)->getOpcode() ==
               Opcode::GetElementPtr;
    }
};

}  // namespace midend