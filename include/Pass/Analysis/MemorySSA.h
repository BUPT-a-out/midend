#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions/OtherOps.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"

namespace midend {

// Forward declarations for Memory SSA classes
class MemoryAccess;
class MemoryUse;
class MemoryDef;
class MemoryPhi;
class MemorySSAWalker;

/// Base class for all memory accesses in Memory SSA form
class MemoryAccess {
   public:
    enum MemoryAccessKind { MemoryUseKind, MemoryDefKind, MemoryPhiKind };

   private:
    MemoryAccessKind kind_;
    BasicBlock* block_;
    unsigned id_;
    static unsigned nextId_;

   protected:
    MemoryAccess(MemoryAccessKind kind, BasicBlock* block)
        : kind_(kind), block_(block), id_(nextId_++) {}

   public:
    virtual ~MemoryAccess() = default;

    MemoryAccessKind getKind() const { return kind_; }
    BasicBlock* getBlock() const { return block_; }
    unsigned getID() const { return id_; }

    bool isMemoryUse() const { return kind_ == MemoryUseKind; }
    bool isMemoryDef() const { return kind_ == MemoryDefKind; }
    bool isMemoryPhi() const { return kind_ == MemoryPhiKind; }

    virtual void print() const = 0;
};

/// Represents a memory operation that reads from memory
class MemoryUse : public MemoryAccess {
   private:
    Instruction* memoryInst_;
    MemoryAccess* definingAccess_;

   public:
    MemoryUse(Instruction* inst, MemoryAccess* definingAccess,
              BasicBlock* block)
        : MemoryAccess(MemoryUseKind, block),
          memoryInst_(inst),
          definingAccess_(definingAccess) {}

    Instruction* getMemoryInst() const { return memoryInst_; }
    MemoryAccess* getDefiningAccess() const { return definingAccess_; }
    void setDefiningAccess(MemoryAccess* access) { definingAccess_ = access; }

    void print() const override;

    static bool classof(const MemoryAccess* MA) {
        return MA->getKind() == MemoryUseKind;
    }
};

/// Represents a memory operation that writes to memory or introduces ordering
/// constraints
class MemoryDef : public MemoryAccess {
   private:
    Instruction* memoryInst_;
    MemoryAccess* definingAccess_;

   public:
    MemoryDef(Instruction* inst, MemoryAccess* definingAccess,
              BasicBlock* block)
        : MemoryAccess(MemoryDefKind, block),
          memoryInst_(inst),
          definingAccess_(definingAccess) {}

    Instruction* getMemoryInst() const { return memoryInst_; }
    MemoryAccess* getDefiningAccess() const { return definingAccess_; }
    void setDefiningAccess(MemoryAccess* access) { definingAccess_ = access; }

    void print() const override;

    static bool classof(const MemoryAccess* MA) {
        return MA->getKind() == MemoryDefKind;
    }
};

/// Represents a phi node for memory SSA, merging memory states at control flow
/// joins
class MemoryPhi : public MemoryAccess {
   private:
    std::vector<std::pair<MemoryAccess*, BasicBlock*>> operands_;

   public:
    explicit MemoryPhi(BasicBlock* block)
        : MemoryAccess(MemoryPhiKind, block) {}

    void addIncoming(MemoryAccess* access, BasicBlock* block) {
        operands_.emplace_back(access, block);
    }

    unsigned getNumIncomingValues() const { return operands_.size(); }

    MemoryAccess* getIncomingValue(unsigned i) const {
        return i < operands_.size() ? operands_[i].first : nullptr;
    }

    BasicBlock* getIncomingBlock(unsigned i) const {
        return i < operands_.size() ? operands_[i].second : nullptr;
    }

    MemoryAccess* getIncomingValueForBlock(BasicBlock* block) const {
        for (const auto& operand : operands_) {
            if (operand.second == block) {
                return operand.first;
            }
        }
        return nullptr;
    }

    void setIncomingValue(unsigned i, MemoryAccess* access) {
        if (i < operands_.size()) {
            operands_[i].first = access;
        }
    }

    const std::vector<std::pair<MemoryAccess*, BasicBlock*>>& getOperands()
        const {
        return operands_;
    }

    void print() const override;

    static bool classof(const MemoryAccess* MA) {
        return MA->getKind() == MemoryPhiKind;
    }
};

/// Walker interface for efficiently querying memory dependencies
class MemorySSAWalker {
   public:
    virtual ~MemorySSAWalker() = default;

