#include "IR/Constant.h"

#include <functional>
#include <map>
#include <stdexcept>

#include "flat_hash_map/unordered_map.hpp"

namespace std {
template <typename T1, typename T2>
struct hash<std::pair<T1*, T2>> {
    size_t operator()(const std::pair<T1*, T2>& p) const {
        auto h1 = std::hash<void*>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};
}  // namespace std

namespace midend {

static ska::unordered_map<std::pair<IntegerType*, uint64_t>, ConstantInt*>
    integerCache;
static ska::unordered_map<std::pair<IntegerType*, uint64_t>,
                          std::shared_ptr<ConstantInt>>
    sharedIntegerCache;
static ska::unordered_map<std::pair<FloatType*, float>, ConstantFP*> fpCache;
static ska::unordered_map<std::pair<FloatType*, float>,
                          std::shared_ptr<ConstantFP>>
    sharedFPCache;
static ska::unordered_map<PointerType*, ConstantPointerNull*> nullPtrCache;
static ska::unordered_map<Type*, UndefValue*> undefCache;

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

std::shared_ptr<ConstantInt> ConstantInt::getShared(IntegerType* ty,
                                                    uint32_t val) {
    auto key = std::make_pair(ty, val);
    auto it = sharedIntegerCache.find(key);
    if (it != sharedIntegerCache.end()) {
        return it->second;
    }
    auto* constant = new ConstantInt(ty, val);
    sharedIntegerCache[key] = std::shared_ptr<ConstantInt>(constant);
    return sharedIntegerCache[key];
}

ConstantInt* ConstantInt::get(Context* ctx, unsigned bitWidth, uint32_t val) {
    return get(ctx->getIntegerType(bitWidth), val);
}

ConstantInt* ConstantInt::getTrue(Context* ctx) {
    return get(ctx->getInt1Type(), 1);
}

std::shared_ptr<ConstantInt> ConstantInt::getTrueShared(Context* ctx) {
    return getShared(ctx->getInt1Type(), 1);
}

ConstantInt* ConstantInt::getFalse(Context* ctx) {
    return get(ctx->getInt1Type(), 0);
}

std::shared_ptr<ConstantInt> ConstantInt::getFalseShared(Context* ctx) {
    return getShared(ctx->getInt1Type(), 0);
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

std::shared_ptr<ConstantFP> ConstantFP::getShared(FloatType* ty, float val) {
    auto key = std::make_pair(ty, val);
    auto it = sharedFPCache.find(key);
    if (it != sharedFPCache.end()) {
        return it->second;
    }
    auto* constant = new ConstantFP(ty, val);
    sharedFPCache[key] = std::shared_ptr<ConstantFP>(constant);
    return sharedFPCache[key];
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
    } else if (auto* ptrType = dyn_cast<PointerType>(indexType)) {
        // For pointer types, we need to check what the pointer points to
        Type* pointedType = ptrType->getElementType();
        Type* baseType = arr->getType()->getBaseElementType();

        if (auto* pointedArrayType = dyn_cast<ArrayType>(pointedType)) {
            // Pointer to array: index moves by the size of the array
            size_t elementStride =
                pointedArrayType->getSizeInBytes() / baseType->getSizeInBytes();
            flatIndex = index * elementStride;
        } else {
            // Pointer to scalar: index moves by size of the scalar
            size_t elementStride =
                pointedType->getSizeInBytes() / baseType->getSizeInBytes();
            flatIndex = index * elementStride;
        }
        // No bounds checking for pointers since they can point anywhere
    }

#ifdef A_OUT_DEBUG
    // Check bound only for array types, not for pointer types
    if (!isa<PointerType>(indexType)) {
        size_t totalElements = arr->getNumElements();
        if (flatIndex >= totalElements) {
            throw std::runtime_error("ConstantGEP: index " +
                                     std::to_string(flatIndex) +
                                     " out of bounds for array of size " +
                                     std::to_string(totalElements));
        }
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

    bool isPointerType = isa<PointerType>(indexType);
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
        } else if (auto* ptrType = dyn_cast<PointerType>(currentType)) {
            // For pointer types, the first index moves by the size of the
            // pointed-to type
            Type* pointedType = ptrType->getElementType();
            if (auto* pointedArrayType = dyn_cast<ArrayType>(pointedType)) {
                // Pointer to array: first index moves by array size
                size_t arrayStride = pointedArrayType->getSizeInBytes() /
                                     baseType->getSizeInBytes();
                flatIndex += idx * arrayStride;
                currentType = pointedArrayType;  // Continue with the array type
                                                 // for next indices
            } else {
                // Pointer to scalar: just use index directly
                flatIndex += idx;
                currentType = pointedType;
            }
        } else {
            if (i != indices.size() - 1) return nullptr;
            flatIndex += idx;
        }
    }

#ifdef A_OUT_DEBUG
    // Check bound only for non-pointer types
    if (!isPointerType) {
        size_t totalElements = arr->getNumElements();
        if (flatIndex >= totalElements) {
            throw std::runtime_error("ConstantGEP: index " +
                                     std::to_string(flatIndex) +
                                     " out of bounds for array of size " +
                                     std::to_string(totalElements));
        }
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