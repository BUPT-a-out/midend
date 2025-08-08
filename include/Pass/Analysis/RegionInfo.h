#pragma once

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Value.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"

namespace midend {

class Region;
class RegionInfo;

// Custom hash function for pair of pointers
struct PairHash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

/// A Single Entry Single Exit (SESE) region in the control flow graph
class Region {
   public:
    using BlockSet = std::unordered_set<BasicBlock*>;
    using BlockVector = std::vector<BasicBlock*>;
    using RegionVector = std::vector<std::unique_ptr<Region>>;
    using const_iterator = RegionVector::const_iterator;

   private:
    BasicBlock* entry_;  // Entry block (dominates all blocks in region)
    BasicBlock*
        exit_;  // Exit block (post-dominates all blocks, not part of region)
    Region* parent_;           // Parent region in the hierarchy
    RegionVector subRegions_;  // Child regions
    mutable BlockSet
        allBlocks_;  // All blocks in this region (including sub-regions)
    mutable BlockSet
        ownBlocks_;  // Blocks in this region (excluding sub-regions)
    mutable bool allBlocksComputed_ =
        false;  // Lazy computation flag for all blocks
    mutable bool ownBlocksComputed_ =
        false;            // Lazy computation flag for own blocks
    unsigned depth_ = 0;  // Nesting depth (0 for top-level regions)

    /// Common DFS traversal utility for computing blocks
    void dfsTraverseBlocks(BasicBlock* start, BasicBlock* stop,
                           std::unordered_set<BasicBlock*>& result) const;

   public:
    Region(BasicBlock* entry, BasicBlock* exit, Region* parent = nullptr);
    ~Region() = default;

    // Disable copy operations since Region contains unique_ptr
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    // Enable move operations
    Region(Region&&) = default;
    Region& operator=(Region&&) = default;

    /// Get the entry block of this region
    BasicBlock* getEntry() const { return entry_; }

    /// Get the exit block of this region (not part of the region)
    BasicBlock* getExit() const { return exit_; }

    /// Get the parent region (null for top-level regions)
    Region* getParent() const { return parent_; }

    /// Set the parent region
    void setParent(Region* parent) { parent_ = parent; }

    /// Remove duplicate getSubRegions() - already defined below

    /// Add a sub-region
    void addSubRegion(std::unique_ptr<Region> subRegion);

    /// Get nesting depth
    unsigned getDepth() const { return depth_; }

    /// Set nesting depth
    void setDepth(unsigned depth) { depth_ = depth; }

    /// Check if this region contains a basic block
    bool contains(BasicBlock* BB) const;

    /// Check if this region contains another region
    bool contains(const Region* R) const;

    /// Get all blocks in this region (including sub-regions) - lazy computation
    const BlockSet& getAllBlocks() const;

    /// Get blocks in this region (excluding sub-regions) - lazy computation
    const BlockSet& getOwnBlocks() const;

    /// Get blocks as vector for iteration
    BlockVector getAllBlocksVector() const;
    BlockVector getOwnBlocksVector() const;

    /// Container-like interface
    bool empty() const { return getOwnBlocks().empty(); }
    size_t size() const { return getOwnBlocks().size(); }
    size_t totalSize() const { return getAllBlocks().size(); }

    /// Sub-region access
    bool hasSubRegions() const { return !subRegions_.empty(); }
    size_t getSubRegionCount() const { return subRegions_.size(); }

    /// Find sub-region containing a basic block
    std::optional<Region*> findSubRegionFor(BasicBlock* BB) const;

    /// Check if this is a simple region (single entry edge, single exit edge)
    bool isSimple() const;

    /// Check if this is the top-level region
    bool isTopLevel() const { return parent_ == nullptr; }

    /// Invalidate cached block computations
    void invalidateBlockCache();

    /// Print region information for debugging
    void print() const;

    // Iterator support for sub-regions
    const_iterator begin() const { return subRegions_.begin(); }
    const_iterator end() const { return subRegions_.end(); }
    const_iterator cbegin() const { return subRegions_.cbegin(); }
    const_iterator cend() const { return subRegions_.cend(); }

    /// Range-based access to sub-regions
    const RegionVector& getSubRegions() const { return subRegions_; }

    /// Access sub-region by index
    const std::unique_ptr<Region>& operator[](size_t index) const {
        return subRegions_[index];
    }

   private:
    /// Compute all blocks in the region (including sub-regions)
    void computeAllBlocks() const;

    /// Compute blocks in this region only (excluding sub-regions)
    void computeOwnBlocks() const;
};

/// Iterator for traversing regions in depth-first order
class RegionDepthFirstIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Region*;
    using difference_type = std::ptrdiff_t;
    using pointer = Region**;
    using reference = Region*&;

   private:
    std::vector<Region*> stack_;
    Region* current_;

