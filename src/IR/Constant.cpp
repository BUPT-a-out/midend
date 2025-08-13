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

ConstantGEP* ConstantGEP::get(ConstantArray* arr, size_t index) {
    if (!arr) return nullptr;
    auto* elemPtrTy = PointerType::get(arr->getType()->getElementType());
    return new ConstantGEP(elemPtrTy, arr, index);
}

ConstantGEP* ConstantGEP::get(ConstantGEP* base, size_t index) {
    if (!base) return nullptr;
    // Multi-dimensional: use base->getElement() which should return Constant*
    auto* nextArray = dyn_cast<ConstantArray>(base->getElement());
    if (!nextArray) return nullptr;
    auto* elemPtrTy = PointerType::get(nextArray->getType()->getElementType());
    return new ConstantGEP(elemPtrTy, nextArray, index,
                           base->getArrayPointer());
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr, ConstantInt* indexConst) {
    if (!arr || !indexConst) return nullptr;
    int32_t signedIdx = indexConst->getSignedValue();
    if (signedIdx < 0) return nullptr;
    return get(arr, static_cast<size_t>(signedIdx));
}

ConstantGEP* ConstantGEP::get(ConstantGEP* base, ConstantInt* indexConst) {
    if (!base || !indexConst) return nullptr;
    int32_t signedIdx = indexConst->getSignedValue();
    if (signedIdx < 0) return nullptr;
    return get(base, static_cast<size_t>(signedIdx));
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr,
                              const std::vector<size_t>& indices) {
    if (!arr) return nullptr;
    ConstantGEP* current = nullptr;
    for (size_t i = 0; i < indices.size(); ++i) {
        current = (i == 0) ? get(arr, indices[i]) : get(current, indices[i]);
        if (!current) return nullptr;
    }
    return current;
}

ConstantGEP* ConstantGEP::get(ConstantArray* arr,
                              const std::vector<ConstantInt*>& indices) {
    if (!arr) return nullptr;
    ConstantGEP* current = nullptr;
    for (size_t i = 0; i < indices.size(); ++i) {
        if (!indices[i]) return nullptr;
        int32_t idx = indices[i]->getSignedValue();
        if (idx < 0) return nullptr;
        current = (i == 0) ? get(arr, static_cast<size_t>(idx))
                           : get(current, static_cast<size_t>(idx));
        if (!current) return nullptr;
    }
    return current;
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