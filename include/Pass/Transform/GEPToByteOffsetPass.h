#pragma once

#include "Pass/Pass.h"

namespace midend {

class Function;
class GetElementPtrInst;
class Instruction;
class IRBuilder;

class GEPToByteOffsetPass : public FunctionPass {
   public:
    GEPToByteOffsetPass()
        : FunctionPass("GEPToByteOffsetPass", 
                      "Convert GEP indices to byte offsets") {}

    bool runOnFunction(Function& function, AnalysisManager& am) override;

   private:
    bool transformGEPs(Function& function);
    bool transformGEP(GetElementPtrInst* gep);
};

}  // namespace midend