#include "Pass/Analysis/RegionInfo.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <stack>

#include "IR/Instruction.h"
#include "IR/Instructions.h"

namespace midend {

//===----------------------------------------------------------------------===//
// Region Implementation
//===----------------------------------------------------------------------===//

Region::Region(BasicBlock* entry, BasicBlock* exit, Region* parent)
    : entry_(entry),
      exit_(exit),
      parent_(parent),
      allBlocksComputed_(false),
      ownBlocksComputed_(false),
      depth_(parent ? parent->depth_ + 1 : 0) {}

void Region::addSubRegion(std::unique_ptr<Region> subRegion) {
    subRegion->setParent(this);
    subRegion->setDepth(depth_ + 1);
    subRegions_.push_back(std::move(subRegion));
    invalidateBlockCache();
}

bool Region::contains(BasicBlock* BB) const {
    return getAllBlocks().count(BB) > 0;
}

bool Region::contains(const Region* R) const {
    if (R == this) return true;

    for (const auto& subRegion : subRegions_) {
        if (subRegion->contains(R)) {
            return true;
        }
    }
    return false;
}

const Region::BlockSet& Region::getAllBlocks() const {
    if (!allBlocksComputed_) {
        computeAllBlocks();
    }
    return allBlocks_;
}

const Region::BlockSet& Region::getOwnBlocks() const {
    if (!ownBlocksComputed_) {
        computeOwnBlocks();
    }
    return ownBlocks_;
}

Region::BlockVector Region::getAllBlocksVector() const {
    const auto& blockSet = getAllBlocks();
    return BlockVector(blockSet.begin(), blockSet.end());
}

Region::BlockVector Region::getOwnBlocksVector() const {
    const auto& blockSet = getOwnBlocks();
    return BlockVector(blockSet.begin(), blockSet.end());
}

bool Region::isSimple() const {
    // A region with null exit (like top-level region) is not simple
    if (!entry_ || !exit_) {
        return false;
    }

    // A simple region has exactly one incoming edge to entry and one outgoing
    // edge from exit
    auto entryPreds = entry_->getPredecessors();

    // Count external predecessors to entry
    int externalEntryPreds = 0;
    for (BasicBlock* pred : entryPreds) {
        if (!contains(pred)) {
            externalEntryPreds++;
        }
    }

    // Count external successors from blocks that exit to exit_
    int externalExitSuccs = 0;
    for (BasicBlock* BB : getAllBlocks()) {
        for (BasicBlock* succ : BB->getSuccessors()) {
            if (succ == exit_) {
                externalExitSuccs++;
            }
        }
    }

    return externalEntryPreds == 1 && externalExitSuccs == 1;
}

void Region::invalidateBlockCache() {
    allBlocksComputed_ = false;
    ownBlocksComputed_ = false;
    allBlocks_.clear();
    ownBlocks_.clear();

    // Recursively invalidate parent caches
    if (parent_) {
        parent_->invalidateBlockCache();
    }
}

void Region::computeAllBlocks() const {
    allBlocks_.clear();

    // DFS to find all blocks between entry and exit
    std::stack<BasicBlock*> stack;
    std::unordered_set<BasicBlock*> visited;

    stack.push(entry_);

    while (!stack.empty()) {
        BasicBlock* current = stack.top();
        stack.pop();

        if (visited.count(current) || current == exit_) {
            continue;
        }

        visited.insert(current);
        allBlocks_.insert(current);

        // Add successors
        for (BasicBlock* succ : current->getSuccessors()) {
            if (!visited.count(succ)) {
                stack.push(succ);
            }
        }
    }

    allBlocksComputed_ = true;
}

void Region::computeOwnBlocks() const {
    // First compute all blocks
    if (!allBlocksComputed_) {
        computeAllBlocks();
    }

    ownBlocks_ = allBlocks_;

    // Remove blocks that belong to sub-regions
    for (const auto& subRegion : subRegions_) {
        const auto& subBlocks = subRegion->getAllBlocks();
        for (BasicBlock* BB : subBlocks) {
            ownBlocks_.erase(BB);
        }
    }

    ownBlocksComputed_ = true;
}

void Region::print() const {
    std::cout << "Region [" << (entry_ ? entry_->getName() : "null") << " -> "
              << (exit_ ? exit_->getName() : "null") << "] depth=" << depth_
              << "\n";
    std::cout << "  Blocks: ";
    for (BasicBlock* BB : getAllBlocks()) {
        std::cout << BB->getName() << " ";
    }
    std::cout << "\n";

    for (const auto& subRegion : subRegions_) {
        subRegion->print();
    }
}

//===----------------------------------------------------------------------===//
// RegionDepthFirstIterator Implementation
//===----------------------------------------------------------------------===//

RegionDepthFirstIterator::RegionDepthFirstIterator(Region* root)
    : current_(nullptr) {
    if (root) {
        stack_.push_back(root);
        advance();  // Set current_ to the first region
    }
}

RegionDepthFirstIterator& RegionDepthFirstIterator::operator++() {
    advance();
    return *this;
}

RegionDepthFirstIterator RegionDepthFirstIterator::operator++(int) {
    RegionDepthFirstIterator tmp = *this;
    advance();
    return tmp;
}

bool RegionDepthFirstIterator::operator==(
    const RegionDepthFirstIterator& other) const {
    return current_ == other.current_;
}

bool RegionDepthFirstIterator::operator!=(
    const RegionDepthFirstIterator& other) const {
    return !(*this == other);
}

void RegionDepthFirstIterator::advance() {
    if (stack_.empty()) {
        current_ = nullptr;
        return;
    }

    // Pop the current region from stack
    current_ = stack_.back();
    stack_.pop_back();

    // Add children to stack in reverse order for left-to-right traversal
    const auto& subRegions = current_->getSubRegions();
    for (auto it = subRegions.rbegin(); it != subRegions.rend(); ++it) {
        stack_.push_back(it->get());
    }
}

//===----------------------------------------------------------------------===//
// RegionBlockIterator Implementation
//===----------------------------------------------------------------------===//

RegionBlockIterator::RegionBlockIterator(const Region* region, bool end)
    : region_(region), index_(0), computed_(false) {
    if (end) {
        computed_ = true;
        index_ = SIZE_MAX;
    }
}

BasicBlock* RegionBlockIterator::operator*() const {
    if (!computed_) {
        const_cast<RegionBlockIterator*>(this)->computeBlocks();
    }
    if (index_ >= blocks_.size()) {
        return nullptr;
    }
    return blocks_[index_];
}

BasicBlock* RegionBlockIterator::operator->() const { return operator*(); }

RegionBlockIterator& RegionBlockIterator::operator++() {
    if (!computed_) {
        computeBlocks();
    }
    ++index_;
    return *this;
}

RegionBlockIterator RegionBlockIterator::operator++(int) {
    RegionBlockIterator tmp = *this;
    ++(*this);
    return tmp;
}

bool RegionBlockIterator::operator==(const RegionBlockIterator& other) const {
    if (region_ != other.region_) return false;

    // If both are not computed and have same index, they're equal
    if (!computed_ && !other.computed_) {
        return index_ == other.index_;
    }

    // Ensure both are computed for proper comparison
    if (!computed_) {
        const_cast<RegionBlockIterator*>(this)->computeBlocks();
    }
    if (!other.computed_) {
        const_cast<RegionBlockIterator*>(&other)->computeBlocks();
    }

    // Now both are computed, handle end iterator cases
    bool thisIsEnd = (index_ == SIZE_MAX) || (index_ >= blocks_.size());
    bool otherIsEnd =
        (other.index_ == SIZE_MAX) || (other.index_ >= other.blocks_.size());

    if (thisIsEnd && otherIsEnd) {
        return true;
    }

    return index_ == other.index_;
}

bool RegionBlockIterator::operator!=(const RegionBlockIterator& other) const {
    return !(*this == other);
}

void RegionBlockIterator::computeBlocks() {
    if (computed_ || !region_) return;

    blocks_ = region_->getAllBlocksVector();
    computed_ = true;
}

//===----------------------------------------------------------------------===//
// RegionMover Implementation
//===----------------------------------------------------------------------===//

RegionMover::RegionMover(RegionInfo* RI, DominanceInfo* DI,
                         PostDominanceInfo* PDI)
    : regionInfo_(RI), domInfo_(DI), postDomInfo_(PDI) {}

bool RegionMover::moveRegion(Region* R, BasicBlock* newPred,
                             BasicBlock* newSucc) {
    if (!R || !newPred || !newSucc) return false;

    // Compute region summary
    RegionSummary summary;
    computeRegionSummary(R, summary);

    // Disconnect region from current location
    disconnectRegion(R, summary);

    // Connect region to new location
    connectRegion(R, newPred, newSucc, summary);

    return true;
}

void RegionMover::computeRegionSummary(Region* R, RegionSummary& summary) {
    const auto& regionBlocks = R->getAllBlocks();

    // Find live-in and live-out values
    for (BasicBlock* BB : regionBlocks) {
        for (auto& inst : *BB) {
            // Check operands for live-in values
            for (size_t i = 0; i < inst->getNumOperands(); ++i) {
                Value* operand = inst->getOperand(i);
                if (auto* operandInst = dynamic_cast<Instruction*>(operand)) {
                    BasicBlock* defBB = operandInst->getParent();
                    if (regionBlocks.count(defBB) == 0) {
                        // Used in region, defined outside - this is live-in
                        summary.liveInSet.insert(operand);
                    }
                }
            }

            // Check if this instruction is used outside the region
            for (Value::use_iterator UI = inst->use_begin(),
                                     UE = inst->use_end();
                 UI != UE; ++UI) {
                Use* use = *UI;
                if (auto* userInst =
                        dynamic_cast<Instruction*>(use->getUser())) {
                    BasicBlock* useBB = userInst->getParent();
                    if (regionBlocks.count(useBB) == 0) {
                        // Defined in region, used outside - this is live-out
                        summary.liveOutSet.insert(inst);
                    }
                }
            }
        }
    }
}

void RegionMover::disconnectRegion(Region* R, const RegionSummary& summary) {
    BasicBlock* entry = R->getEntry();
    BasicBlock* exit = R->getExit();

    // Find predecessors of entry that are outside the region
    auto entryPreds = entry->getPredecessors();
    for (BasicBlock* pred : entryPreds) {
        if (!R->contains(pred)) {
            // Modify pred's terminator to jump directly to exit
            auto* terminator = pred->getTerminator();
            if (auto* branch = dynamic_cast<BranchInst*>(terminator)) {
                if (branch->isConditional()) {
                    if (branch->getTrueBB() == entry) {
                        branch->setOperand(1, exit);
                    }
                    if (branch->getFalseBB() == entry) {
                        branch->setOperand(2, exit);
                    }
                } else {
                    branch->setOperand(0, exit);
                }
            }
        }
    }

    // Update PHI nodes in exit block
    updateSourcePHINodes(exit, R, summary);
}

void RegionMover::connectRegion(Region* R, BasicBlock* newPred,
                                BasicBlock* newSucc,
                                const RegionSummary& summary) {
    BasicBlock* entry = R->getEntry();
    BasicBlock* exit = R->getExit();

    // Update newPred's terminator to jump to entry
    auto* terminator = newPred->getTerminator();
    if (auto* branch = dynamic_cast<BranchInst*>(terminator)) {
        if (branch->isConditional()) {
            // This is more complex - would need to know which branch to take
            // For simplicity, assume unconditional for now
            branch->setOperand(0, entry);
        } else {
            branch->setOperand(0, entry);
        }
    }

    // Update region's exit blocks to jump to newSucc
    for (BasicBlock* BB : R->getAllBlocks()) {
        auto* terminator = BB->getTerminator();
        if (auto* branch = dynamic_cast<BranchInst*>(terminator)) {
            if (branch->isConditional()) {
                if (branch->getTrueBB() == exit) {
                    branch->setOperand(1, newSucc);
                }
                if (branch->getFalseBB() == exit) {
                    branch->setOperand(2, newSucc);
                }
            } else if (branch->getTargetBB() == exit) {
                branch->setOperand(0, newSucc);
            }
        }
    }

    // Update PHI nodes in newSucc
    updateTargetPHINodes(newSucc, R, summary);

    // Satisfy live-in dependencies
    satisfyLiveInDependencies(R, newPred, summary);
}

void RegionMover::updateSourcePHINodes(BasicBlock* sourceSucc, Region* R,
                                       const RegionSummary& /* summary */) {
    // Remove PHI entries that came from the moved region
    for (auto& inst : *sourceSucc) {
        if (auto* phi = dynamic_cast<PHINode*>(inst)) {
            // Remove entries from blocks in the region
            for (BasicBlock* BB : R->getAllBlocks()) {
                phi->deleteIncoming(BB);
            }
        } else {
            break;  // PHI nodes are at the beginning
        }
    }
}

void RegionMover::updateTargetPHINodes(BasicBlock* targetSucc, Region* R,
                                       const RegionSummary& summary) {
    // Add PHI entries for values coming from the moved region
    for (auto& inst : *targetSucc) {
        if (auto* phi = dynamic_cast<PHINode*>(inst)) {
            // Add entries from region's exit blocks
            for (BasicBlock* BB : R->getAllBlocks()) {
                for (BasicBlock* succ : BB->getSuccessors()) {
                    if (succ == targetSucc) {
                        // Find appropriate value from live-out set
                        for (Value* liveOut : summary.liveOutSet) {
                            if (auto* liveOutInst =
                                    dynamic_cast<Instruction*>(liveOut)) {
                                if (liveOutInst->getParent() == BB) {
                                    phi->addIncoming(liveOut, BB);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            break;
        }
    }
}

void RegionMover::satisfyLiveInDependencies(Region* R, BasicBlock* newPred,
                                            const RegionSummary& summary) {
    // For each live-in value, ensure it's available at the new location
    for (Value* liveIn : summary.liveInSet) {
        if (auto* liveInInst = dynamic_cast<Instruction*>(liveIn)) {
            // Check if the value is still dominated by its definition
            BasicBlock* defBB = liveInInst->getParent();
            if (!domInfo_->dominates(defBB, newPred)) {
                // May need to insert PHI nodes - this is complex and would
                // require running SSA construction algorithm For now, just
                // ensure the value is reachable
                createPHINodesForLiveIn(R, summary);
            }
        }
    }
}

void RegionMover::createPHINodesForLiveIn(Region* /* R */,
                                          const RegionSummary& /* summary */) {
    // This would implement PHI node insertion for live-in values
    // Complex algorithm - simplified implementation for now
    // In a full implementation, this would use the iterated dominance frontier
    // algorithm to insert PHI nodes at appropriate locations
}

//===----------------------------------------------------------------------===//
// RegionInfo Implementation
//===----------------------------------------------------------------------===//

RegionInfo::RegionInfo(Function* F, const DominanceInfo* DI,
                       const PostDominanceInfo* PDI)
    : function_(F), domInfo_(DI), postDomInfo_(PDI) {
    if (F && !F->empty()) {
        // Create top-level region representing entire function
        BasicBlock* entry = &F->front();
        BasicBlock* exit = nullptr;  // Top-level region has no explicit exit

        topLevelRegion_ = std::make_unique<Region>(entry, exit);

        // Build the region hierarchy
        buildRegions();

        // Create region mover
        mover_ =
            std::make_unique<RegionMover>(this, const_cast<DominanceInfo*>(DI),
                                          const_cast<PostDominanceInfo*>(PDI));
    }
}

Region* RegionInfo::getRegionFor(BasicBlock* BB) const {
    auto it = blockToRegion_.find(BB);
    if (it != blockToRegion_.end()) {
        return it->second;
    }
    return topLevelRegion_.get();  // Default to top-level region
}

Region* RegionInfo::getCommonRegion(BasicBlock* A, BasicBlock* B) const {
    Region* regionA = getRegionFor(A);
    Region* regionB = getRegionFor(B);

    if (!regionA || !regionB) return topLevelRegion_.get();

    // Find common ancestor
    std::unordered_set<Region*> ancestorsA;
    Region* current = regionA;
    while (current) {
        ancestorsA.insert(current);
        current = current->getParent();
    }

    current = regionB;
    while (current) {
        if (ancestorsA.count(current)) {
            return current;  // First common ancestor
        }
        current = current->getParent();
    }

    return topLevelRegion_.get();
}

bool RegionInfo::isValidRegion(BasicBlock* entry, BasicBlock* exit) const {
    return validateRegionPair(entry, exit);
}

Region* RegionInfo::getOrCreateRegion(BasicBlock* entry, BasicBlock* exit) {
    // First check if the region already exists in the cache
    auto key = std::make_pair(entry, exit);
    auto it = regionCache_.find(key);
    if (it != regionCache_.end()) {
        return it->second.get();
    }

    // Search existing hierarchy for the region
    for (const auto* region : regions()) {
        if (region->getEntry() == entry && region->getExit() == exit) {
            return const_cast<Region*>(region);
        }
    }

    // If not found and is valid, create a new region
    if (isValidRegion(entry, exit)) {
        auto region = std::make_unique<Region>(entry, exit);
        Region* regionPtr = region.get();
        regionCache_[key] = std::move(region);
        return regionPtr;
    }

    return nullptr;
}

void RegionInfo::buildRegions() {
    discoverRegions();
    buildRegionHierarchy();
    updateBlockMapping();
}

bool RegionInfo::verify() const {
    // Verify region hierarchy is consistent
    for (const auto& region : regions()) {
        // Check that entry dominates all blocks in region
        for (BasicBlock* BB : region->getAllBlocks()) {
            if (!domInfo_->dominates(region->getEntry(), BB)) {
                return false;
            }
        }

        // Check that exit post-dominates all blocks in region (if exit exists)
        if (region->getExit()) {
            for (BasicBlock* BB : region->getAllBlocks()) {
                if (!postDomInfo_->dominates(region->getExit(), BB)) {
                    return false;
                }
            }
        }
    }

    return true;
}

void RegionInfo::print() const {
    std::cout << "=== Region Information for " << function_->getName()
              << " ===\n";
    if (topLevelRegion_) {
        topLevelRegion_->print();
    }
    std::cout << "=== End Region Information ===\n";
}

void RegionInfo::discoverRegions() {
    if (!domInfo_ || !postDomInfo_) return;

    // Get post-order traversal of dominator tree
    auto postOrder = domInfo_->computeReversePostOrder();
    std::reverse(postOrder.begin(), postOrder.end());

    // For each block as potential entry
    for (BasicBlock* entry : postOrder) {
        // Find candidate exit blocks by walking up post-dominator tree
        std::vector<BasicBlock*> candidateExits = findCandidateExits(entry);

        for (BasicBlock* exit : candidateExits) {
            if (validateRegionPair(entry, exit)) {
                // Create region
                getOrCreateRegion(entry, exit);
            }
        }
    }
}

// Helper function extracted from validateRegionPair for better readability
void RegionInfo::dfsCollectBlocks(BasicBlock* start, BasicBlock* stop,
                                  std::unordered_set<BasicBlock*>& result) {
    std::stack<BasicBlock*> stack;
    std::unordered_set<BasicBlock*> visited;

    stack.push(start);

    while (!stack.empty()) {
        BasicBlock* current = stack.top();
        stack.pop();

        if (visited.count(current) || current == stop) {
            continue;
        }

        visited.insert(current);
        result.insert(current);

        // Add successors
        for (BasicBlock* succ : current->getSuccessors()) {
            if (!visited.count(succ)) {
                stack.push(succ);
            }
        }
    }
}

// Extracted validation helper methods
bool RegionInfo::validateDominanceProperties(BasicBlock* entry,
                                             BasicBlock* exit) const {
    return domInfo_->dominates(entry, exit) &&
           postDomInfo_->dominates(exit, entry);
}

bool RegionInfo::validateNoBypassPaths(
    BasicBlock* entry, BasicBlock* exit,
    const std::unordered_set<BasicBlock*>& regionBlocks) const {
    // Check no bypass entries: all external predecessors of region blocks
    // (except entry) must go through entry
    for (BasicBlock* BB : regionBlocks) {
        if (BB == entry) continue;

        const auto& preds = BB->getPredecessors();
        if (std::any_of(preds.begin(), preds.end(),
                        [&regionBlocks](BasicBlock* pred) {
                            return regionBlocks.count(pred) == 0;
                        })) {
            return false;  // Found bypass entry
        }
    }

    // Check no bypass exits: all successors of region blocks
    // that go outside must go to exit
    for (BasicBlock* BB : regionBlocks) {
        const auto& succs = BB->getSuccessors();
        if (std::any_of(succs.begin(), succs.end(),
                        [&regionBlocks, exit](BasicBlock* succ) {
                            return regionBlocks.count(succ) == 0 &&
                                   succ != exit;
                        })) {
            return false;  // Found bypass exit
        }
    }

    return true;
}

bool RegionInfo::validateRegionPair(BasicBlock* entry, BasicBlock* exit) const {
    if (!entry || !exit || entry == exit) return false;

    // 1. Check dominance properties
    if (!validateDominanceProperties(entry, exit)) {
        return false;
    }

    // 2. Find all blocks between entry and exit using DFS
    std::unordered_set<BasicBlock*> regionBlocks;
    dfsCollectBlocks(entry, exit, regionBlocks);

    // 3. Verify there's at least one path from entry to exit through region
    // blocks
    if (regionBlocks.empty()) {
        return false;
    }

    // 4. Check no bypass paths
    return validateNoBypassPaths(entry, exit, regionBlocks);
}

// Optimized O(n log n) version of region hierarchy building
void RegionInfo::buildRegionHierarchyOptimized() {
    // Convert regionCache_ to a vector and sort by size (smaller to larger)
    std::vector<std::pair<Region*, std::unique_ptr<Region>>> regionPairs;
    for (auto& pair : regionCache_) {
        regionPairs.emplace_back(pair.second.get(), std::move(pair.second));
    }
    regionCache_.clear();

    // Sort by region size for optimized hierarchy building
    std::sort(regionPairs.begin(), regionPairs.end(),
              [](const auto& a, const auto& b) {
                  return a.first->getAllBlocks().size() <
                         b.first->getAllBlocks().size();
              });

    // Build parent-child relationships in O(n²) but more efficiently
    for (size_t i = 0; i < regionPairs.size(); ++i) {
        Region* currentRegion = regionPairs[i].first;
        Region* parent = nullptr;
        size_t parentSize = SIZE_MAX;

        // Only check larger regions (already sorted)
        for (size_t j = i + 1; j < regionPairs.size(); ++j) {
            Region* candidate = regionPairs[j].first;

            if (candidate->getAllBlocks().size() < parentSize) {
                const auto& candidateBlocks = candidate->getAllBlocks();
                const auto& currentBlocks = currentRegion->getAllBlocks();

                // Check if all current blocks are in candidate using
                // std::all_of
                if (std::all_of(currentBlocks.begin(), currentBlocks.end(),
                                [&candidateBlocks](BasicBlock* BB) {
                                    return candidateBlocks.count(BB) > 0;
                                })) {
                    parent = candidate;
                    parentSize = candidate->getAllBlocks().size();
                }
            }
        }

        // Set parent relationship
        Region* finalParent = parent ? parent : topLevelRegion_.get();
        currentRegion->setParent(finalParent);
        currentRegion->setDepth(finalParent->getDepth() + 1);
    }

    // Move regions to their correct parents and fix depth calculation
    for (auto& regionPair : regionPairs) {
        Region* region = regionPair.first;
        Region* parent = region->getParent();

        // Move the region to its parent
        if (parent == topLevelRegion_.get()) {
            topLevelRegion_->addSubRegion(std::move(regionPair.second));
        } else {
            parent->addSubRegion(std::move(regionPair.second));
        }
    }

    // Fix depth calculation after all regions are properly nested
    fixDepthCalculation(topLevelRegion_.get(), 0);
}

void RegionInfo::fixDepthCalculation(Region* root, unsigned currentDepth) {
    if (!root) return;

    root->setDepth(currentDepth);
    for (const auto& subRegion : root->getSubRegions()) {
        fixDepthCalculation(subRegion.get(), currentDepth + 1);
    }
}

void RegionInfo::buildRegionHierarchy() { buildRegionHierarchyOptimized(); }

void RegionInfo::updateBlockMapping() {
    blockToRegion_.clear();

    // Map each block to its smallest containing region
    for (const auto& region : regions()) {
        for (BasicBlock* BB : region->getAllBlocks()) {
            // Only update if we haven't seen this block or found a smaller
            // region
            if (blockToRegion_.find(BB) == blockToRegion_.end() ||
                region->getAllBlocks().size() <
                    blockToRegion_[BB]->getAllBlocks().size()) {
                blockToRegion_[BB] = const_cast<Region*>(region);
            }
        }
    }
}

std::vector<BasicBlock*> RegionInfo::findCandidateExits(
    BasicBlock* entry) const {
    std::vector<BasicBlock*> candidates;

    // Walk up post-dominator tree from entry to find blocks that post-dominate
    // entry
    BasicBlock* current = postDomInfo_->getImmediateDominator(entry);
    while (current) {
        candidates.push_back(current);
        current = postDomInfo_->getImmediateDominator(current);
    }

    return candidates;
}

//===----------------------------------------------------------------------===//
// RegionAnalysis Implementation
//===----------------------------------------------------------------------===//

std::unique_ptr<AnalysisResult> RegionAnalysis::runOnFunction(Function& f) {
    // This version doesn't have access to AnalysisManager, so we can't get
    // dependencies Return empty result - the AM version should be used instead
    return std::make_unique<RegionInfo>(&f, nullptr, nullptr);
}

std::unique_ptr<AnalysisResult> RegionAnalysis::runOnFunction(
    Function& f, AnalysisManager& AM) {
    // Get required dependencies
    auto* domInfo = AM.getAnalysis<DominanceInfo>("DominanceAnalysis", f);
    auto* postDomInfo =
        AM.getAnalysis<PostDominanceInfo>("PostDominanceAnalysis", f);

    return std::make_unique<RegionInfo>(&f, domInfo, postDomInfo);
}

//===----------------------------------------------------------------------===//
// RegionInfo convenience methods implementation
//===----------------------------------------------------------------------===//

size_t RegionInfo::size() const {
    return countRegionsRecursive(topLevelRegion_.get()) -
           1;  // Exclude top-level region
}

unsigned RegionInfo::depth() const {
    return findMaxDepthRecursive(topLevelRegion_.get());
}

std::vector<Region*> RegionInfo::getRegionsAtDepth(unsigned depth) const {
    std::vector<Region*> result;
    if (topLevelRegion_) {
        collectRegionsAtDepth(topLevelRegion_.get(), depth, 0, result);
    }
    return result;
}

std::vector<Region*> RegionInfo::getLeafRegions() const {
    std::vector<Region*> result;
    if (topLevelRegion_) {
        collectLeafRegions(topLevelRegion_.get(), result);
    }
    return result;
}

size_t RegionInfo::countRegionsRecursive(const Region* root) const {
    if (!root) return 0;

    size_t count = 1;
    for (const auto& subRegion : root->getSubRegions()) {
        count += countRegionsRecursive(subRegion.get());
    }
    return count;
}

unsigned RegionInfo::findMaxDepthRecursive(const Region* root) const {
    if (!root) return 0;

    unsigned maxDepth = root->getDepth();
    for (const auto& subRegion : root->getSubRegions()) {
        maxDepth = std::max(maxDepth, findMaxDepthRecursive(subRegion.get()));
    }
    return maxDepth;
}

void RegionInfo::collectRegionsAtDepth(const Region* root, unsigned targetDepth,
                                       unsigned currentDepth,
                                       std::vector<Region*>& result) const {
    if (!root) return;

    if (currentDepth == targetDepth) {
        result.push_back(const_cast<Region*>(root));
    }

    for (const auto& subRegion : root->getSubRegions()) {
        collectRegionsAtDepth(subRegion.get(), targetDepth, currentDepth + 1,
                              result);
    }
}

void RegionInfo::collectLeafRegions(const Region* root,
                                    std::vector<Region*>& result) const {
    if (!root) return;

    if (root->getSubRegions().empty()) {
        result.push_back(const_cast<Region*>(root));
    } else {
        for (const auto& subRegion : root->getSubRegions()) {
            collectLeafRegions(subRegion.get(), result);
        }
    }
}

//===----------------------------------------------------------------------===//
// Region convenience methods implementation
//===----------------------------------------------------------------------===//

std::optional<Region*> Region::findSubRegionFor(BasicBlock* BB) const {
    for (const auto& subRegion : subRegions_) {
        if (subRegion->contains(BB)) {
            return subRegion.get();
        }
    }
    return std::nullopt;
}

}  // namespace midend