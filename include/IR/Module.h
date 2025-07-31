#pragma once

#include <list>
#include <string>

#include "IR/Function.h"
#include "IR/Value.h"

namespace midend {

class GlobalVariable : public Constant {
   public:
    enum LinkageTypes { ExternalLinkage, InternalLinkage, PrivateLinkage };

   private:
    Module* parent_;
    bool isConstant_;
    Value* initializer_;
    LinkageTypes linkage_;
    using GlobalListType = std::list<GlobalVariable*>;
    GlobalListType::iterator iterator_;

   public:
    GlobalVariable(Type* ty, bool isConstant,
                   LinkageTypes linkage = ExternalLinkage,
                   const std::string& name = "", Module* parent = nullptr,
                   Value* init = nullptr)
        : Constant(PointerType::get(ty), ValueKind::GlobalVariable),
          parent_(parent),
          isConstant_(isConstant),
          initializer_(init),
          linkage_(linkage) {
        setName(name);
    }

    static GlobalVariable* Create(Type* ty, bool isConstant,
                                  LinkageTypes linkage, Value* init,
                                  const std::string& name, Module* parent);

    Module* getParent() const { return parent_; }
    void setParent(Module* m) { parent_ = m; }

    bool isConstant() const { return isConstant_; }
    void setConstant(bool c) { isConstant_ = c; }

    LinkageTypes getLinkage() const { return linkage_; }
    void setLinkage(LinkageTypes l) { linkage_ = l; }

    bool hasInitializer() const { return initializer_ != nullptr; }
    Value* getInitializer() { return initializer_; }
    void setInitializer(Value* init) { initializer_ = init; }

    Type* getValueType() const {
        return static_cast<PointerType*>(getType())->getElementType();
    }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::GlobalVariable;
    }

    friend class Module;
    void setIterator(GlobalListType::iterator it) { iterator_ = it; }
    GlobalListType::iterator getIterator() { return iterator_; }
};

class Module {
   private:
    std::string name_;
    Context* context_;
    std::list<Function*> functions_;
    std::list<GlobalVariable*> globals_;
    std::string targetTriple_;
    std::string dataLayout_;

   public:
    Module(const std::string& name, Context* ctx)
        : name_(name), context_(ctx) {}

    ~Module();

    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    Context* getContext() const { return context_; }

    const std::string& getTargetTriple() const { return targetTriple_; }
    void setTargetTriple(const std::string& triple) { targetTriple_ = triple; }

    const std::string& getDataLayout() const { return dataLayout_; }
    void setDataLayout(const std::string& layout) { dataLayout_ = layout; }

    using iterator = std::list<Function*>::iterator;
    using const_iterator = std::list<Function*>::const_iterator;
    using global_iterator = std::list<GlobalVariable*>::iterator;
    using const_global_iterator = std::list<GlobalVariable*>::const_iterator;

    iterator begin() { return functions_.begin(); }
    iterator end() { return functions_.end(); }
    const_iterator begin() const { return functions_.begin(); }
    const_iterator end() const { return functions_.end(); }

    global_iterator global_begin() { return globals_.begin(); }
    global_iterator global_end() { return globals_.end(); }
    const_global_iterator global_begin() const { return globals_.begin(); }
    const_global_iterator global_end() const { return globals_.end(); }

    // Range-based iteration for globals
    std::list<GlobalVariable*>& globals() { return globals_; }
    const std::list<GlobalVariable*>& globals() const { return globals_; }

    bool empty() const { return functions_.empty(); }
    size_t size() const { return functions_.size(); }

    Function* front() { return functions_.front(); }
    const Function* front() const { return functions_.front(); }
    Function* back() { return functions_.back(); }
    const Function* back() const { return functions_.back(); }

    void push_back(Function* fn);
    void push_front(Function* fn);
    iterator insert(iterator pos, Function* fn);
    iterator erase(iterator pos);
    void remove(Function* fn);

    GlobalVariable* addGlobalVariable(GlobalVariable* gv);
    void removeGlobalVariable(GlobalVariable* gv);

    Function* getFunction(const std::string& name) const;
    GlobalVariable* getGlobalVariable(const std::string& name) const;

    std::string toString() const;
};

}  // namespace midend