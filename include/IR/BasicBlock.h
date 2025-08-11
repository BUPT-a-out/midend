#pragma once

#include <list>
#include <vector>

#include "IR/Instruction.h"
#include "IR/Value.h"

namespace midend {

class Function;
class Module;

class BasicBlock : public Value {
   private:
    Function* parent_;
    std::list<Instruction*> instructions_;
    using BBListType = std::list<BasicBlock*>;
    BBListType::iterator iterator_;

    // Cache for predecessor results
    mutable std::vector<BasicBlock*> predecessors_cache_;
    mutable bool predecessors_cached_ = false;

   private:
    BasicBlock(Context* ctx, const std::string& name = "",
               Function* parent = nullptr);

   public:
    ~BasicBlock();

    static BasicBlock* Create(Context* ctx, const std::string& name = "",
                              Function* parent = nullptr);

    bool isVirtual = false;
    using iterator = std::list<Instruction*>::iterator;
    using const_iterator = std::list<Instruction*>::const_iterator;
    using reverse_iterator = std::list<Instruction*>::reverse_iterator;
    using const_reverse_iterator =
        std::list<Instruction*>::const_reverse_iterator;

    iterator begin() { return instructions_.begin(); }
    iterator end() { return instructions_.end(); }
    const_iterator begin() const { return instructions_.begin(); }
    const_iterator end() const { return instructions_.end(); }

    reverse_iterator rbegin() { return instructions_.rbegin(); }
    reverse_iterator rend() { return instructions_.rend(); }
    const_reverse_iterator rbegin() const { return instructions_.rbegin(); }
    const_reverse_iterator rend() const { return instructions_.rend(); }

    bool empty() const { return instructions_.empty(); }
    size_t size() const { return instructions_.size(); }

    Instruction& front() { return *instructions_.front(); }
    const Instruction& front() const { return *instructions_.front(); }
    Instruction& back() { return *instructions_.back(); }
    const Instruction& back() const { return *instructions_.back(); }

    Function* getParent() const { return parent_; }
    void setParent(Function* f) { parent_ = f; }

    Module* getModule() const;

    void push_back(Instruction* inst);
    void push_front(Instruction* inst);
    iterator insert(iterator pos, Instruction* inst);
    iterator erase(iterator pos);
    void remove(Instruction* inst);

    Instruction* getTerminator() {
        return !empty() && back().isTerminator() ? &back() : nullptr;
    }

    const Instruction* getTerminator() const {
        return !empty() && back().isTerminator() ? &back() : nullptr;
    }

    void insertBefore(BasicBlock* bb);
    void insertAfter(BasicBlock* bb);
    void moveBefore(BasicBlock* bb);
    void moveAfter(BasicBlock* bb);
    void removeFromParent();
    void eraseFromParent();

    std::vector<BasicBlock*> getPredecessors() const;
    std::vector<BasicBlock*> getSuccessors() const;

    BasicBlock* split(iterator pos, const std::vector<BasicBlock*>& between);

    // Cache invalidation methods
    void invalidatePredecessorCache() const;
    void invalidatePredecessorCacheInFunction() const;

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::BasicBlock;
    }

    friend class Function;
    void setIterator(BBListType::iterator it) { iterator_ = it; }
    BBListType::iterator getIterator() { return iterator_; }
};

}  // namespace midend