#pragma once

#include <cassert>

namespace midend {

enum class ValueKind {
    Value,
    Use,
    User,
    Constant,
    GlobalVariable,
    Function,
    BasicBlock,
    Argument,

    InstructionBegin,
    BinaryOperator,
    UnaryOperator,
    CmpInst,
    AllocaInst,
    LoadInst,
    StoreInst,
    GetElementPtrInst,
    CallInst,
    ReturnInst,
    BranchInst,
    PHINode,
    InstructionEnd,
};

template <typename To, typename From>
inline bool isa(const From* val) {
    return To::classof(val);
}

template <typename To, typename From>
inline bool isa(From* val) {
    return To::classof(val);
}

template <typename To, typename From>
inline bool isa(const From& val) {
    return To::classof(&val);
}

template <typename To, typename From>
inline To* dyn_cast(From* val) {
    return isa<To>(*val) ? static_cast<To*>(val) : nullptr;
}

template <typename To, typename From>
inline const To* dyn_cast(const From* val) {
    return isa<To>(*val) ? static_cast<const To*>(val) : nullptr;
}

template <typename To, typename From>
inline To* cast(From* val) {
    assert(isa<To>(*val) && "Invalid cast!");
    return static_cast<To*>(val);
}

template <typename To, typename From>
inline const To* cast(const From* val) {
    assert(isa<To>(*val) && "Invalid cast!");
    return static_cast<const To*>(val);
}

}  // namespace midend