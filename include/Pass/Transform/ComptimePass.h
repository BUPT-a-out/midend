#pragma once

#include <memory>
#include <unordered_set>
#include <vector>

#include "Pass/Pass.h"
#include "flat_hash_map/unordered_map.hpp"

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
    using ValuePtr = std::shared_ptr<Value>;

    ComptimePass() : ModulePass("ComptimePass", "Compile-Time Evaluation") {}

    bool runOnModule(Module& module, AnalysisManager& am) override;

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override;

   private:
    using ValueMap = ska::unordered_map<Value*, ValuePtr>;
    using RuntimeSet = ska::unordered_set<Value*>;
    using ComptimeSet = ska::unordered_set<Instruction*>;
    using VisitedSet = ska::unordered_set<BasicBlock*>;

    ValueMap globalValueMap;
    RuntimeSet runtimeValues;   // Values that become runtime-dependent
    ComptimeSet comptimeInsts;  // Instructions confirmed as compile-time
    AnalysisManager* analysisManager = nullptr;

    bool isPropagation;

    void initializeGlobalValueMap(Module& module);

    // Unified evaluation function - pass ComptimeSet as nullptr for propagation
    std::pair<ValuePtr, bool> evaluateFunction(
        Function* func, const std::vector<ValuePtr>& args, bool isMainFunction,
        const ComptimeSet* comptimeSet, ValueMap& upperValueMap,
        const std::vector<Value*>& argsRef);

    bool evaluateBlock(BasicBlock* block, BasicBlock* prevBlock,
                       ValueMap& valueMap, bool isMainFunction,
                       const ComptimeSet* comptimeSet = nullptr);

    void performRuntimePropagation(BasicBlock* startBlock, BasicBlock* endBlock,
                                   ValueMap& valueMap);

    ValuePtr evaluateAllocaInst(AllocaInst* alloca, ValueMap& valueMap);
    ValuePtr evaluateBinaryOp(BinaryOperator* binOp, ValueMap& valueMap);
    ValuePtr evaluateUnaryOp(UnaryOperator* unOp, ValueMap& valueMap);
    ValuePtr evaluateCmpInst(CmpInst* cmp, ValueMap& valueMap);
    ValuePtr evaluateLoadInst(LoadInst* load, ValueMap& valueMap);
    ValuePtr evaluateStoreInst(StoreInst* store, ValueMap& valueMap,
                               bool skipSideEffect);
    ValuePtr evaluateGEP(GetElementPtrInst* gep, ValueMap& valueMap);
    std::pair<ValuePtr, bool> evaluateCallInst(CallInst* call,
                                               ValueMap& valueMap,
                                               bool isMainFunction,
                                               bool skipSideEffect);
    ValuePtr evaluateCastInst(CastInst* castInst, ValueMap& valueMap);

    void markAsRuntime(Value* value, ValueMap& valueMap, bool no_gep = false);
    void invalidateValuesFromCall(CallInst* call, ValueMap& valueMap);

    bool updateValueMap(Value* inst, ValuePtr result, ValueMap& valueMap);

    void handlePHINodes(BasicBlock* block, BasicBlock* prevBlock,
                        ValueMap& valueMap);
    ValuePtr evaluatePHINode(PHINode* phi, BasicBlock* prevBlock,
                             ValueMap& valueMap);

    size_t eliminateComputedInstructions(Function* func);
    bool initializeValues(Module& module);
    void initializeLocalArray(Function* mainFunc, AllocaInst* alloca,
                              ConstantArray* arrayValue);

    Constant* createZeroInitializedConstant(Type* type);

    int countNonZeroElements(Constant* constant);
    int getTotalElements(Constant* constant);
    void collectFlatIndices(Constant* constant,
                            std::vector<std::pair<int, Constant*>>& indices,
                            bool onlyNonZero = false, int baseIndex = 0);

    ValuePtr getValueOrConstant(Value* v, ValueMap& valueMap);

    BasicBlock* getPostImmediateDominator(BasicBlock* block);
};

}  // namespace midend