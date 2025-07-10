#include "IR/BasicBlock.h"

#include "IR/Function.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "IR/Type.h"

namespace midend {

BasicBlock::BasicBlock(Context* ctx, const std::string& name, Function* parent)
    : Value(LabelType::get(ctx), ValueKind::BasicBlock, name),
      parent_(nullptr) {
    if (parent) {
        parent->push_back(this);
    }
}

BasicBlock* BasicBlock::Create(Context* ctx, const std::string& name,
                               Function* parent) {
    return new BasicBlock(ctx, name, parent);
}

BasicBlock::~BasicBlock() {
    for (auto* inst : instructions_) {
        delete inst;
    }
}

Module* BasicBlock::getModule() const {
    return parent_ ? parent_->getParent() : nullptr;
}

void BasicBlock::push_back(Instruction* inst) {
    instructions_.push_back(inst);
    inst->setParent(this);
    inst->setIterator(std::prev(instructions_.end()));
    
    if (inst->isTerminator()) {
        invalidatePredecessorCacheInFunction();
    }
}

void BasicBlock::push_front(Instruction* inst) {
    instructions_.push_front(inst);
    inst->setParent(this);
    inst->setIterator(instructions_.begin());
}

BasicBlock::iterator BasicBlock::insert(iterator pos, Instruction* inst) {
    auto it = instructions_.insert(pos, inst);
    inst->setParent(this);
    inst->setIterator(it);
    
    if (inst->isTerminator()) {
        invalidatePredecessorCacheInFunction();
    }
    
    return it;
}

BasicBlock::iterator BasicBlock::erase(iterator pos) {
    bool was_terminator = (*pos)->isTerminator();
    (*pos)->setParent(nullptr);
    delete *pos;
    auto result = instructions_.erase(pos);
    
    if (was_terminator) {
        invalidatePredecessorCacheInFunction();
    }
    
    return result;
}

void BasicBlock::remove(Instruction* inst) {
    bool was_terminator = inst->isTerminator();
    instructions_.erase(inst->getIterator());
    inst->setParent(nullptr);
    
    if (was_terminator) {
        invalidatePredecessorCacheInFunction();
    }
}

void BasicBlock::insertBefore(BasicBlock* bb) {
    if (!bb->getParent()) return;

    Function* fn = bb->getParent();
    fn->insert(bb->getIterator(), this);
}

void BasicBlock::insertAfter(BasicBlock* bb) {
    if (!bb->getParent()) return;

    Function* fn = bb->getParent();
    auto it = bb->getIterator();
    ++it;
    fn->insert(it, this);
}

void BasicBlock::moveBefore(BasicBlock* bb) {
    removeFromParent();
    insertBefore(bb);
}

void BasicBlock::moveAfter(BasicBlock* bb) {
    removeFromParent();
    insertAfter(bb);
}

void BasicBlock::removeFromParent() {
    if (parent_) {
        parent_->remove(this);
    }
}

void BasicBlock::eraseFromParent() {
    if (parent_) {
        parent_->erase(iterator_);
    }
}

std::vector<BasicBlock*> BasicBlock::getPredecessors() const {
    if (predecessors_cached_) {
        return predecessors_cache_;
    }

    predecessors_cache_.clear();
    
    if (!parent_) {
        predecessors_cached_ = true;
        return predecessors_cache_;
    }

    for (auto* bb : parent_->getBasicBlocks()) {
        if (bb == this) continue;

        auto* terminator = bb->getTerminator();
        if (!terminator) continue;

        if (auto* br = dyn_cast<BranchInst>(terminator)) {
            for (unsigned i = 0; i < br->getNumSuccessors(); ++i) {
                if (br->getSuccessor(i) == this) {
                    predecessors_cache_.push_back(bb);
                    break;
                }
            }
        }
    }

    predecessors_cached_ = true;
    return predecessors_cache_;
}

std::vector<BasicBlock*> BasicBlock::getSuccessors() const {
    std::vector<BasicBlock*> successors;

    auto* terminator = getTerminator();
    if (!terminator) return successors;

    if (auto* br = dyn_cast<BranchInst>(terminator)) {
        for (unsigned i = 0; i < br->getNumSuccessors(); ++i) {
            auto* succ = br->getSuccessor(i);
            if (succ) {
                successors.push_back(succ);
            }
        }
    }

    return successors;
}

void BasicBlock::invalidatePredecessorCache() const {
    predecessors_cached_ = false;
    predecessors_cache_.clear();
}

void BasicBlock::invalidatePredecessorCacheInFunction() const {
    if (!parent_) return;
    
    for (auto* bb : *parent_) {
        bb->invalidatePredecessorCache();
    }
}

}  // namespace midend