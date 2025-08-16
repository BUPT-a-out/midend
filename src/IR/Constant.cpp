#include "IR/Constant.h"

#include <map>
#include <stdexcept>

namespace midend {

static std::map<std::pair<IntegerType*, uint64_t>, ConstantInt*> integerCache;
static std::map<std::pair<FloatType*, float>, ConstantFP*> fpCache;
static std::map<PointerType*, ConstantPointerNull*> nullPtrCache;
static std::map<Type*, UndefValue*> undefCache;

ConstantInt* ConstantInt::get(IntegerType* ty, uint32_t val) {
    auto key = std::make_pair(ty, val);
    auto it = integerCache.find(key);
    if (it != integerCache.end()) {
        return it->second;
    }
    auto* constant = new ConstantInt(ty, val);
    integerCache[key] = constant;
    return constant;
}

ConstantInt* ConstantInt::get(Context* ctx, unsigned bitWidth, uint32_t val) {
    return get(ctx->getIntegerType(bitWidth), val);
}

ConstantInt* ConstantInt::getTrue(Context* ctx) {
    return get(ctx->getInt1Type(), 1);
}

ConstantInt* ConstantInt::getFalse(Context* ctx) {
    return get(ctx->getInt1Type(), 0);
}

ConstantFP* ConstantFP::get(FloatType* ty, float val) {
    auto key = std::make_pair(ty, val);
    auto it = fpCache.find(key);
    if (it != fpCache.end()) {
        return it->second;
    }
    auto* constant = new ConstantFP(ty, val);
    fpCache[key] = constant;
    return constant;
}

ConstantFP* ConstantFP::get(Context* ctx, float val) {
    return get(ctx->getFloatType(), val);
}

ConstantPointerNull* ConstantPointerNull::get(PointerType* ty) {
    auto it = nullPtrCache.find(ty);
    if (it != nullPtrCache.end()) {
        return it->second;
    }
    auto* constant = new ConstantPointerNull(ty);
    nullPtrCache[ty] = constant;
    return constant;
}

ConstantArray* ConstantArray::get(ArrayType* ty,
                                  std::vector<Constant*> elements) {
    return new ConstantArray(ty, std::move(elements));
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr, Type* indexType,
                              size_t index) {
    if (!arr || !indexType) return nullptr;

    size_t flatIndex = index;
    if (auto* arrayType = dyn_cast<ArrayType>(indexType)) {
        Type* baseType = arr->getType()->getBaseElementType();
        size_t elementStride = arrayType->getElementType()->getSizeInBytes() /
                               baseType->getSizeInBytes();
        flatIndex = index * elementStride;
    }

#ifdef A_OUT_DEBUG
    // Check bound
    size_t totalElements = arr->getNumElements();
    if (flatIndex >= totalElements) {
        throw std::runtime_error("ConstantGEP: index " +
                                 std::to_string(flatIndex) +
                                 " out of bounds for array of size " +
                                 std::to_string(totalElements));
    }
#endif

    auto* elemPtrTy = PointerType::get(arr->getType()->getBaseElementType());
    return new ConstantGEP(elemPtrTy, arr, indexType, flatIndex);
}

ConstantGEP* ConstantGEP::get(ConstantGEP* base, size_t index) {
    if (!base) return nullptr;

    Type* newIndexType = nullptr;
    size_t additionalOffset = 0;

    if (auto* arrayType = dyn_cast<ArrayType>(base->getIndexType())) {
        newIndexType = arrayType->getElementType();

        if (auto* elemArrayType = dyn_cast<ArrayType>(newIndexType)) {
            size_t stride =
                elemArrayType->getSizeInBytes() / base->getArray()
                                                      ->getType()
                                                      ->getBaseElementType()
                                                      ->getSizeInBytes();
            additionalOffset = index * stride;
        } else {
            additionalOffset = index;
        }
    } else {
        return nullptr;
    }

    size_t newFlatIndex = base->getIndex() + additionalOffset;

#ifdef A_OUT_DEBUG
    // Check bounds
    size_t totalElements = base->getArray()->getNumElements();
    if (newFlatIndex >= totalElements) {
        throw std::runtime_error("ConstantGEP: index " +
                                 std::to_string(newFlatIndex) +
                                 " out of bounds for array of size " +
                                 std::to_string(totalElements));
    }
#endif

    auto* elemPtrTy =
        PointerType::get(base->getArray()->getType()->getBaseElementType());
    return new ConstantGEP(elemPtrTy, base->getArray(), newIndexType,
                           newFlatIndex, base->getArrayPointer());
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr, Type* indexType,
                              ConstantInt* indexConst) {
    if (!arr || !indexType || !indexConst) return nullptr;
    int32_t signedIdx = indexConst->getSignedValue();
    if (signedIdx < 0) return nullptr;
    return get(arr, indexType, static_cast<size_t>(signedIdx));
}

ConstantGEP* ConstantGEP::get(ConstantGEP* base, ConstantInt* indexConst) {
    if (!base || !indexConst) return nullptr;
    int32_t signedIdx = indexConst->getSignedValue();
    if (signedIdx < 0) return nullptr;
    return get(base, static_cast<size_t>(signedIdx));
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr, Type* indexType,
                              const std::vector<size_t>& indices) {
    if (!arr || !indexType) return nullptr;
    ConstantGEP* current = nullptr;
    for (size_t i = 0; i < indices.size(); ++i) {
        current = (i == 0) ? get(arr, indexType, indices[i])
                           : get(current, indices[i]);
        if (!current) return nullptr;
    }
    return current;
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr, Type* indexType,
                              const std::vector<ConstantInt*>& indices) {
    if (!arr || !indexType) return nullptr;
    if (indices.empty()) return nullptr;
    size_t flatIndex = 0;
    Type* currentType = indexType;
    Type* baseType = arr->getType()->getBaseElementType();

    for (size_t i = 0; i < indices.size(); ++i) {
        if (!indices[i]) return nullptr;
        int32_t idx = indices[i]->getSignedValue();
        if (idx < 0) return nullptr;

        if (auto* arrayType = dyn_cast<ArrayType>(currentType)) {
            Type* elementType = arrayType->getElementType();
            size_t elementStride =
                elementType->getSizeInBytes() / baseType->getSizeInBytes();
            flatIndex += idx * elementStride;

            currentType = elementType;
        } else {
            if (i != indices.size() - 1) return nullptr;
            flatIndex += idx;
        }
    }

#ifdef A_OUT_DEBUG
    size_t totalElements = arr->getNumElements();
    if (flatIndex >= totalElements) {
        throw std::runtime_error("ConstantGEP: index " +
                                 std::to_string(flatIndex) +
                                 " out of bounds for array of size " +
                                 std::to_string(totalElements));
    }
#endif

    auto* elemPtrTy = PointerType::get(baseType);
    return new ConstantGEP(elemPtrTy, arr, currentType, flatIndex);
}

ConstantGEP* ConstantGEP::get(ConstantGEP* base,
                              const std::vector<size_t>& indices) {
    if (!base) return nullptr;
    ConstantGEP* current = base;
    for (size_t idx : indices) {
        current = get(current, idx);
        if (!current) return nullptr;
    }
    return current;
}

ConstantGEP* ConstantGEP::get(ConstantGEP* base,
                              const std::vector<ConstantInt*>& indices) {
    if (!base) return nullptr;
    ConstantGEP* current = base;
    for (auto* ci : indices) {
        if (!ci) return nullptr;
        int32_t idx = ci->getSignedValue();
        if (idx < 0) return nullptr;
        current = get(current, static_cast<size_t>(idx));
        if (!current) return nullptr;
    }
    return current;
}

void ConstantGEP::setElementValue(Value* newValue) {
    auto* newConst = dyn_cast<Constant>(newValue);
#ifdef A_OUT_DEBUG
    if (!array_) throw std::runtime_error("ConstantGEP: null array");
    Constant* cur = array_->getElement(index_);
    if (!cur) throw std::runtime_error("ConstantGEP: index out of range");

    if (!isa<ConstantInt>(cur) && !isa<ConstantFP>(cur)) {
        throw std::runtime_error(
            "ConstantGEP: only ConstantInt or ConstantFP element can be "
            "updated");
    }

    if (!newValue || newValue->getType() != cur->getType()) {
        throw std::runtime_error(
            "ConstantGEP: type mismatch when updating element value");
    }

    if (!newConst) {
        throw std::runtime_error("ConstantGEP: new value must be a Constant");
    }
#endif
    array_->setElement(index_, newConst);
}

ConstantExpr* ConstantExpr::getAdd(Constant* lhs, Constant* rhs) {
    if (lhs->getType() != rhs->getType()) {
        throw std::runtime_error(
            "Operands must be of the same type for `ConstantExpr::getAdd`");
    }
    return new ConstantExpr(lhs->getType(), Opcode::Add, {lhs, rhs});
}

ConstantExpr* ConstantExpr::getSub(Constant* lhs, Constant* rhs) {
    if (lhs->getType() != rhs->getType()) {
        throw std::runtime_error(
            "Operands must be of the same type for `ConstantExpr::getSub`");
    }
    return new ConstantExpr(lhs->getType(), Opcode::Sub, {lhs, rhs});
}

ConstantExpr* ConstantExpr::getMul(Constant* lhs, Constant* rhs) {
    if (lhs->getType() != rhs->getType()) {
        throw std::runtime_error(
            "Operands must be of the same type for `ConstantExpr::getMul`");
    }
    return new ConstantExpr(lhs->getType(), Opcode::Mul, {lhs, rhs});
}

UndefValue* UndefValue::get(Type* ty) {
    auto it = undefCache.find(ty);
    if (it != undefCache.end()) {
        return it->second;
    }
    auto* constant = new UndefValue(ty);
    undefCache[ty] = constant;
    return constant;
}

}  // namespace midend