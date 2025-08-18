#pragma once

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/Instructions/MemoryOps.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/MemorySSA.h"
#include "Pass/Pass.h"

namespace midend {

class Function;
class BasicBlock;
class Instruction;
class Value;
class PHINode;
template <bool>
class DominanceInfoBase;
using DominanceInfo = DominanceInfoBase<false>;
class CallGraph;

class GVNPass : public FunctionPass {
   public:
    GVNPass() : FunctionPass("GVNPass", "Global Value Numbering Pass") {}
    ~GVNPass() = default;

    static const std::string& getName() {
        static const std::string name = "GVNPass";
        return name;
    }

    bool runOnFunction(Function& F, AnalysisManager& AM) override;

    std::vector<std::string> getDependencies() const {
        return {"DominanceAnalysis", "CallGraphAnalysis", "AliasAnalysis"};
    }

    void init() {}

   private:
    // Expression representation for value numbering
    struct Expression {
        unsigned opcode;
        std::vector<unsigned> operands;  // Value numbers of operands
        Value* constant = nullptr;       // For constant values
        Value* memoryPtr = nullptr;      // For load instructions
        unsigned memoryState = 0;        // Memory SSA state for loads

        bool operator==(const Expression& other) const;
    };

    struct ExpressionHash {
        std::size_t operator()(const Expression& expr) const;
    };

    std::unordered_map<unsigned, Value*> valueNumberToValue;
    std::unordered_map<Expression, unsigned, ExpressionHash>
        expressionToValueNumber;
    std::unordered_map<Value*, unsigned> valueToNumber;
    unsigned nextValueNumber = 1;

    DominanceInfo* DI = nullptr;
    CallGraph* CG = nullptr;
    AliasAnalysis::Result* AA = nullptr;
    MemorySSA* MSSA = nullptr;

    unsigned numGVNEliminated = 0;
    unsigned numPHIEliminated = 0;
    unsigned numLoadEliminated = 0;
    unsigned numCallEliminated = 0;

    bool processFunction(Function& F);
    bool processBlock(BasicBlock* BB);
    bool processInstruction(Instruction* I);

    unsigned getValueNumber(Value* V);
    unsigned createValueNumber(Value* V);
    Expression createExpression(Instruction* I);
    void normalizeCommutativeExpression(Expression& expr);
    bool isCommutative(unsigned opcode);

    bool eliminateRedundancy(Instruction* I, const Expression& expr);
    bool eliminatePHIRedundancy(PHINode* PHI);

    bool processMemoryInstruction(Instruction* I);
    bool eliminateLoadRedundancy(Instruction* Load);
    Value* findAvailableLoad(Instruction* Load, BasicBlock* BB);
    bool hasInterveningStore(Instruction* availLoad, Instruction* currentLoad,
                             Value* ptr);
    void recordAvailableLoad(Instruction* Load);
    void invalidateLoads(Instruction* Store);

    // Memory SSA enhanced methods
    bool processMemoryInstructionWithMSSA(Instruction* I);
    bool eliminateLoadRedundancyWithMSSA(Instruction* Load);
    Value* findAvailableLoadWithMSSA(LoadInst* LI);
    Value* findLoadValueInPredecessors(LoadInst* LI, BasicBlock* BB);
    bool canEliminateLoadWithMSSA(LoadInst* LI, MemoryAccess* clobber);
    Value* insertPhiForLoadValue(
        LoadInst* LI,
        const std::vector<std::pair<BasicBlock*, Value*>>& incomingValues);
    unsigned getMemoryStateValueNumber(MemoryAccess* access);
    Expression createMemoryExpression(LoadInst* LI);

    // Array and GEP specific optimizations
    bool analyzeGEPAccess(GetElementPtrInst* GEP, LoadInst* LI);
    bool isConstantGEP(GetElementPtrInst* GEP);
    int64_t computeGEPOffset(GetElementPtrInst* GEP);

    // Cross-block optimization helpers
    struct LoadValueInfo {
        Value* value;
        BasicBlock* block;
        MemoryAccess* memoryState;
        bool isValid;
    };

    std::vector<LoadValueInfo> collectLoadValuesFromPredecessors(
        LoadInst* LI, BasicBlock* BB);

    bool processFunctionCall(Instruction* Call);
    bool isPureFunction(Function* F);
    Expression createCallExpression(Instruction* Call);

    void buildGlobalValueNumbers();
    bool isValueAvailable(Value* V, BasicBlock* BB);
    Value* findLeader(unsigned valueNumber, BasicBlock* BB);

    unsigned computePHIValueNumber(PHINode* PHI);

    class UnionFind {
       private:
        std::unordered_map<unsigned, unsigned> parent;
        std::unordered_map<unsigned, int> rank;

       public:
        unsigned find(unsigned x);
        void unite(unsigned x, unsigned y);
        bool connected(unsigned x, unsigned y);
        void makeSet(unsigned x);
    };

    UnionFind equivalenceClasses;

    struct BlockInfo {
        std::unordered_map<Expression, unsigned, ExpressionHash>
            availableExpressions;
        std::unordered_set<unsigned> availableValues;
        std::vector<std::pair<Value*, Instruction*>>
            availableLoads;  // ptr -> load inst
    };

    std::unordered_map<BasicBlock*, BlockInfo> blockInfoMap;

    // Memory SSA specific data structures
    std::unordered_map<MemoryAccess*, unsigned> memoryAccessToValueNumber;
    std::unordered_map<LoadInst*, std::vector<LoadValueInfo>> loadValueCache;

    struct LoadBlockPair {
        LoadInst* load;
        BasicBlock* block;

        bool operator==(const LoadBlockPair& other) const {
            return load == other.load && block == other.block;
        }
    };

    struct LoadBlockPairHash {
        std::size_t operator()(const LoadBlockPair& pair) const {
            return std::hash<void*>()(pair.load) ^
                   (std::hash<void*>()(pair.block) << 1);
        }
    };

    std::unordered_map<LoadBlockPair, Value*, LoadBlockPairHash>
        crossBlockLoadCache;

    bool isSafeToEliminate(Instruction* I);
    bool hasMemoryEffects(Instruction* I);
    bool dominates(Value* V, Instruction* I);
    void replaceAndErase(Instruction* I, Value* replacement);
    void propagateEquivalence(unsigned vn1, unsigned vn2);
    Value* trySimplifyInstruction(Instruction* I);

    void performIterativeGVN(Function& F);
    bool transferFunction(BasicBlock* BB);
    void meetOperator(BasicBlock* BB);

    std::queue<BasicBlock*> workList;
    std::unordered_set<BasicBlock*> inWorkList;

    void dumpValueNumbering() const;
    bool verifyGVN() const;
};

}  // namespace midend