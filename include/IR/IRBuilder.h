#pragma once

#include <string>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instructions.h"

namespace midend {

class IRBuilder {
   private:
    BasicBlock* insertBB_;
    BasicBlock::iterator insertPt_;
    Context* context_;

    void insertHelper(Instruction* inst, const std::string& name) {
        if (!name.empty()) {
            inst->setName(name);
        }
        if (insertBB_) {
            insertBB_->insert(insertPt_, inst);
        }
    }

   public:
    explicit IRBuilder(Context* ctx) : insertBB_(nullptr), context_(ctx) {}

    IRBuilder(BasicBlock* bb, BasicBlock::iterator it, Context* ctx)
        : insertBB_(bb), insertPt_(it), context_(ctx) {}

    explicit IRBuilder(BasicBlock* bb)
        : insertBB_(bb),
          insertPt_(bb->end()),
          context_(bb ? bb->getType()->getContext() : nullptr) {}

    Context* getContext() const { return context_; }

    void setInsertPoint(BasicBlock* bb) {
        insertBB_ = bb;
        insertPt_ = bb->end();
    }

    void setInsertPoint(BasicBlock* bb, BasicBlock::iterator it) {
        insertBB_ = bb;
        insertPt_ = it;
    }

    void setInsertPoint(Instruction* inst) {
        insertBB_ = inst->getParent();
        insertPt_ = inst->getIterator();
    }

    void insert(Instruction* inst, const std::string& name = "") {
        insertHelper(inst, name);
    }

    BasicBlock* getInsertBlock() const { return insertBB_; }
    BasicBlock::iterator getInsertPoint() const { return insertPt_; }

    // Type creation helpers
    IntegerType* getInt1Type() { return context_->getInt1Type(); }
    IntegerType* getInt32Type() { return context_->getInt32Type(); }
    IntegerType* getIntType(unsigned bits) {
        return context_->getIntegerType(bits);
    }

    Type* getVoidType() { return context_->getVoidType(); }
    Type* getFloatType() { return context_->getFloatType(); }

    // Constant creation helpers
    ConstantInt* getInt1(bool val) {
        return ConstantInt::get(getInt1Type(), val ? 1 : 0);
    }

    ConstantInt* getInt32(uint32_t val) {
        return ConstantInt::get(getInt32Type(), val);
    }

    ConstantInt* getTrue() { return ConstantInt::getTrue(context_); }
    ConstantInt* getFalse() { return ConstantInt::getFalse(context_); }

    ConstantFP* getFloat(float val) { return ConstantFP::get(context_, val); }

    // Unary operations
    UnaryOperator* createUAdd(Value* operand, const std::string& name = "") {
        auto* inst = UnaryOperator::Create(Opcode::UAdd, operand);
        insertHelper(inst, name);
        return inst;
    }

    UnaryOperator* createUSub(Value* operand, const std::string& name = "") {
        auto* inst = UnaryOperator::Create(Opcode::USub, operand);
        insertHelper(inst, name);
        return inst;
    }

    UnaryOperator* createNot(Value* operand, const std::string& name = "") {
        auto* inst = UnaryOperator::Create(Opcode::Not, operand);
        insertHelper(inst, name);
        return inst;
    }