   public:
    explicit RegionDepthFirstIterator(Region* root = nullptr);

    Region* operator*() const { return current_; }
    Region* operator->() const { return current_; }

    RegionDepthFirstIterator& operator++();
    RegionDepthFirstIterator operator++(int);

    bool operator==(const RegionDepthFirstIterator& other) const;
    bool operator!=(const RegionDepthFirstIterator& other) const;

   private:
    void advance();
};

/// Iterator for traversing basic blocks in a region (lazy evaluation)
class RegionBlockIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = BasicBlock*;
    using difference_type = std::ptrdiff_t;
    using pointer = BasicBlock**;
    using reference = BasicBlock*&;

   private:
    const Region* region_;
    std::vector<BasicBlock*> blocks_;
    size_t index_;
    bool computed_;

   public:
    explicit RegionBlockIterator(const Region* region, bool end = false);

    BasicBlock* operator*() const;
    BasicBlock* operator->() const;

    RegionBlockIterator& operator++();
    RegionBlockIterator operator++(int);

    bool operator==(const RegionBlockIterator& other) const;
    bool operator!=(const RegionBlockIterator& other) const;

   private:
    void computeBlocks();
};

/// Range wrapper for C++17 range-based for loops
template <typename IteratorType>
class IteratorRange {
   private:
    IteratorType begin_it_;
    IteratorType end_it_;

   public:
    IteratorRange(IteratorType begin, IteratorType end)
        : begin_it_(begin), end_it_(end) {}

    IteratorType begin() const { return begin_it_; }
    IteratorType end() const { return end_it_; }
};

/// Helper class for moving SESE regions while maintaining SSA form
class RegionMover {
   public:
    /// Summary of region's data flow interface
    struct RegionSummary {
        std::set<Value*>
            liveInSet;  // Values used in region but defined outside
        std::set<Value*>
            liveOutSet;  // Values defined in region and used outside
        std::unordered_map<BasicBlock*, std::vector<Value*>>
            phiInputs;  // PHI inputs by block
        std::unordered_map<BasicBlock*, std::vector<Value*>>
            phiOutputs;  // PHI outputs by block
    };

   private:
    RegionInfo* regionInfo_;
    DominanceInfo* domInfo_;
    PostDominanceInfo* postDomInfo_;

   public:
    RegionMover(RegionInfo* RI, DominanceInfo* DI, PostDominanceInfo* PDI);

    /// Move a region from its current location to between newPred and newSucc
    bool moveRegion(Region* R, BasicBlock* newPred, BasicBlock* newSucc);

   private:
    /// Compute data flow summary for a region
    void computeRegionSummary(Region* R, RegionSummary& summary);

    /// Disconnect region from its current location
    void disconnectRegion(Region* R, const RegionSummary& summary);

    /// Connect region to new location
    void connectRegion(Region* R, BasicBlock* newPred, BasicBlock* newSucc,
                       const RegionSummary& summary);

    /// Update PHI nodes at the source location
    void updateSourcePHINodes(BasicBlock* sourceSucc, Region* R,
                              const RegionSummary& summary);

    /// Update PHI nodes at the target location
    void updateTargetPHINodes(BasicBlock* targetSucc, Region* R,
                              const RegionSummary& summary);

    /// Satisfy live-in dependencies at new location
    void satisfyLiveInDependencies(Region* R, BasicBlock* newPred,
                                   const RegionSummary& summary);

    /// Create PHI nodes if needed for live-in values
    void createPHINodesForLiveIn(Region* R, const RegionSummary& summary);
};

/// Analysis result that contains region information for a function
class RegionInfo : public AnalysisResult {
   public:
    using RegionVector = std::vector<std::unique_ptr<Region>>;
    using const_iterator = RegionDepthFirstIterator;

   private:
    Function* function_;
    std::unique_ptr<Region>
        topLevelRegion_;  // Root region representing entire function
    std::unordered_map<BasicBlock*, Region*>
        blockToRegion_;  // Maps blocks to their smallest containing region
    std::unordered_map<std::pair<BasicBlock*, BasicBlock*>,
                       std::unique_ptr<Region>,
                       PairHash>
        regionCache_;  // Cache for (entry,exit) -> region
    const DominanceInfo* domInfo_;
    const PostDominanceInfo* postDomInfo_;
    std::unique_ptr<RegionMover> mover_;

    /// Utility methods extracted from complex functions
    bool validateDominanceProperties(BasicBlock* entry, BasicBlock* exit) const;
    bool validateNoBypassPaths(
        BasicBlock* entry, BasicBlock* exit,
        const std::unordered_set<BasicBlock*>& regionBlocks) const;
    void buildRegionHierarchyOptimized();

    /// Common DFS utility for region discovery
    static void dfsCollectBlocks(BasicBlock* start, BasicBlock* stop,
                                 std::unordered_set<BasicBlock*>& result);

