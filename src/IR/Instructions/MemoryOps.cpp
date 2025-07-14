#include "IR/Instructions/MemoryOps.h"

#include "IR/BasicBlock.h"
#include "IR/Type.h"

namespace midend {

AllocaInst* AllocaInst::Create(Type* ty, Value* arraySize,
                               const std::string& name, BasicBlock* parent) {
    auto* inst = new AllocaInst(ty, arraySize, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

LoadInst* LoadInst::Create(Value* ptr, const std::string& name,
                           BasicBlock* parent) {
    auto* inst = new LoadInst(ptr, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

StoreInst* StoreInst::Create(Value* val, Value* ptr, BasicBlock* parent) {
    auto* inst = new StoreInst(val, ptr);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

GetElementPtrInst* GetElementPtrInst::Create(Type* pointeeType, Value* ptr,
                                             const std::vector<Value*>& indices,
                                             const std::string& name,
                                             BasicBlock* parent) {
    auto* inst = new GetElementPtrInst(pointeeType, ptr, indices, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

Instruction* GetElementPtrInst::clone() const {
    std::vector<Value*> indices;
    for (unsigned i = 0; i < getNumIndices(); ++i) {
        indices.push_back(getIndex(i));
    }
    return Create(getSourceElementType(), getPointerOperand(), indices,
                  getName());
}

}  // namespace midend