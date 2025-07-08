#include "IR/Function.h"

#include "IR/Module.h"
#include "IR/Type.h"

namespace midend {

Function::Function(FunctionType* ty, const std::string& name, Module* parent)
    : Constant(ty, ValueKind::Function), parent_(parent), isDeclaration_(true) {
    setName(name);

    if (parent_) {
        parent_->push_back(this);
    }

    const auto& paramTypes = ty->getParamTypes();
    arguments_.reserve(paramTypes.size());
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        arguments_.push_back(std::make_unique<Argument>(
            paramTypes[i], "arg" + std::to_string(i), this, i));
    }
}

Function::~Function() {
    for (auto* bb : basicBlocks_) {
        delete bb;
    }
}

FunctionType* Function::getFunctionType() const {
    return static_cast<FunctionType*>(getType());
}

Type* Function::getReturnType() const {
    return getFunctionType()->getReturnType();
}

void Function::push_back(BasicBlock* bb) {
    basicBlocks_.push_back(bb);
    bb->setParent(this);
    bb->setIterator(std::prev(basicBlocks_.end()));
    isDeclaration_ = false;
}

void Function::push_front(BasicBlock* bb) {
    basicBlocks_.push_front(bb);
    bb->setParent(this);
    bb->setIterator(basicBlocks_.begin());
    isDeclaration_ = false;
}

Function::iterator Function::insert(iterator pos, BasicBlock* bb) {
    auto it = basicBlocks_.insert(pos, bb);
    bb->setParent(this);
    bb->setIterator(it);
    isDeclaration_ = false;
    return it;
}

Function::iterator Function::erase(iterator pos) {
    (*pos)->setParent(nullptr);
    delete *pos;
    return basicBlocks_.erase(pos);
}

void Function::remove(BasicBlock* bb) {
    basicBlocks_.erase(bb->getIterator());
    bb->setParent(nullptr);
}

void Function::insertBefore(Function* fn) {
    if (!fn->getParent()) return;

    Module* mod = fn->getParent();
    mod->insert(fn->getIterator(), this);
}

void Function::insertAfter(Function* fn) {
    if (!fn->getParent()) return;

    Module* mod = fn->getParent();
    auto it = fn->getIterator();
    ++it;
    mod->insert(it, this);
}

void Function::moveBefore(Function* fn) {
    removeFromParent();
    insertBefore(fn);
}

void Function::moveAfter(Function* fn) {
    removeFromParent();
    insertAfter(fn);
}

void Function::removeFromParent() {
    if (parent_) {
        parent_->remove(this);
    }
}

void Function::eraseFromParent() {
    if (parent_) {
        parent_->erase(iterator_);
    }
}

std::vector<BasicBlock*> Function::getBasicBlocks() const {
    std::vector<BasicBlock*> blocks;
    blocks.reserve(basicBlocks_.size());
    for (auto* bb : basicBlocks_) {
        blocks.push_back(bb);
    }
    return blocks;
}

Function* Function::Create(FunctionType* ty, const std::string& name,
                           Module* parent) {
    return new Function(ty, name, parent);
}

}  // namespace midend