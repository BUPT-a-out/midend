#pragma once

#include <list>

#include "IR/Value.h"

namespace midend {

class BasicBlock;
class Function;
class Module;

enum class Opcode {
    // Unary operations
    UAdd,
    USub,
    Not,

    // Integer binary operations
    Add,
    Sub,
    Mul,
    Div,
    Rem,
    And,
    Or,
    Xor,
    Shl,
    Shr,

    // Floating point binary operations
    FAdd,
    FSub,
    FMul,
    FDiv,

    // Logical operations
    LAnd,
    LOr,

    // Comparison
    ICmpEQ,
    ICmpNE,
    ICmpSLT,
    ICmpSLE,
    ICmpSGT,
    ICmpSGE,
    FCmpOEQ,
    FCmpONE,
    FCmpOLT,
    FCmpOLE,
    FCmpOGT,
    FCmpOGE,

    // Memory operations
    Alloca,
    Load,
    Store,
    GetElementPtr,

    // Control flow
    Ret,
    Br,
    Call,

    // Other
    PHI,
    Select,
    Cast,
    Move,
};

class Instruction : public User {
   private:
    BasicBlock* parent_;
    Opcode opcode_;
    using InstListType = std::list<Instruction*>;
    InstListType::iterator iterator_;

   protected:
    Instruction(Type* ty, Opcode op, unsigned numOps,
                BasicBlock* parent = nullptr)
        : User(ty, ValueKind::InstructionBegin, numOps),
          parent_(parent),
          opcode_(op) {}

   public:
    virtual ~Instruction() = default;

    Opcode getOpcode() const { return opcode_; }

    BasicBlock* getParent() const { return parent_; }
    Function* getFunction() const;
    Module* getModule() const;

    void setParent(BasicBlock* bb) { parent_ = bb; }

    void insertBefore(Instruction* inst);
    void insertAfter(Instruction* inst);
    void moveBefore(Instruction* inst);
    void moveAfter(Instruction* inst);
    void removeFromParent();
    void eraseFromParent();

    bool isTerminator() const {
        return opcode_ == Opcode::Ret || opcode_ == Opcode::Br;
    }

    bool isUnaryOp() const {
        return opcode_ >= Opcode::UAdd && opcode_ <= Opcode::Not;
    }

    bool isBinaryOp() const {
        return (opcode_ >= Opcode::Add && opcode_ <= Opcode::Shr) ||
               (opcode_ >= Opcode::LAnd && opcode_ <= Opcode::LOr);
    }

    bool isComparison() const {
        return opcode_ >= Opcode::ICmpEQ && opcode_ <= Opcode::FCmpOGE;
    }

    bool isMemoryOp() const {
        return opcode_ >= Opcode::Alloca && opcode_ <= Opcode::GetElementPtr;
    }

    virtual Instruction* clone() const = 0;

    static bool classof(const Value* v) {
        return v->getValueKind() >= ValueKind::InstructionBegin &&
               v->getValueKind() <= ValueKind::InstructionEnd;
    }

    friend class BasicBlock;
    void setIterator(InstListType::iterator it) { iterator_ = it; }
    InstListType::iterator getIterator() { return iterator_; }
};

}  // namespace midend