    // Binary operations
    BinaryOperator* createAdd(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Add, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createSub(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Sub, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createMul(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Mul, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createDiv(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Div, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createRem(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Rem, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    // Logical operations
    BinaryOperator* createLAnd(Value* lhs, Value* rhs,
                               const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::LAnd, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createLOr(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::LOr, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    // Bitwise operations
    BinaryOperator* createAnd(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::And, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createOr(Value* lhs, Value* rhs,
                             const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Or, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createXor(Value* lhs, Value* rhs,
                              const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::Xor, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    // Floating point operations
    BinaryOperator* createFAdd(Value* lhs, Value* rhs,
                               const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::FAdd, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createFSub(Value* lhs, Value* rhs,
                               const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::FSub, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createFMul(Value* lhs, Value* rhs,
                               const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::FMul, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    BinaryOperator* createFDiv(Value* lhs, Value* rhs,
                               const std::string& name = "") {
        auto* inst = BinaryOperator::Create(Opcode::FDiv, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    // Comparison operations
    CmpInst* createICmpEQ(Value* lhs, Value* rhs,
                          const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::ICmpEQ, CmpInst::ICMP_EQ, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createICmpNE(Value* lhs, Value* rhs,
                          const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::ICmpNE, CmpInst::ICMP_NE, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createICmpSLT(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::ICmpSLT, CmpInst::ICMP_SLT, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createICmpSLE(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::ICmpSLE, CmpInst::ICMP_SLE, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createICmpSGT(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::ICmpSGT, CmpInst::ICMP_SGT, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createICmpSGE(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::ICmpSGE, CmpInst::ICMP_SGE, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createFCmpOEQ(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::FCmpOEQ, CmpInst::FCMP_OEQ, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createFCmpONE(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::FCmpONE, CmpInst::FCMP_ONE, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createFCmpOLT(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::FCmpOLT, CmpInst::FCMP_OLT, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createFCmpOLE(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::FCmpOLE, CmpInst::FCMP_OLE, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createFCmpOGT(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::FCmpOGT, CmpInst::FCMP_OGT, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    CmpInst* createFCmpOGE(Value* lhs, Value* rhs,
                           const std::string& name = "") {
        auto* inst =
            CmpInst::Create(Opcode::FCmpOGE, CmpInst::FCMP_OGE, lhs, rhs);
        insertHelper(inst, name);
        return inst;
    }

    // Memory operations
    AllocaInst* createAlloca(Type* ty, Value* arraySize = nullptr,
                             const std::string& name = "") {
        auto* inst = AllocaInst::Create(ty, arraySize);
        insertHelper(inst, name);
        return inst;
    }

    LoadInst* createLoad(Value* ptr, const std::string& name = "") {
        auto* inst = LoadInst::Create(ptr);
        insertHelper(inst, name);
        return inst;
    }

    StoreInst* createStore(Value* val, Value* ptr) {
        auto* inst = StoreInst::Create(val, ptr);
        insertHelper(inst, "");
        return inst;
    }

    GetElementPtrInst* createGEP(Type* ty, Value* ptr,
                                 const std::vector<Value*>& indices,
                                 const std::string& name = "") {
        auto* inst = GetElementPtrInst::Create(ty, ptr, indices);
        insertHelper(inst, name);
        return inst;
    }

    // Convenience overload for single index GEP (common for array access)
    GetElementPtrInst* createGEP(Value* ptr, Value* index,
                                 const std::string& name = "") {
        // For pointer type, we need to get the pointed-to type
        auto* ptrType = dyn_cast<PointerType>(ptr->getType());
        if (!ptrType) {
            // If it's not a pointer type, assume it's for array indexing
            return createGEP(ptr->getType(), ptr, {index}, name);
        }
        Type* elementType = ptrType->getElementType();
        return createGEP(elementType, ptr, {index}, name);
    }

    // Control flow
    ReturnInst* createRetVoid() {
        auto* inst = ReturnInst::Create(context_, nullptr);
        insertHelper(inst, "");
        return inst;
    }

    ReturnInst* createRet(Value* val) {
        auto* inst = ReturnInst::Create(context_, val);
        insertHelper(inst, "");
        return inst;
    }

    BranchInst* createBr(BasicBlock* dest) {
        auto* inst = BranchInst::Create(dest);
        insertHelper(inst, "");
        return inst;
    }

    BranchInst* createCondBr(Value* cond, BasicBlock* trueDest,
                             BasicBlock* falseDest) {
        auto* inst = BranchInst::Create(cond, trueDest, falseDest);
        insertHelper(inst, "");
        return inst;
    }

    // Function calls
    CallInst* createCall(Function* fn, const std::vector<Value*>& args,
                         const std::string& name = "") {
        auto* inst = CallInst::Create(fn, args);
        insertHelper(inst, name);
        return inst;
    }

    CallInst* createCall(FunctionType* fnTy, Value* fn,
                         const std::vector<Value*>& args,
                         const std::string& name = "") {
        auto* inst = CallInst::Create(fnTy, fn, args);
        insertHelper(inst, name);
        return inst;
    }

    // Other instructions
    PHINode* createPHI(Type* ty, const std::string& name = "") {
        auto* inst = PHINode::Create(ty, name);
        insertHelper(inst, name);
        return inst;
    }

    Value* createCast(CastInst::CastOps op, Value* val, Type* destTy,
                      const std::string& name = "") {
        auto* inst = CastInst::Create(op, val, destTy);
        insertHelper(inst, name);
        return inst;
    }

    // Convenient cast operations
    Value* createSIToFP(Value* val, Type* destTy,
                        const std::string& name = "") {
        return createCast(CastInst::SIToFP, val, destTy, name);
    }

    Value* createFPToSI(Value* val, Type* destTy,
                        const std::string& name = "") {
        return createCast(CastInst::FPToSI, val, destTy, name);
    }

    // Simple assignment/move instruction
    MoveInst* createMove(Value* val, const std::string& name = "") {
        auto* inst = MoveInst::Create(val);
        insertHelper(inst, name);
        return inst;
    }

    // Basic block creation
    BasicBlock* createBasicBlock(const std::string& name = "",
                                 Function* parent = nullptr) {
        return BasicBlock::Create(context_, name, parent);
    }
};

}  // namespace midend