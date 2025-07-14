#include "IR/Constant.h"

#include <map>
#include <stdexcept>

namespace midend {

static std::map<std::pair<IntegerType*, uint64_t>, ConstantInt*> integerCache;
static std::map<std::pair<FloatType*, float>, ConstantFP*> fpCache;
static std::map<PointerType*, ConstantPointerNull*> nullPtrCache;
static std::map<Type*, UndefValue*> undefCache;

ConstantInt* ConstantInt::get(IntegerType* ty, uint64_t val) {
    auto key = std::make_pair(ty, val);
    auto it = integerCache.find(key);
    if (it != integerCache.end()) {
        return it->second;
    }
    auto* constant = new ConstantInt(ty, val);
    integerCache[key] = constant;
    return constant;
}

ConstantInt* ConstantInt::get(Context* ctx, unsigned bitWidth, uint64_t val) {
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