    /// Find the memory access that clobbers the given memory use or def
    virtual MemoryAccess* getClobberingMemoryAccess(MemoryAccess* ma) = 0;

    /// Find the memory access that clobbers a memory location at a given
    /// instruction
    virtual MemoryAccess* getClobberingMemoryAccess(
        Instruction* inst, MemoryAccess* startAccess) = 0;
};

/// Basic walker implementation using dominance and alias analysis
class CachingWalker : public MemorySSAWalker {
   private:
    class MemorySSA* mssa_;
    const DominanceInfo* domInfo_;
    const AliasAnalysis::Result* aliasAnalysis_;

    // Cache for clobbering queries
    mutable std::unordered_map<MemoryAccess*, MemoryAccess*> walkCache_;

   public:
    CachingWalker(class MemorySSA* mssa, const DominanceInfo* di,
                  const AliasAnalysis::Result* aa)
        : mssa_(mssa), domInfo_(di), aliasAnalysis_(aa) {}

    MemoryAccess* getClobberingMemoryAccess(MemoryAccess* ma) override;
    MemoryAccess* getClobberingMemoryAccess(Instruction* inst,
                                            MemoryAccess* startAccess) override;

   private:
    MemoryAccess* walkToNextClobber(MemoryAccess* start, Instruction* inst);
    bool clobbers(MemoryAccess* access, Instruction* inst);
    void invalidateCache();
};

/// Main Memory SSA analysis result
class MemorySSA : public AnalysisResult {
   public:
    using AccessMap = std::unordered_map<Instruction*, MemoryAccess*>;
    using PhiMap = std::unordered_map<BasicBlock*, MemoryPhi*>;

   private:
    Function* function_;
    const DominanceInfo* domInfo_;
    const AliasAnalysis::Result* aliasAnalysis_;

    // Core data structures
    AccessMap memoryAccesses_;
    PhiMap memoryPhis_;
    std::vector<std::unique_ptr<MemoryAccess>> accessStorage_;

    // Special access representing initial memory state
    std::unique_ptr<MemoryDef> liveOnEntry_;

    // Walker for dependency queries
    std::unique_ptr<MemorySSAWalker> walker_;

    // Analysis state during construction
    std::unordered_map<BasicBlock*, MemoryAccess*> blockLastDef_;
    std::unordered_set<BasicBlock*> phiBlocks_;

   public:
    explicit MemorySSA(Function* f, const DominanceInfo* di,
                       const AliasAnalysis::Result* aa);
    ~MemorySSA();

    // Main query interface
    MemoryAccess* getMemoryAccess(Instruction* inst) const;
    MemoryPhi* getMemoryPhi(BasicBlock* block) const;
    MemoryDef* getLiveOnEntry() const { return liveOnEntry_.get(); }

    // Walker-based queries
    MemoryAccess* getClobberingMemoryAccess(MemoryAccess* ma);
    MemoryAccess* getClobberingMemoryAccess(Instruction* inst);

    // Utility functions
    Function* getFunction() const { return function_; }
    const DominanceInfo* getDominanceInfo() const { return domInfo_; }
    const AliasAnalysis::Result* getAliasAnalysis() const {
        return aliasAnalysis_;
    }

    // Iteration support
    const AccessMap& getMemoryAccesses() const { return memoryAccesses_; }
    const PhiMap& getMemoryPhis() const { return memoryPhis_; }

    // Debug support
    void print() const;
    bool verify() const;

   private:
    // Construction helpers
    void buildMemorySSA();
    void insertMemoryPhis();
    void renameMemoryAccesses();
    void populateInitialMemoryState();

    // Phi insertion helpers
    void findPhiBlocks();
    void insertPhiNodes();

    // Memory access creation
    MemoryUse* createMemoryUse(Instruction* inst, MemoryAccess* definingAccess);
    MemoryDef* createMemoryDef(Instruction* inst, MemoryAccess* definingAccess);
    MemoryPhi* createMemoryPhi(BasicBlock* block);

    // Utility functions
    bool isMemoryOperation(Instruction* inst) const;
    bool definesMemory(Instruction* inst) const;
    bool usesMemory(Instruction* inst) const;

    friend class CachingWalker;
};

/// Analysis pass that computes Memory SSA information
class MemorySSAAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<MemorySSA>;

    static const std::string& getName() {
        static const std::string name = "MemorySSAAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f,
                                                  AnalysisManager& AM) override;

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"DominanceAnalysis", "AliasAnalysis"};
    }

    static Result run(Function& F, AnalysisManager& AM);
};

}  // namespace midend