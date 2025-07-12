#pragma once

#include <list>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Value.h"

namespace midend {

class Module;
class FunctionType;

class Argument : public Value {
   private:
    Function* parent_;
    unsigned argNo_;

   public:
    Argument(Type* ty, const std::string& name, Function* parent,
             unsigned argNo)
        : Value(ty, ValueKind::Argument, name),
          parent_(parent),
          argNo_(argNo) {}

    Function* getParent() const { return parent_; }
    unsigned getArgNo() const { return argNo_; }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Argument;
    }
};

class Function : public Constant {
   private:
    Module* parent_;
    std::list<BasicBlock*> basicBlocks_;
    std::vector<std::unique_ptr<Argument>> arguments_;
    bool isDeclaration_;
    using FuncListType = std::list<Function*>;
    FuncListType::iterator iterator_;

   public:
    Function(FunctionType* ty, const std::string& name,
             Module* parent = nullptr);
    Function(FunctionType* ty, const std::string& name,
             const std::vector<std::string>& paramNames,
             Module* parent = nullptr);
    ~Function();

    using iterator = std::list<BasicBlock*>::iterator;
    using const_iterator = std::list<BasicBlock*>::const_iterator;
    using arg_iterator = std::vector<std::unique_ptr<Argument>>::iterator;
    using const_arg_iterator =
        std::vector<std::unique_ptr<Argument>>::const_iterator;

    iterator begin() { return basicBlocks_.begin(); }
    iterator end() { return basicBlocks_.end(); }
    const_iterator begin() const { return basicBlocks_.begin(); }
    const_iterator end() const { return basicBlocks_.end(); }

    arg_iterator arg_begin() { return arguments_.begin(); }
    arg_iterator arg_end() { return arguments_.end(); }
    const_arg_iterator arg_begin() const { return arguments_.begin(); }
    const_arg_iterator arg_end() const { return arguments_.end(); }

    bool empty() const { return basicBlocks_.empty(); }
    size_t size() const { return basicBlocks_.size(); }

    BasicBlock& front() { return *basicBlocks_.front(); }
    const BasicBlock& front() const { return *basicBlocks_.front(); }
    BasicBlock& back() { return *basicBlocks_.back(); }
    const BasicBlock& back() const { return *basicBlocks_.back(); }

    BasicBlock& getEntryBlock() { return front(); }
    const BasicBlock& getEntryBlock() const { return front(); }

    Module* getParent() const { return parent_; }
    void setParent(Module* m) { parent_ = m; }

    FunctionType* getFunctionType() const;
    Type* getReturnType() const;

    size_t getNumArgs() const { return arguments_.size(); }
    Argument* getArg(unsigned i) {
        return i < arguments_.size() ? arguments_[i].get() : nullptr;
    }
    Argument* getArgByName(const std::string& name) {
        for (auto& arg : arguments_) {
            if (arg->getName() == name) {
                return arg.get();
            }
        }
        return nullptr;
    }
    void setArgName(unsigned i, const std::string& name) {
        if (i < arguments_.size()) {
            arguments_[i]->setName(name);
        }
    }

    bool isDeclaration() const { return isDeclaration_; }
    bool isDefinition() const { return !isDeclaration_; }

    void push_back(BasicBlock* bb);
    void push_front(BasicBlock* bb);
    iterator insert(iterator pos, BasicBlock* bb);
    iterator erase(iterator pos);
    void remove(BasicBlock* bb);

    void insertBefore(Function* fn);
    void insertAfter(Function* fn);
    void moveBefore(Function* fn);
    void moveAfter(Function* fn);
    void removeFromParent();
    void eraseFromParent();

    std::vector<BasicBlock*> getBasicBlocks() const;

    static Function* Create(FunctionType* ty, const std::string& name,
                            Module* parent = nullptr);
    static Function* Create(FunctionType* ty, const std::string& name,
                            const std::vector<std::string>& paramNames,
                            Module* parent = nullptr);

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Function;
    }

    friend class Module;
    void setIterator(FuncListType::iterator it) { iterator_ = it; }
    FuncListType::iterator getIterator() { return iterator_; }
};

}  // namespace midend