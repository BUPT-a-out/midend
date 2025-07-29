#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"

namespace midend {

class Loop;
class LoopInfo;

class Loop {
   public:
    using BlockSet = std::unordered_set<BasicBlock*>;
    using BlockVector = std::vector<BasicBlock*>;
    using LoopVector = std::vector<std::unique_ptr<Loop>>;

   private:
    BasicBlock* header_;
    Loop* parentLoop_;
    LoopVector subLoops_;
    BlockSet blocks_;
    BlockVector exitingBlocks_;
    BlockVector exitBlocks_;
    BasicBlock* preheader_;
    unsigned loopDepth_;

   public:
    explicit Loop(BasicBlock* header);

    /// Get the loop header (dominates all blocks in the loop)
    BasicBlock* getHeader() const { return header_; }

    /// Get the parent loop (null if this is a top-level loop)
    Loop* getParentLoop() const { return parentLoop_; }

    /// Set the parent loop
    void setParentLoop(Loop* parent) { parentLoop_ = parent; }

    /// Get all sub-loops
    const LoopVector& getSubLoops() const { return subLoops_; }
    LoopVector& getSubLoops() { return subLoops_; }

    /// Add a sub-loop
    void addSubLoop(std::unique_ptr<Loop> subLoop);

    /// Get all blocks in this loop (excluding sub-loops)
    const BlockSet& getBlocks() const { return blocks_; }

    /// Add a block to this loop
    void addBlock(BasicBlock* bb) { blocks_.insert(bb); }

    /// Remove a block from this loop
    void removeBlock(BasicBlock* bb) { blocks_.erase(bb); }

    /// Check if a block is in this loop
    bool contains(BasicBlock* bb) const { return blocks_.count(bb) > 0; }

    /// Check if this loop contains another loop
    bool contains(const Loop* L) const;

    /// Get the loop depth (0 for top-level loops)
    unsigned getLoopDepth() const { return loopDepth_; }

    /// Set the loop depth
    void setLoopDepth(unsigned depth) { loopDepth_ = depth; }

    /// Get blocks that exit the loop
    const BlockVector& getExitingBlocks() const { return exitingBlocks_; }

    /// Get blocks that are successors of exiting blocks (outside the loop)
    const BlockVector& getExitBlocks() const { return exitBlocks_; }

    /// Get the preheader block (single predecessor of header from outside loop)
    BasicBlock* getPreheader() const { return preheader_; }

    /// Compute and cache exit information
    void computeExitInfo();

    /// Find and set the preheader
    void computePreheader();

    /// Check if this is a simplified loop (single backedge, has preheader)
    bool isSimplified() const;

    /// Get all backedges to the header
    std::vector<BasicBlock*> getLoopLatches() const;

    /// Check if the loop has a single backedge
    bool hasSingleBackedge() const { return getLoopLatches().size() == 1; }

    // Iterator support for blocks
    using block_iterator = BlockSet::const_iterator;
    block_iterator block_begin() const { return blocks_.begin(); }
    block_iterator block_end() const { return blocks_.end(); }

    /// Print loop information for debugging
    void print() const;

   private:
    void computeExitingBlocks();
    void computeExitBlocks();
};

/// Analysis result that contains loop information for a function
class LoopInfo : public AnalysisResult {
   public:
    using LoopVector = std::vector<std::unique_ptr<Loop>>;

   private:
    Function* function_;
    LoopVector topLevelLoops_;
    std::unordered_map<BasicBlock*, Loop*> blockToLoop_;
    const DominanceInfo* domInfo_;

   public:
    explicit LoopInfo(Function* F, const DominanceInfo* DI);
    ~LoopInfo() = default;

    /// Get the function this loop info is for
    Function* getFunction() const { return function_; }

    /// Get all top-level loops
    const LoopVector& getTopLevelLoops() const { return topLevelLoops_; }

    /// Get the innermost loop that contains the given basic block
    Loop* getLoopFor(BasicBlock* BB) const;

    /// Get the loop depth of a basic block (0 if not in any loop)
    unsigned getLoopDepth(BasicBlock* BB) const;

    /// Check if a basic block is in a loop
    bool isLoopHeader(BasicBlock* BB) const;

    /// Add a new top-level loop
    void addTopLevelLoop(std::unique_ptr<Loop> L);

    /// Change the loop that contains a basic block
    void changeLoopFor(BasicBlock* BB, Loop* L);

    /// Remove a loop from the analysis
    void removeLoop(Loop* L);

    /// Verify the loop information is consistent
    bool verify() const;

    /// Print all loop information
    void print() const;

    // Iterator support
    using iterator = LoopVector::const_iterator;
    iterator begin() const { return topLevelLoops_.begin(); }
    iterator end() const { return topLevelLoops_.end(); }

    bool empty() const { return topLevelLoops_.empty(); }
    size_t size() const { return topLevelLoops_.size(); }

   private:
    /// Build loop information using dominance analysis
    void analyze();

    /// Discover natural loops using backedges
    void discoverLoops();

    /// Find all backedges in the CFG
    std::vector<std::pair<BasicBlock*, BasicBlock*>> findBackEdges();

    /// Build a natural loop from a backedge
    std::unique_ptr<Loop> buildLoop(BasicBlock* header, BasicBlock* latch);

    /// Populate a loop with all its blocks
    void populateLoop(Loop* L, BasicBlock* header, BasicBlock* latch);

    /// Organize loops into a hierarchy
    void buildLoopHierarchy();

    /// Compute additional loop properties
    void computeLoopProperties();
};

/// Analysis pass that computes loop information
class LoopAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<LoopInfo>;

    static const std::string& getName() {
        static const std::string name = "LoopAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override;

    /// Version that accepts an AnalysisManager to resolve dependencies
    std::unique_ptr<AnalysisResult> runOnFunction(Function& f,
                                                  AnalysisManager& AM) override;

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"DominanceAnalysis"};
    }

    static Result run(Function& F, const DominanceInfoBase<false>& DI) {
        return std::make_unique<LoopInfo>(&F, &DI);
    }
};

}  // namespace midend