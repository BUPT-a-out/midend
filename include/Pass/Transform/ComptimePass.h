#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Pass/Pass.h"

namespace midend {

class Value;
class Function;
class BasicBlock;
class Instruction;
class Module;
class AnalysisManager;
class Constant;
class PHINode;
class Type;
class AllocaInst;
class BinaryOperator;
class UnaryOperator;
class CmpInst;
class LoadInst;
class StoreInst;
class GetElementPtrInst;
class CallInst;
class CastInst;
class ConstantArray;

class ComptimePass : public ModulePass {
   public:
    ComptimePass() : ModulePass("ComptimePass", "Compile-Time Evaluation") {}

    bool runOnModule(Module& module, AnalysisManager& am) override;

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override;

   private:
    using ValueMap = std::unordered_map<Value*, Value*>;
    using ChangedSet = std::unordered_set<Value*>;

    ValueMap globalValueMap;
    ChangedSet changedValues;
    AnalysisManager* analysisManager = nullptr;

    void initializeGlobalValueMap(Module& module);
    Value* evaluateFunction(Function* func, const std::vector<Value*>& args,
                            bool isMainFunction = false);

    void evaluateBlock(BasicBlock* block, BasicBlock* prevBlock,
                       ValueMap& valueMap, bool isMainFunction);

    Value* evaluateAllocaInst(AllocaInst* alloca, ValueMap& valueMap);
    Value* evaluateBinaryOp(BinaryOperator* binOp, ValueMap& valueMap);
    Value* evaluateUnaryOp(UnaryOperator* unOp, ValueMap& valueMap);
    Value* evaluateCmpInst(CmpInst* cmp, ValueMap& valueMap);
    Value* evaluateLoadInst(LoadInst* load, ValueMap& valueMap);
    void evaluateStoreInst(StoreInst* store, ValueMap& valueMap);
    Value* evaluateGEP(GetElementPtrInst* gep, ValueMap& valueMap);
    Value* evaluateCallInst(CallInst* call, ValueMap& valueMap,
                            bool isMainFunction);
    Value* evaluateCastInst(CastInst* castInst, ValueMap& valueMap);

    void handlePHINodes(BasicBlock* block, BasicBlock* prevBlock,
                        ValueMap& valueMap);
    void evaluatePHINode(PHINode* phi, BasicBlock* prevBlock,
                         ValueMap& valueMap);

    size_t eliminateComputedInstructions(Function* func);
    void initializeArrays(Module& module);
    void initializeLocalArray(Function* mainFunc, AllocaInst* alloca,
                              ConstantArray* arrayValue);

    Constant* createZeroInitializedConstant(Type* type);
    Value* getFromNestedArray(Value* array, const std::vector<Value*>& indices);

    int countNonZeroElements(Constant* constant);
    int getTotalElements(Constant* constant);
    void collectFlatIndices(Constant* constant,
                            std::vector<std::pair<int, Constant*>>& indices,
                            bool onlyNonZero = false, int baseIndex = 0);

    Value* getValueOrConstant(Value* v, ValueMap& valueMap);

    BasicBlock* getPostImmediateDominator(BasicBlock* block);
};

}  // namespace midend