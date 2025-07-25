#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Pass/Pass.h"

namespace midend {

class Value;
class Function;
class Module;
class AllocaInst;
class GetElementPtrInst;
class Instruction;

class AliasAnalysis : public AnalysisBase {
   public:
    enum class AliasResult {
        NoAlias,    // The two references cannot alias
        MustAlias,  // The two references always alias
        MayAlias    // The two references may alias
    };

    struct Location {
        Value* ptr;
        uint64_t size;

        Location(Value* p, uint64_t s = 0) : ptr(p), size(s) {}

        bool operator==(const Location& other) const {
            return ptr == other.ptr && size == other.size;
        }
    };

    struct LocationHash {
        std::size_t operator()(const Location& loc) const {
            return std::hash<void*>()(loc.ptr) ^
                   std::hash<uint64_t>()(loc.size);
        }
    };

    class Result : public AnalysisResult {
       public:
        explicit Result(Function* f) : function(f) {}

        // Main alias query interface
        AliasResult alias(const Location& loc1, const Location& loc2);
        AliasResult alias(Value* v1, Value* v2);
        AliasResult alias(Value* v1, uint64_t size1, Value* v2, uint64_t size2);

        // Check if a value may be modified by an instruction
        bool mayModify(Instruction* inst, const Location& loc);
        bool mayModify(Instruction* inst, Value* ptr);

        // Check if a value may be referenced by an instruction
        bool mayRef(Instruction* inst, const Location& loc);
        bool mayRef(Instruction* inst, Value* ptr);

        // Get all values that may alias with a given value
        std::vector<Value*> getMayAliasSet(Value* v);

        // Get the underlying object (allocation site) for a pointer
        Value* getUnderlyingObject(Value* v);

        // Check if a value is derived from a stack allocation
        bool isStackObject(Value* v);

        // Check if a value is derived from a global variable
        bool isGlobalObject(Value* v);

        // Check if two values are derived from different allocations
        bool isDifferentObject(Value* v1, Value* v2);

       private:
        Function* function;

        // Cache for alias query results
        struct PairHash {
            std::size_t operator()(
                const std::pair<Location, Location>& p) const {
                auto h1 = LocationHash()(p.first);
                auto h2 = LocationHash()(p.second);
                return h1 ^ (h2 << 1);
            }
        };
        mutable std::unordered_map<std::pair<Location, Location>, AliasResult,
                                   PairHash>
            aliasCache;

        // Points-to sets for flow-insensitive analysis
        std::unordered_map<Value*, std::unordered_set<Value*>> pointsToSets;

        // Memory allocation sites
        std::unordered_set<Value*> allocSites;

        // Helper methods for alias analysis
        AliasResult aliasInternal(const Location& loc1, const Location& loc2);
        AliasResult aliasGEP(GetElementPtrInst* gep1, uint64_t size1,
                             GetElementPtrInst* gep2, uint64_t size2);
        AliasResult aliasPHI(Value* v1, uint64_t size1, Value* v2,
                             uint64_t size2);

        // Offset computation for GEP instructions
        bool computeGEPOffset(GetElementPtrInst* gep, int64_t& offset);

        // Build points-to sets using Andersen's algorithm
        void buildPointsToSets();

        // Add constraints for Andersen's algorithm
        void addConstraints();

        friend class AliasAnalysis;
    };

    static const std::string& getName() {
        static std::string name = "AliasAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override;
    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override;

    bool supportsFunction() const override { return true; }
    bool supportsModule() const override { return true; }
};

}  // namespace midend