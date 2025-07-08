#include "IR/Instruction.h"

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Module.h"

namespace midend {

Function* Instruction::getFunction() const {
    return parent_ ? parent_->getParent() : nullptr;
}

Module* Instruction::getModule() const {
    Function* f = getFunction();
    return f ? f->getParent() : nullptr;
}

void Instruction::insertBefore(Instruction* inst) {
    if (!inst->getParent()) return;

    BasicBlock* bb = inst->getParent();
    bb->insert(inst->getIterator(), this);
}

void Instruction::insertAfter(Instruction* inst) {
    if (!inst->getParent()) return;

    BasicBlock* bb = inst->getParent();
    auto it = inst->getIterator();
    ++it;
    bb->insert(it, this);
}

void Instruction::moveBefore(Instruction* inst) {
    removeFromParent();
    insertBefore(inst);
}

void Instruction::moveAfter(Instruction* inst) {
    removeFromParent();
    insertAfter(inst);
}

void Instruction::removeFromParent() {
    if (parent_) {
        parent_->remove(this);
    }
}

void Instruction::eraseFromParent() {
    if (parent_) {
        parent_->erase(iterator_);
    }
}

}  // namespace midend