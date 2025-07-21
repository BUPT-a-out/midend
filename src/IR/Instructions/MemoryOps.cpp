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

std::vector<unsigned> GetElementPtrInst::getStrides() const {
    std::vector<unsigned> strides;
    Type* currentType = sourceElementType_;
    
    // Calculate stride for each dimension
    for (unsigned i = 0; i < getNumIndices(); ++i) {
        if (currentType->isArrayType()) {
            // For array types, stride is the size of the element type
            auto* arrayType = static_cast<ArrayType*>(currentType);
            Type* elemType = arrayType->getElementType();
            unsigned stride = elemType->getSizeInBytes();
            strides.push_back(stride);
            currentType = elemType;
        } else if (currentType->isPointerType()) {
            // For pointer types, stride is the size of pointed-to type
            auto* ptrType = static_cast<PointerType*>(currentType);
            Type* pointedType = ptrType->getElementType();
            unsigned stride = pointedType->getSizeInBytes();
            strides.push_back(stride);
            currentType = pointedType;
        } else {
            // For basic types, stride is the type size itself
            unsigned stride = currentType->getSizeInBytes();
            strides.push_back(stride);
        }
    }
    
    return strides;
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