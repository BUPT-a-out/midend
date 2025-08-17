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
    using RuntimeSet = std::unordered_set<Value*>;
    using ComptimeSet = std::unordered_set<Instruction*>;
    using VisitedSet = std::unordered_set<BasicBlock*>;

    ValueMap globalValueMap;
    RuntimeSet runtimeValues;   // Values that become runtime-dependent
    ComptimeSet comptimeInsts;  // Instructions confirmed as compile-time
    AnalysisManager* analysisManager = nullptr;

    bool isPropagation;

    void initializeGlobalValueMap(Module& module);

    // Unified evaluation function - pass ComptimeSet as nullptr for propagation
    std::pair<Value*, bool> evaluateFunction(
        Function* func, const std::vector<Value*>& args, bool isMainFunction,
        const ComptimeSet* comptimeSet = nullptr);

    void evaluateBlock(BasicBlock* block, BasicBlock* prevBlock,
                       ValueMap& valueMap, bool isMainFunction,
                       const ComptimeSet* comptimeSet = nullptr);

    void performRuntimePropagation(BasicBlock* startBlock, BasicBlock* endBlock,
                                   ValueMap& valueMap);

    Value* evaluateAllocaInst(AllocaInst* alloca, ValueMap& valueMap);
    Value* evaluateBinaryOp(BinaryOperator* binOp, ValueMap& valueMap);
    Value* evaluateUnaryOp(UnaryOperator* unOp, ValueMap& valueMap);
    Value* evaluateCmpInst(CmpInst* cmp, ValueMap& valueMap);
    Value* evaluateLoadInst(LoadInst* load, ValueMap& valueMap);
    Value* evaluateStoreInst(StoreInst* store, ValueMap& valueMap,
                             bool skipSideEffect);
    Value* evaluateGEP(GetElementPtrInst* gep, ValueMap& valueMap);
    std::pair<Value*, bool> evaluateCallInst(CallInst* call, ValueMap& valueMap,
                                             bool isMainFunction,
                                             bool skipSideEffect);
    Value* evaluateCastInst(CastInst* castInst, ValueMap& valueMap);

    void markAsRuntime(Value* value);
    void invalidateValuesFromCall(CallInst* call, ValueMap& valueMap);

    bool updateValueMap(Value* inst, Value* result, ValueMap& valueMap);

    void handlePHINodes(BasicBlock* block, BasicBlock* prevBlock,
                        ValueMap& valueMap);
    Value* evaluatePHINode(PHINode* phi, BasicBlock* prevBlock,
                           ValueMap& valueMap);

    size_t eliminateComputedInstructions(Function* func);
    void initializeValues(Module& module);
    void initializeLocalArray(Function* mainFunc, AllocaInst* alloca,
                              ConstantArray* arrayValue);

    Constant* createZeroInitializedConstant(Type* type);

    int countNonZeroElements(Constant* constant);
    int getTotalElements(Constant* constant);
    void collectFlatIndices(Constant* constant,
                            std::vector<std::pair<int, Constant*>>& indices,
                            bool onlyNonZero = false, int baseIndex = 0);

    Value* getValueOrConstant(Value* v, ValueMap& valueMap);

    BasicBlock* getPostImmediateDominator(BasicBlock* block);
};

}  // namespace midend