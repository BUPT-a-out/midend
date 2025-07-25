#pragma once

#include <cstdint>

#include "IR/Instruction.h"
#include "IR/Type.h"
#include "IR/Value.h"

namespace midend {

class Constant : public User {
   protected:
    Constant(Type* ty, ValueKind kind) : User(ty, kind, 0) {}

   public:
    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant ||
               v->getValueKind() == ValueKind::Function ||
               v->getValueKind() == ValueKind::GlobalVariable;
    }
};

class ConstantInt : public Constant {
   private:
    uint32_t value_;

    ConstantInt(IntegerType* ty, uint32_t val)
        : Constant(ty, ValueKind::Constant), value_(val) {}

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
    bool isNegative() const { return static_cast<int64_t>(value_) < 0; }

    std::string toString() const override { return std::to_string(value_); }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant;
    }
};

class ConstantFP : public Constant {
   private:
    float value_;

    ConstantFP(FloatType* ty, float val)
        : Constant(ty, ValueKind::Constant), value_(val) {}

   public:
    static ConstantFP* get(FloatType* ty, float val);
    static ConstantFP* get(Context* ctx, float val);

    float getValue() const { return value_; }

    bool isZero() const { return value_ == 0.0f; }
    bool isNegative() const { return value_ < 0.0f; }

    std::string toString() const override { return std::to_string(value_); }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant;
    }
};

class ConstantPointerNull : public Constant {
   private:
    ConstantPointerNull(PointerType* ty) : Constant(ty, ValueKind::Constant) {}

   public:
    static ConstantPointerNull* get(PointerType* ty);

    bool isNullValue() const { return true; }

    std::string toString() const override { return "null"; }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant;
    }
};

class ConstantArray : public Constant {
   private:
    std::vector<Constant*> elements_;

    ConstantArray(ArrayType* ty, std::vector<Constant*> elems)
        : Constant(ty, ValueKind::Constant), elements_(std::move(elems)) {}

   public:
    static ConstantArray* get(ArrayType* ty, std::vector<Constant*> elements);

    ArrayType* getType() const {
        return static_cast<ArrayType*>(Constant::getType());
    }

    unsigned getNumElements() const { return elements_.size(); }
    Constant* getElement(unsigned idx) const {
        return idx < elements_.size() ? elements_[idx] : nullptr;
    }

    std::string toString() const override {
        std::string result = "[";
        for (size_t i = 0; i < elements_.size(); ++i) {
            if (i > 0) result += ", ";
            result += elements_[i]->toString();
        }
        result += "]";
        return result;
    }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant;
    }
};

class ConstantExpr : public Constant {
   private:
    Opcode opcode_;
    std::vector<Constant*> operands_;

    ConstantExpr(Type* ty, Opcode op, std::vector<Constant*> operands)
        : Constant(ty, ValueKind::Constant),
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
        return v->getValueKind() == ValueKind::Constant;
    }
};

class UndefValue : public Constant {
   private:
    UndefValue(Type* ty) : Constant(ty, ValueKind::Constant) {}

   public:
    static UndefValue* get(Type* ty);

    std::string toString() const override { return "undef"; }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant;
    }
};

}  // namespace midend