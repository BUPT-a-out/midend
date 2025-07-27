#include "IR/Instructions/TerminatorOps.h"

#include "IR/BasicBlock.h"

namespace midend {

BranchInst::BranchInst(BasicBlock* dest)
    : Instruction(dest->getType()->getContext()->getVoidType(), Opcode::Br, 1) {
    setOperand(0, dest);
}

BranchInst::BranchInst(Value* cond, BasicBlock* trueDest, BasicBlock* falseDest)
    : Instruction(cond->getType()->getContext()->getVoidType(), Opcode::Br, 3) {
    setOperand(0, cond);
    setOperand(1, trueDest);
    setOperand(2, falseDest);
}

ReturnInst* ReturnInst::Create(Context* ctx, Value* retVal,
                               BasicBlock* parent) {
    auto* inst = new ReturnInst(ctx, retVal);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

BranchInst* BranchInst::Create(BasicBlock* dest, BasicBlock* parent) {
    auto* inst = new BranchInst(dest);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

BranchInst* BranchInst::Create(Value* cond, BasicBlock* trueDest,
                               BasicBlock* falseDest, BasicBlock* parent) {
    auto* inst = new BranchInst(cond, trueDest, falseDest);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

void BranchInst::setOperand(unsigned i, Value* v) {
    // Get the current value to check if it's a BasicBlock
    Value* oldVal = getOperand(i);
    bool oldWasBasicBlock =
        oldVal && oldVal->getValueKind() == ValueKind::BasicBlock;
    bool newIsBasicBlock = v && v->getValueKind() == ValueKind::BasicBlock;

    // Call base class setOperand
    Instruction::setOperand(i, v);

    // Invalidate predecessor cache if we're changing successor BasicBlocks
    if (oldWasBasicBlock || newIsBasicBlock) {
        if (auto* parent = getParent()) {
            parent->invalidatePredecessorCacheInFunction();
        }
    }
}

Instruction* BranchInst::clone() const {
    if (isUnconditional()) {
        return Create(getSuccessor(0));
    } else {
        return Create(getCondition(), getSuccessor(0), getSuccessor(1));
    }
}

PHINode* PHINode::Create(Type* ty, const std::string& name,
                         BasicBlock* parent) {
    auto* inst = new PHINode(ty, name);
    if (parent) {
        parent->push_back(inst);
    }
    return inst;
}

void PHINode::addIncoming(Value* val, BasicBlock* bb) {
    addOperand(val);
    addOperand(bb);
}

int PHINode::getBasicBlockIndex(const BasicBlock* bb) const {
    for (unsigned i = 0; i < getNumIncomingValues(); ++i) {
        if (getIncomingBlock(i) == bb) {
            return i;
        }
    }
    return -1;
}

Value* PHINode::getIncomingValueForBlock(const BasicBlock* bb) const {
    int idx = getBasicBlockIndex(bb);
    return idx >= 0 ? getIncomingValue(idx) : nullptr;
}

void PHINode::deleteIncoming(BasicBlock* bb) {
    int idx = getBasicBlockIndex(bb);
    if (idx < 0) return;

    unsigned deleteIdx = static_cast<unsigned>(idx);

    removeOperand(deleteIdx * 2);
    removeOperand(deleteIdx * 2);
}

Instruction* PHINode::clone() const {
    auto* newPhi = Create(getType(), getName());
    for (unsigned i = 0; i < getNumIncomingValues(); ++i) {
        newPhi->addIncoming(getIncomingValue(i), getIncomingBlock(i));
    }
    return newPhi;
}

}  // namespace midend