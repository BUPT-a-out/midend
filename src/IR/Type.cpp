#include "IR/Type.h"

#include <iostream>
#include <sstream>

#include "IR/Value.h"

namespace midend {

IntegerType* IntegerType::get(Context* ctx, unsigned bits) {
    return ctx->getIntegerType(bits);
}

FloatType* FloatType::get(Context* ctx) { return ctx->getFloatType(); }

VoidType* VoidType::get(Context* ctx) { return ctx->getVoidType(); }

LabelType* LabelType::get(Context* ctx) { return ctx->getLabelType(); }

PointerType* PointerType::get(Type* elemType) {
    return elemType->getContext()->getPointerType(elemType);
}

ArrayType* ArrayType::get(Type* elemType, uint64_t numElems) {
    return elemType->getContext()->getArrayType(elemType, numElems);
}

FunctionType* FunctionType::get(Type* retType, std::vector<Type*> params,
                                bool varArg) {
    return new FunctionType(retType, std::move(params), varArg,
                            retType->getContext());
}

std::string FunctionType::toString() const {
    std::stringstream ss;
    ss << returnType_->toString() << " (";
    for (size_t i = 0; i < paramTypes_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << paramTypes_[i]->toString();
    }
    if (isVarArg_) {
        if (!paramTypes_.empty()) ss << ", ";
        ss << "...";
    }
    ss << ")";
    return ss.str();
}

Context::Context() {
    voidType_.reset(new VoidType(this));
    floatType_.reset(new FloatType(this));
    labelType_.reset(new LabelType(this));
}

VoidType* Context::getVoidType() { return voidType_.get(); }

IntegerType* Context::getIntegerType(unsigned bits) {
    if (bits != 1 && bits != 32) {
        std::cerr << "Warning: unsupported integer type bits: " << bits
                  << ". Only 1 and 32 bits are supported." << std::endl;
    }
    auto& type = integerTypes_[bits];
    if (!type) {
        type.reset(new IntegerType(bits, this));
    }
    return type.get();
}

FloatType* Context::getFloatType() { return floatType_.get(); }

LabelType* Context::getLabelType() { return labelType_.get(); }

PointerType* Context::getPointerType(Type* elemType) {
    auto& type = pointerTypes_[elemType];
    if (!type) {
        type.reset(new PointerType(elemType, this));
    }
    return type.get();
}

ArrayType* Context::getArrayType(Type* elemType, uint64_t numElems) {
    std::string key =
        elemType->toString() + "[" + std::to_string(numElems) + "]";
    auto& type = arrayTypes_[key];
    if (!type) {
        type.reset(new ArrayType(elemType, numElems, this));
    }
    return type.get();
}

PointerType* IntegerType::getPointerTo() { return PointerType::get(this); }

PointerType* FloatType::getPointerTo() { return PointerType::get(this); }

PointerType* VoidType::getPointerTo() { return PointerType::get(this); }

Type* Type::getSingleElementType() const {
    if (isArrayType()) {
        auto* arrayType = static_cast<const ArrayType*>(this);
        return arrayType->getElementType();
    }
    if (isPointerType()) {
        auto* ptrType = static_cast<const PointerType*>(this);
        return ptrType->getElementType();
    }
    return const_cast<Type*>(this);
}

Type* Type::getBaseElementType() const {
    if (isArrayType()) {
        auto* arrayType = static_cast<const ArrayType*>(this);
        return arrayType->getElementType()->getBaseElementType();
    }
    if (isPointerType()) {
        auto* ptrType = static_cast<const PointerType*>(this);
        return ptrType->getElementType()->getBaseElementType();
    }
    return const_cast<Type*>(this);
}

Type* Type::getMultiLevelElementType(unsigned levels) const {
    Type* current = const_cast<Type*>(this);
    for (unsigned i = 0; i < levels; ++i) {
        current = current->getSingleElementType();
    }
    return current;
}

unsigned Type::getSizeInBytes() const {
    if (isIntegerType()) {
        auto* intTy = static_cast<const IntegerType*>(this);
        return (intTy->getBitWidth() + 7) / 8;
    }
    if (isFloatType()) {
        return 4;  // float = 4 bytes
    }
    if (isPointerType()) {
        return 8;  // pointer = 8 bytes (64-bit)
    }
    if (isArrayType()) {
        auto* arrayTy = static_cast<const ArrayType*>(this);
        return arrayTy->getNumElements() *
               arrayTy->getElementType()->getSizeInBytes();
    }
    // For other types (void, label, etc.), return 0
    return 0;
}

}  // namespace midend