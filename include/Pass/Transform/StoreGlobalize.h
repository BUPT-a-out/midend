#pragma once

#include <vector>

#include "Pass/Pass.h"

namespace midend {

class AllocaInst;
class Function;
class GlobalVariable;

class StoreGlobalize : public ModulePass {
   public:
    StoreGlobalize()
        : ModulePass("StoreGlobalize",
                     "Move alloca instructions from main function to global "
                     "variables") {}

    bool runOnModule(Module& module, AnalysisManager& am) override;

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {}

   private:
    Function* findMainFunction(Module& module);
    std::vector<AllocaInst*> collectAllocasFromEntry(Function* mainFunc);
    GlobalVariable* createGlobalFromAlloca(AllocaInst* alloca, Module& module);

    int gv_id;
};

}  // namespace midend