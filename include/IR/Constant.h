#pragma once

#include <cstdint>

#include "IR/IRPrinter.h"
#include "IR/Instruction.h"
#include "IR/Type.h"
#include "IR/Value.h"

namespace midend {

class Constant : public User {
   protected:
    Constant(Type* ty, ValueKind kind) : User(ty, kind, 0) {}

   public:
    static bool classof(const Value* v) {
        return (v->getValueKind() >= ValueKind::ConstantBegin &&
                v->getValueKind() <= ValueKind::ConstantEnd) ||
               v->getValueKind() == ValueKind::Function ||
               v->getValueKind() == ValueKind::GlobalVariable;
    }
};

class ConstantInt : public Constant {
   private:
    uint32_t value_;

    ConstantInt(IntegerType* ty, uint32_t val)
        : Constant(ty, ValueKind::ConstantInt), value_(val) {}

   public:
    static ConstantInt* get(IntegerType* ty, uint32_t val);
    static ConstantInt* get(Context* ctx, unsigned bitWidth, uint32_t val);
    static ConstantInt* getTrue(Context* ctx);
    static ConstantInt* getFalse(Context* ctx);

    int32_t getValue() const { return getSignedValue(); }
    uint32_t getUnsignedValue() const { return value_; }
    int32_t getSignedValue() const { return static_cast<int32_t>(value_); }

    bool isZero() const { return value_ == 0; }
    bool isOne() const { return value_ == 1; }
    bool isTrue() const { return value_ != 0; }
    bool isFalse() const { return value_ == 0; }
    bool isNegative() const { return static_cast<int32_t>(value_) < 0; }

    std::string toString() const override { return std::to_string(value_); }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::ConstantInt;
    }
};

class ConstantFP : public Constant {
   private:
    float value_;

    ConstantFP(FloatType* ty, float val)
        : Constant(ty, ValueKind::ConstantFP), value_(val) {}

   public:
    static ConstantFP* get(FloatType* ty, float val);
    static ConstantFP* get(Context* ctx, float val);

    float getValue() const { return value_; }

    bool isZero() const { return value_ == 0.0f; }
    bool isNegative() const { return value_ < 0.0f; }

    std::string toString() const override { return std::to_string(value_); }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::ConstantFP;
    }
};

class ConstantPointerNull : public Constant {
   private:
    ConstantPointerNull(PointerType* ty)
        : Constant(ty, ValueKind::ConstantPointerNull) {}

   public:
    static ConstantPointerNull* get(PointerType* ty);

    bool isNullValue() const { return true; }

    std::string toString() const override { return "null"; }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::ConstantPointerNull;
    }
};

class ConstantArray : public Constant {
   private:
    std::vector<Constant*> elements_;

    ConstantArray(ArrayType* ty, std::vector<Constant*> elems)
        : Constant(ty, ValueKind::ConstantArray), elements_(std::move(elems)) {}

   public:
    static ConstantArray* get(ArrayType* ty, std::vector<Constant*> elements);

    ArrayType* getType() const {
        return static_cast<ArrayType*>(Constant::getType());
    }

    unsigned getNumElements() const { return elements_.size(); }
    Constant* getElement(unsigned idx) const {
        return idx < elements_.size() ? elements_[idx] : nullptr;
    }

    void setElement(unsigned idx, Constant* value) {
        if (idx < elements_.size()) {
            elements_[idx] = value;
        }
    }

    std::string toString() const override {
        std::string result = "[";
        unsigned lastNonZero = 0;

        // Find the last non-zero element
        for (unsigned i = getNumElements(); i > 0; --i) {
            if (auto* elem = dyn_cast<ConstantInt>(getElement(i - 1))) {
                if (elem->getSignedValue() != 0) {
                    lastNonZero = i;
                    break;
                }
            } else {
                // Non-integer elements are always considered significant
                lastNonZero = i;
                break;
            }
        }

        // Print elements up to the last non-zero
        for (unsigned i = 0; i < lastNonZero; ++i) {
            if (i > 0) result += ", ";
            result += IRPrinter::printType(getElement(i)->getType()) + " ";
            result += IRPrinter().getValueName(getElement(i));
        }

        // If there are trailing zeros, indicate with ...
        if (lastNonZero < getNumElements()) {
            if (lastNonZero > 0) result += ", ";
            result += "...";
        }

        result += "]";
        return result;
    }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::ConstantArray;
    }
};

class ConstantGEP : public Constant {
   private:
    ConstantArray* array_;
    size_t index_;

    ConstantGEP(PointerType* ty, ConstantArray* arr, size_t idx)
        : Constant(ty, ValueKind::ConstantGEP), array_(arr), index_(idx) {}

   public:
    // 1) ConstantArray*, size_t
    static ConstantGEP* get(ConstantArray* arr, size_t index);
    // 2) ConstantGEP*, size_t
    static ConstantGEP* get(ConstantGEP* base, size_t index);
    // 3) ConstantArray*, ConstantInt*
    static ConstantGEP* get(ConstantArray* arr, ConstantInt* indexConst);
    // 4) ConstantGEP*, ConstantInt*
    static ConstantGEP* get(ConstantGEP* base, ConstantInt* indexConst);
    // 5) ConstantArray*, std::vector<size_t>
    static ConstantGEP* get(ConstantArray* arr,
                            const std::vector<size_t>& indices);
    // 6) ConstantArray*, std::vector<ConstantInt*>
    static ConstantGEP* get(ConstantArray* arr,
                            const std::vector<ConstantInt*>& indices);
    // 7) ConstantGEP*, std::vector<size_t>
    static ConstantGEP* get(ConstantGEP* base,
                            const std::vector<size_t>& indices);
    // 8) ConstantGEP*, std::vector<ConstantInt*>
    static ConstantGEP* get(ConstantGEP* base,
                            const std::vector<ConstantInt*>& indices);

    ConstantArray* getArray() const { return array_; }
    size_t getIndex() const { return index_; }

    Constant* getElement() const {
        return array_ ? array_->getElement(index_) : nullptr;
    }

    std::string toString() const override {
        return std::string("gep(") +
               (array_ ? array_->toString() : std::string("null")) + ", " +
               std::to_string(index_) + ")";
    }

    void setElementValue(Value* newValue);

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::ConstantGEP;
    }
};

class ConstantExpr : public Constant {
   private:
    Opcode opcode_;
    std::vector<Constant*> operands_;

    ConstantExpr(Type* ty, Opcode op, std::vector<Constant*> operands)
        : Constant(ty, ValueKind::ConstantExpr),
          opcode_(op),
          operands_(std::move(operands)) {}

   public:
    static ConstantExpr* getAdd(Constant* lhs, Constant* rhs);
    static ConstantExpr* getSub(Constant* lhs, Constant* rhs);
    static ConstantExpr* getMul(Constant* lhs, Constant* rhs);

    Opcode getOpcode() const { return opcode_; }

    Constant* getOperand(unsigned idx) const {
        return idx < operands_.size() ? operands_[idx] : nullptr;
    }

    unsigned getNumOperands() const { return operands_.size(); }

    std::string toString() const override { return "const_expr"; }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::ConstantExpr;
    }
};

class UndefValue : public Constant {
   private:
    UndefValue(Type* ty) : Constant(ty, ValueKind::UndefValue) {}

   public:
    static UndefValue* get(Type* ty);

    std::string toString() const override { return "undef"; }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::UndefValue;
    }
};

}  // namespace midend