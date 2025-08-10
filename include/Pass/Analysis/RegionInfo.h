#pragma once

#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"

namespace midend {

class Region;
class RegionInfo;

/// Iterator for BFS traversal of blocks within a region
class RegionBlockIterator {
   private:
    BasicBlock* entry_;
    BasicBlock* exit_;
    std::queue<BasicBlock*> queue_;
    std::unordered_set<BasicBlock*> visited_;
    BasicBlock* current_;
    bool atEnd_;

    void advance();

   public:
    RegionBlockIterator(BasicBlock* entry, BasicBlock* exit);
    RegionBlockIterator();

    BasicBlock* operator*() const { return current_; }
    RegionBlockIterator& operator++();
    bool operator==(const RegionBlockIterator& other) const;
    bool operator!=(const RegionBlockIterator& other) const;
};

/// Represents a Single Entry Single Exit (SESE) region in the CFG
class Region {
   private:
    BasicBlock* entry_;
    BasicBlock* exit_;
    Region* parent_;
    std::vector<Region*> subRegions_;
    unsigned level_;

   public:
    Region(BasicBlock* entry, BasicBlock* exit);

    /// Get the entry block of this region
    BasicBlock* getEntry() const { return entry_; }

    /// Get the exit block of this region (may be null for top-level region)
    BasicBlock* getExit() const { return exit_; }

    /// Set the entry block
    void setEntry(BasicBlock* entry) { entry_ = entry; }

    /// Set the exit block
    void setExit(BasicBlock* exit) { entry_ = exit; }

    /// Get all direct sub-regions
    const std::vector<Region*>& getSubRegions() const { return subRegions_; }

    /// Add a sub-region
    void addSubRegion(Region* subRegion);

    /// Remove a sub-region
    void removeSubRegion(Region* subRegion);

    /// Replace a sub-region with multiple new regions, preserving order
    void replaceSubRegion(Region* oldRegion,
                          const std::vector<Region*>& newRegions);

    /// Get iterator for all blocks in this region (entry to exit, excluding
    /// exit)
    RegionBlockIterator blocks() const {
        return RegionBlockIterator(entry_, exit_);
    }

    /// Get end iterator for blocks
    RegionBlockIterator blocks_end() const { return RegionBlockIterator(); }

    /// Set parent region and update level
    void setParent(Region* parent);

    /// Get parent region
    Region* getParent() const { return parent_; }

    /// Get nesting level of this region
    unsigned getLevel() const { return level_; }

    /// Print region information for debugging
    void print() const;
};

/// Analysis result that contains region information for a function
class RegionInfo : public AnalysisResult {
   private:
    Function* function_;
    std::unique_ptr<Region> topRegion_;
    std::unordered_map<Region*, std::unique_ptr<Region>> regionOwners_;
    std::unordered_map<BasicBlock*, Region*> entryToRegion_;
    std::unordered_map<BasicBlock*, Region*> exitToRegion_;
    const PostDominanceInfo* postDomInfo_;

   public:
    explicit RegionInfo(Function* F, const PostDominanceInfo* PDI);
    ~RegionInfo() = default;

    /// Get the function this region info is for
    Function* getFunction() const { return function_; }

    /// Get the top-level region (entire function)
    Region* getTopRegion() const { return topRegion_.get(); }

    /// Get the region that contains the given basic block
    Region* getRegionFor(BasicBlock* BB) const;

    /// Get the region that starts with the given entry block
    Region* getRegionByEntry(BasicBlock* entry) const;

    /// Get the region that ends with the given exit block
    Region* getRegionByExit(BasicBlock* exit) const;

    /// Complex operation to move region src to replace region dst
    Region* moveRegion(Region* src, Region* dst);

    /// Verify the region information is consistent
    bool verify() const;

    /// Print all region information
    void print() const;

   private:
    /// Build region hierarchy using BFS+DFS hybrid algorithm
    void buildRegion(Region* region, std::unordered_set<BasicBlock*>&);

    /// Create a new basic block for region transformations
    BasicBlock* createNewBasicBlock(const std::string& name);

    /// Update all mappings after region modifications
    void updateMappings();
};

/// Analysis pass that computes region information
class RegionAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<RegionInfo>;

    static const std::string& getName() {
        static const std::string name = "RegionAnalysis";
        return name;
    }
    std::unique_ptr<AnalysisResult> runOnFunction(Function& f,
                                                  AnalysisManager& AM) override;

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"PostDominanceAnalysis"};
    }

    static Result run(Function& F, const PostDominanceInfo& PDI) {
        return std::make_unique<RegionInfo>(&F, &PDI);
    }
};

}  // namespace midend