   public:
    RegionInfo(Function* F, const DominanceInfo* DI,
               const PostDominanceInfo* PDI);
    ~RegionInfo() = default;

    /// Get the function this region info is for
    Function* getFunction() const { return function_; }

    /// Get the top-level region (represents entire function)
    Region* getTopLevelRegion() const { return topLevelRegion_.get(); }

    /// Get the smallest region that contains the given basic block
    Region* getRegionFor(BasicBlock* BB) const;

    /// Alternative name for getRegionFor (more intuitive)
    Region* regionOf(BasicBlock* BB) const { return getRegionFor(BB); }

    /// Get the common ancestor region of two basic blocks
    Region* getCommonRegion(BasicBlock* A, BasicBlock* B) const;

    /// Alternative name for getCommonRegion
    Region* commonAncestor(BasicBlock* A, BasicBlock* B) const {
        return getCommonRegion(A, B);
    }

    /// Check if (entry, exit) forms a valid SESE region
    bool isValidRegion(BasicBlock* entry, BasicBlock* exit) const;

    /// Create or find a region with given entry and exit
    Region* getOrCreateRegion(BasicBlock* entry, BasicBlock* exit);

    /// Build the complete region hierarchy
    void buildRegions();

    /// Get region mover for moving regions while maintaining SSA
    RegionMover* getRegionMover() const { return mover_.get(); }

    /// Container-like interface
    bool empty() const { return !topLevelRegion_ || topLevelRegion_->empty(); }
    size_t size() const;     // Count of all regions
    unsigned depth() const;  // Maximum nesting depth

    /// Verify the region information is consistent
    bool verify() const;

    /// Print all region information
    void print() const;

    /// Find all regions at a specific depth level
    std::vector<Region*> getRegionsAtDepth(unsigned depth) const;

    /// Get all leaf regions (regions with no sub-regions)
    std::vector<Region*> getLeafRegions() const;

    // Iterator support for depth-first traversal of all regions
    const_iterator begin() const {
        return RegionDepthFirstIterator(topLevelRegion_.get());
    }
    const_iterator end() const { return RegionDepthFirstIterator(); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    /// Get range for iterating over all regions depth-first
    IteratorRange<RegionDepthFirstIterator> regions() const {
        return IteratorRange<RegionDepthFirstIterator>(begin(), end());
    }

    /// Alternative range access (more modern naming)
    IteratorRange<RegionDepthFirstIterator> allRegions() const {
        return regions();
    }

    /// Get range for iterating blocks in a region
    IteratorRange<RegionBlockIterator> blocks(const Region* R) const {
        return IteratorRange<RegionBlockIterator>(RegionBlockIterator(R),
                                                  RegionBlockIterator(R, true));
    }

    /// Get all blocks in a region as vector (convenience method)
    std::vector<BasicBlock*> getBlocksOf(const Region* R) const {
        return R ? R->getAllBlocksVector() : std::vector<BasicBlock*>{};
    }

   private:
    /// Discover regions using post-order traversal of dominator tree
    void discoverRegions();

    /// Check if (entry, exit) pair passes all SESE region validation tests
    bool validateRegionPair(BasicBlock* entry, BasicBlock* exit) const;

    /// Build region hierarchy from discovered regions
    void buildRegionHierarchy();

    /// Update block-to-region mapping
    void updateBlockMapping();

    /// Find all candidate exit blocks for a given entry block
    std::vector<BasicBlock*> findCandidateExits(BasicBlock* entry) const;

    /// Helper to count all regions recursively
    size_t countRegionsRecursive(const Region* root) const;

    /// Helper to find maximum depth recursively
    unsigned findMaxDepthRecursive(const Region* root) const;

    /// Collect regions at specific depth level
    void collectRegionsAtDepth(const Region* root, unsigned targetDepth,
                               unsigned currentDepth,
                               std::vector<Region*>& result) const;

    /// Collect leaf regions recursively
    void collectLeafRegions(const Region* root,
                            std::vector<Region*>& result) const;

    /// Fix depth calculation after region hierarchy is built
    void fixDepthCalculation(Region* root, unsigned currentDepth);
};

/// Analysis pass that computes region information
class RegionAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<RegionInfo>;

    static const std::string& getName() {
        static const std::string name = "RegionAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override;

    /// Version that accepts an AnalysisManager to resolve dependencies
    std::unique_ptr<AnalysisResult> runOnFunction(Function& f,
                                                  AnalysisManager& AM) override;

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"DominanceAnalysis", "PostDominanceAnalysis"};
    }

    static Result run(Function& F, const DominanceInfo& DI,
                      const PostDominanceInfo& PDI) {
        return std::make_unique<RegionInfo>(&F, &DI, &PDI);
    }
};

}  // namespace midend