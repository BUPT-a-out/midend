#include "Pass/Transform/StoreGlobalize.h"

#include <iostream>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Module.h"
#include "Support/Casting.h"

namespace midend {

bool StoreGlobalize::runOnModule(Module& module, AnalysisManager& am) {
    Function* mainFunc = findMainFunction(module);
    if (!mainFunc) {
        return false;
    }

    if (mainFunc->isDeclaration() || mainFunc->empty()) {
        return false;
    }

    std::vector<AllocaInst*> allocas = collectAllocasFromEntry(mainFunc);
    if (allocas.empty()) {
        return false;
    }

    gv_id = 0;

    std::vector<std::pair<AllocaInst*, GlobalVariable*>> replacements;
    for (AllocaInst* alloca : allocas) {
        GlobalVariable* global = createGlobalFromAlloca(alloca, module);
        replacements.push_back({alloca, global});
    }

    for (auto& [alloca, global] : replacements) {
        alloca->replaceAllUsesWith(global);
        alloca->eraseFromParent();
    }

    return true;
}

Function* StoreGlobalize::findMainFunction(Module& module) {
    for (Function* func : module) {
        if (func->getName() == "main") {
            return func;
        }
    }
    return nullptr;
}

std::vector<AllocaInst*> StoreGlobalize::collectAllocasFromEntry(
    Function* mainFunc) {
    std::vector<AllocaInst*> allocas;

    BasicBlock& entryBlock = mainFunc->front();
    for (Instruction* inst : entryBlock) {
        if (auto* alloca = dyn_cast<AllocaInst>(inst)) {
            allocas.push_back(alloca);
        }
    }

    return allocas;
}

GlobalVariable* StoreGlobalize::createGlobalFromAlloca(AllocaInst* alloca,
                                                       Module& module) {
    Type* allocatedType = alloca->getAllocatedType();

    std::string baseName = alloca->getName();
    if (baseName.empty()) {
        baseName = "unnamed_alloca.";
    }

    std::string globalName = baseName + ".global." + std::to_string(gv_id++);
    return GlobalVariable::Create(allocatedType, false,
                                  GlobalVariable::InternalLinkage, nullptr,
                                  globalName, &module);
}

REGISTER_PASS(StoreGlobalize, "store-globalize");

}  // namespace midend