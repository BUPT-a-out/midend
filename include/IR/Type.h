#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace midend {

class Context;
class LabelType;
class PointerType;

enum class TypeKind {
    Void,
    Integer,
    Float,
    Pointer,
    Array,
    Function,
    Label,
};

class Type {
   private:
    TypeKind kind_;
    Context *context_;

   protected:
    Type(TypeKind kind, Context *ctx) : kind_(kind), context_(ctx) {}

   public:
    virtual ~Type() = default;

    TypeKind getKind() const { return kind_; }
    Context *getContext() const { return context_; }

    bool isVoidType() const { return kind_ == TypeKind::Void; }
    bool isIntegerType() const { return kind_ == TypeKind::Integer; }
    bool isFloatType() const { return kind_ == TypeKind::Float; }
    bool isPointerType() const { return kind_ == TypeKind::Pointer; }
    bool isArrayType() const { return kind_ == TypeKind::Array; }
    bool isFunctionType() const { return kind_ == TypeKind::Function; }

    virtual unsigned getBitWidth() const { return 0; }
    virtual bool isSized() const { return true; }

    // Get single-level element type
    Type *getSingleElementType() const;

    // Get base element type (for multi-dimensional arrays/pointers)
    Type *getBaseElementType() const;

    // Get multi-level element type (for GEP with multiple indices)
    Type *getMultiLevelElementType(unsigned levels) const;

    // Get size of type in bytes (for backend code generation)
    unsigned getSizeInBytes() const;

    virtual std::string toString() const = 0;
};

class IntegerType : public Type {
   private:
    unsigned bitWidth_;

    IntegerType(unsigned bits, Context *ctx)
        : Type(TypeKind::Integer, ctx), bitWidth_(bits) {}

    friend class Context;

   public:
    static IntegerType *get(Context *ctx, unsigned bits);

    unsigned getBitWidth() const override { return bitWidth_; }

    bool isBool() const { return bitWidth_ == 1; }

    std::string toString() const override {
        return "i" + std::to_string(bitWidth_);
    }

    PointerType *getPointerTo();

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Integer;
    }
};

class FloatType : public Type {
   private:
    FloatType(Context *ctx) : Type(TypeKind::Float, ctx) {}

    friend class Context;

   public:
    static FloatType *get(Context *ctx);

    unsigned getBitWidth() const override { return 32; }

    std::string toString() const override { return "float"; }

    PointerType *getPointerTo();

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Float;
    }
};

class VoidType : public Type {
   private:
    VoidType(Context *ctx) : Type(TypeKind::Void, ctx) {}

    friend class Context;

   public:
    static VoidType *get(Context *ctx);

    std::string toString() const override { return "void"; }

    PointerType *getPointerTo();

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Void;
    }
};

class PointerType : public Type {
   private:
    Type *elementType_;

    PointerType(Type *elemType, Context *ctx)
        : Type(TypeKind::Pointer, ctx), elementType_(elemType) {}

    friend class Context;

   public:
    static PointerType *get(Type *elemType);

    Type *getElementType() const { return elementType_; }

    unsigned getBitWidth() const override { return 64; }

    std::string toString() const override {
        return elementType_->toString() + "*";
    }

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Pointer;
    }
};

class ArrayType : public Type {
   private:
    Type *elementType_;
    uint64_t numElements_;

    ArrayType(Type *elemType, uint64_t numElems, Context *ctx)
        : Type(TypeKind::Array, ctx),
          elementType_(elemType),
          numElements_(numElems) {}

    friend class Context;

   public:
    static ArrayType *get(Type *elemType, uint64_t numElems);

    Type *getElementType() const { return elementType_; }
    uint64_t getNumElements() const { return numElements_; }

    std::string toString() const override {
        return "[" + std::to_string(numElements_) + " x " +
               elementType_->toString() + "]";
    }

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Array;
    }
};

class FunctionType : public Type {
   private:
    Type *returnType_;
    std::vector<Type *> paramTypes_;
    bool isVarArg_;

    FunctionType(Type *retType, std::vector<Type *> params, bool varArg,
                 Context *ctx)
        : Type(TypeKind::Function, ctx),
          returnType_(retType),
          paramTypes_(std::move(params)),
          isVarArg_(varArg) {}

   public:
    static FunctionType *get(Type *retType, std::vector<Type *> params,
                             bool varArg = false);

    Type *getReturnType() const { return returnType_; }
    const std::vector<Type *> &getParamTypes() const { return paramTypes_; }
    bool isVarArg() const { return isVarArg_; }

    std::string toString() const override;

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Function;
    }
};

class LabelType : public Type {
   private:
    LabelType(Context *ctx) : Type(TypeKind::Label, ctx) {}

    friend class Context;

   public:
    static LabelType *get(Context *ctx);

    std::string toString() const override { return "label"; }

    static bool classof(const Type *type) {
        return type->getKind() == TypeKind::Label;
    }
};

class Context {
   private:
    std::unique_ptr<VoidType> voidType_;
    std::unique_ptr<FloatType> floatType_;
    std::unique_ptr<LabelType> labelType_;
    std::unordered_map<unsigned, std::unique_ptr<IntegerType>> integerTypes_;
    std::unordered_map<Type *, std::unique_ptr<PointerType>> pointerTypes_;
    std::unordered_map<std::string, std::unique_ptr<ArrayType>> arrayTypes_;
    std::vector<std::unique_ptr<Type>> types_;

   public:
    Context();

    VoidType *getVoidType();
    IntegerType *getIntegerType(unsigned bits);
    FloatType *getFloatType();
    LabelType *getLabelType();
    PointerType *getPointerType(Type *elemType);
    ArrayType *getArrayType(Type *elemType, uint64_t numElems);

    IntegerType *getInt1Type() { return getIntegerType(1); }
    IntegerType *getInt32Type() { return getIntegerType(32); }

    template <typename T, typename... Args>
    T *createType(Args &&...args) {
        auto type = std::make_unique<T>(std::forward<Args>(args)...);
        T *ptr = type.get();
        types_.push_back(std::move(type));
        return ptr;
    }
};

}  // namespace midend