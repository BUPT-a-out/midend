#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Pass/Pass.h"

namespace midend {

class Value;
class Instruction;
class Constant;
class Function;
class BasicBlock;
class PHINode;
class BinaryOperator;
class CmpInst;
class SelectInst;

class ConstantPropagationPass : public FunctionPass {
   public:
    ConstantPropagationPass()
        : FunctionPass("ConstantPropagation",
                       "Propagate constant values and fold constant "
                       "expressions") {}

    void getAnalysisUsage(std::unordered_set<std::string>& required,
                          std::unordered_set<std::string>& preserved) const override;

    bool runOnFunction(Function& f, AnalysisManager& am) override;

   private:
    std::unordered_map<Value*, Constant*> latticeValues_;
    std::vector<Instruction*> workList_;
    std::unordered_set<Instruction*> inWorkList_;
    std::vector<Instruction*> instructionsToRemove_;

    void initializeLattice(Function& f);
    void propagateConstants();
    bool processInstruction(Instruction* inst);
    
    Constant* evaluateInstruction(Instruction* inst);
    Constant* evaluateBinaryOp(BinaryOperator* binOp);
    Constant* evaluateCmpInst(CmpInst* cmp);
    Constant* evaluateSelectInst(SelectInst* select);
    Constant* evaluatePHINode(PHINode* phi);
    
    Constant* getLatticeValue(Value* v);
    void setLatticeValue(Value* v, Constant* c);
    
    void addToWorkList(Instruction* inst);
    void addUsersToWorkList(Value* v);
    
    void replaceWithConstant(Instruction* inst, Constant* c);
    void cleanupDeadInstructions();
    
    static Constant* foldBinaryOp(unsigned opcode, Constant* lhs, Constant* rhs);
    static Constant* foldCmpInst(unsigned predicate, Constant* lhs, Constant* rhs);
};

}  // namespace midend