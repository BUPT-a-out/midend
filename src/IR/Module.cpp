#include "IR/Module.h"

namespace midend {

GlobalVariable* GlobalVariable::Create(Type* ty, bool isConstant,
                                       LinkageTypes linkage, Value* init,
                                       const std::string& name,
                                       Module* parent) {
    auto* global =
        new GlobalVariable(ty, isConstant, linkage, name, parent, init);
    if (parent) {
        parent->addGlobalVariable(global);
    }
    return global;
}

Module::~Module() {
    for (auto* fn : functions_) {
        delete fn;
    }
    for (auto* gv : globals_) {
        delete gv;
    }
}

void Module::push_back(Function* fn) {
    functions_.push_back(fn);
    fn->setParent(this);
    fn->setIterator(std::prev(functions_.end()));
}

void Module::push_front(Function* fn) {
    functions_.push_front(fn);
    fn->setParent(this);
    fn->setIterator(functions_.begin());
}

Module::iterator Module::insert(iterator pos, Function* fn) {
    auto it = functions_.insert(pos, fn);
    fn->setParent(this);
    fn->setIterator(it);
    return it;
}

Module::iterator Module::erase(iterator pos) {
    (*pos)->setParent(nullptr);
    delete *pos;
    return functions_.erase(pos);
}

void Module::remove(Function* fn) {
    functions_.erase(fn->getIterator());
    fn->setParent(nullptr);
}

void Module::addGlobalVariable(GlobalVariable* gv) {
    globals_.push_back(gv);
    gv->setParent(this);
    gv->setIterator(std::prev(globals_.end()));
}

void Module::removeGlobalVariable(GlobalVariable* gv) {
    globals_.erase(gv->getIterator());
    gv->setParent(nullptr);
}

Function* Module::getFunction(const std::string& name) const {
    for (auto* fn : functions_) {
        if (fn->getName() == name) {
            return fn;
        }
    }
    return nullptr;
}

GlobalVariable* Module::getGlobalVariable(const std::string& name) const {
    for (auto* gv : globals_) {
        if (gv->getName() == name) {
            return gv;
        }
    }
    return nullptr;
}

std::string Module::toString() const {
    std::string result = "; ModuleID = '" + name_ + "'\n";

    // Add functions
    for (const auto* func : functions_) {
        if (func) {
            result += "declare " + func->getName() + "\n";
        }
    }

    return result;
}

}  // namespace midend