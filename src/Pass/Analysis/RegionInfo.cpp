#include "Pass/Analysis/RegionInfo.h"

#include <algorithm>
#include <iostream>
#include <stack>

#include "IR/Instructions/TerminatorOps.h"
#include "Pass/Pass.h"
#include "Support/Casting.h"

namespace midend {

unsigned newBlockId = 0;

RegionBlockIterator::RegionBlockIterator(BasicBlock* entry, BasicBlock* exit)
    : entry_(entry), exit_(exit), current_(nullptr), atEnd_(false) {
    if (entry_ && entry_ != exit_) {
        queue_.push(entry_);
        visited_.insert(entry_);
        advance();
    } else {
        atEnd_ = true;
    }
}

RegionBlockIterator::RegionBlockIterator()
    : entry_(nullptr), exit_(nullptr), current_(nullptr), atEnd_(true) {}

void RegionBlockIterator::advance() {
    if (queue_.empty()) {
        atEnd_ = true;
        current_ = nullptr;
        return;
    }

    current_ = queue_.front();
    queue_.pop();

    for (BasicBlock* succ : current_->getSuccessors()) {
        if (succ != exit_ && visited_.find(succ) == visited_.end()) {
            visited_.insert(succ);
            queue_.push(succ);
        }
    }
}

RegionBlockIterator& RegionBlockIterator::operator++() {
    advance();
    return *this;
}

bool RegionBlockIterator::operator==(const RegionBlockIterator& other) const {
    return atEnd_ == other.atEnd_ && current_ == other.current_;
}

bool RegionBlockIterator::operator!=(const RegionBlockIterator& other) const {
    return !(*this == other);
}

Region::Region(BasicBlock* entry, BasicBlock* exit)
    : entry_(entry), exit_(exit), parent_(nullptr), level_(1) {}

void Region::addSubRegion(Region* subRegion) {
    if (subRegion) {
        subRegions_.push_back(subRegion);
        subRegion->setParent(this);
    }
}

void Region::removeSubRegion(Region* subRegion) {
    replaceSubRegion(subRegion, {});
}

void Region::replaceSubRegion(Region* oldRegion,
                              const std::vector<Region*>& newRegions) {
    auto it = std::find(subRegions_.begin(), subRegions_.end(), oldRegion);
    if (it != subRegions_.end()) {
        (*it)->setParent(nullptr);
        it = subRegions_.erase(it);
        for (Region* newRegion : newRegions) {
            if (newRegion) {
                it = subRegions_.insert(it, newRegion);
                newRegion->setParent(this);
                ++it;
            }
        }
    }
}

void Region::setParent(Region* parent) {
    parent_ = parent;
    level_ = parent_ ? parent_->level_ + 1 : 1;
}

void Region::print() const {
    std::cout << std::string(level_ * 2, ' ')
              << "Region: entry=" << (entry_ ? entry_->getName() : "null")
              << ", exit=" << (exit_ ? exit_->getName() : "null")
              << ", level=" << level_ << "\n";

    for (Region* subRegion : subRegions_) {
        subRegion->print();
    }
}

RegionInfo::RegionInfo(Function* F, const PostDominanceInfo* PDI)
    : function_(F), postDomInfo_(PDI) {
    if (!function_ || function_->empty() || function_->isDeclaration()) {
        return;
    }

    BasicBlock* entryBB = &function_->getEntryBlock();
    topRegion_ = std::make_unique<Region>(entryBB, nullptr);

    entryToRegion_[entryBB] = topRegion_.get();

    std::unordered_set<BasicBlock*> visited;

    buildRegion(topRegion_.get(), visited);
}

void RegionInfo::buildRegion(Region* region,
                             std::unordered_set<BasicBlock*>& visited) {
    if (!region || !region->getEntry()) {
        return;
    }
    if (!visited.insert(region->getEntry()).second) {
        return;
    }
    std::queue<BasicBlock*> queue;
    for (BasicBlock* succ : region->getEntry()->getSuccessors())
        if (succ != region->getExit()) queue.push(succ);

    while (!queue.empty()) {
        BasicBlock* entry = queue.front();
        queue.pop();

        BasicBlock* exit = postDomInfo_->getImmediateDominator(entry);

        if (visited.find(entry) != visited.end() ||
            visited.find(exit) != visited.end())
            continue;

        auto newRegion = std::make_unique<Region>(entry, exit);
        Region* newRegionPtr = newRegion.get();
        regionOwners_[newRegionPtr] = std::move(newRegion);
        region->addSubRegion(newRegionPtr);

        entryToRegion_[entry] = newRegionPtr;
        if (exit) {
            exitToRegion_[exit] = newRegionPtr;
        }

        buildRegion(newRegionPtr, visited);
        if (exit && exit != region->getExit() &&
            visited.find(exit) == visited.end()) {
            queue.push(exit);
        }
    }
}

Region* RegionInfo::getRegionFor(BasicBlock* BB) const {
    // Find the most nested region containing this block
    Region* result = nullptr;
    unsigned maxLevel = 0;

    auto entryIt = entryToRegion_.find(BB);
    if (entryIt != entryToRegion_.end() &&
        entryIt->second->getLevel() > maxLevel) {
        result = entryIt->second;
        maxLevel = result->getLevel();
    }

    // Check if this block is contained in any region
    for (const auto& pair : entryToRegion_) {
        Region* region = pair.second;
        if (region->getLevel() > maxLevel) {
            RegionBlockIterator it = region->blocks();
            RegionBlockIterator end = region->blocks_end();
            for (; it != end; ++it) {
                if (*it == BB) {
                    result = region;
                    maxLevel = region->getLevel();
                    break;
                }
            }
        }
    }

    return result ? result : topRegion_.get();
}

Region* RegionInfo::getRegionByEntry(BasicBlock* entry) const {
    auto it = entryToRegion_.find(entry);
    return it != entryToRegion_.end() ? it->second : nullptr;
}

Region* RegionInfo::getRegionByExit(BasicBlock* exit) const {
    auto it = exitToRegion_.find(exit);
    return it != exitToRegion_.end() ? it->second : nullptr;
}

BasicBlock* RegionInfo::createNewBasicBlock(const std::string& name) {
    if (!function_) {
        return nullptr;
    }

    Context* ctx = function_->getContext();
    return BasicBlock::Create(ctx, name, function_);
}

Region* RegionInfo::moveRegion(Region* src, Region* dst) {
    throw std::runtime_error("moveRegion is not implemented yet");
    if (!src || !dst || !src->getEntry() || !dst->getEntry()) {
        return nullptr;
    }
    if (src == dst) return dst;

    BasicBlock* srcEntry = src->getEntry();
    BasicBlock* srcExit = src->getExit();
    BasicBlock* dstEntry = dst->getEntry();
    BasicBlock* dstExit = dst->getExit();

    // Step 1: Modify destination entry and source entry
    // Replace dst entry's terminator with src entry's terminator
    if (auto* srcTerminator = srcEntry->getTerminator()) {
        // Remove old dst terminator
        if (auto* dstTerminator =
                dyn_cast<BranchInst>(dstEntry->getTerminator())) {
            dstTerminator->eraseFromParent();
        }

        // Clone src terminator to dst entry
        auto* newTerminator = srcTerminator->clone();
        dstEntry->push_back(newTerminator);

        src->setEntry(dstEntry);
        srcEntry->replaceUsesWith<PHINode>(dstEntry);

        srcTerminator->eraseFromParent();
        srcEntry->push_back(BranchInst::Create(srcExit));
    }

    // Step 2: Create newExit block
    std::string newExitName = srcExit ? srcExit->getName() + "_new" : "newExit";
    BasicBlock* newExit =
        createNewBasicBlock(newExitName + "." + std::to_string(++newBlockId));

    if (srcExit && newExit) {
        // Copy phi nodes from srcExit to newExit
        std::vector<PHINode*> phisToCopy;
        for (Instruction* inst : *srcExit) {
            if (auto* phi = dyn_cast<PHINode>(inst)) {
                phisToCopy.push_back(phi);
            } else {
                break;
            }
        }

        // Clone and add phi nodes to newExit
        for (PHINode* originalPhi : phisToCopy) {
            auto* newPhi = static_cast<PHINode*>(originalPhi->clone());
            newExit->push_back(newPhi);

            // Update uses of original phi to use new phi
            originalPhi->replaceAllUsesWith(newPhi);
            originalPhi->eraseFromParent();
        }

        // Add unconditional branch to dstExit
        if (dstExit) {
            auto* br = BranchInst::Create(dstExit);
            newExit->push_back(br);
        }

        dstEntry->replaceUsesWith<PHINode>(newExit);
        srcExit->replaceUsesWith<BranchInst>(newExit);
    }

    // Step 3: Create new regions and update mappings

    // Create newRegion2 at dst's location
    auto newRegion2 = std::make_unique<Region>(newExit, dstExit);
    Region* newRegion2Ptr = newRegion2.get();
    regionOwners_[newRegion2Ptr] = std::move(newRegion2);

    // TODO: clean dst region

    // Update dst's parent
    if (dst->getParent()) {
        dst->getParent()->replaceSubRegion(dst, {src, newRegion2Ptr});
    }

    if (dstEntry) entryToRegion_[dstEntry] = src;
    if (newExit) entryToRegion_[newExit] = newRegion2Ptr;
    if (newExit) exitToRegion_[newExit] = src;
    if (dstExit) exitToRegion_[dstExit] = newRegion2Ptr;

    // Create newRegion at src's original location
    auto newRegion = std::make_unique<Region>(srcEntry, srcExit);
    Region* newRegionPtr = newRegion.get();
    regionOwners_[newRegionPtr] = std::move(newRegion);

    // Update src's parent to point to newRegion instead of src
    if (src->getParent()) {
        src->getParent()->replaceSubRegion(src, {newRegionPtr});
    }
    if (srcEntry) entryToRegion_[srcEntry] = newRegionPtr;
    if (srcExit && exitToRegion_[srcExit] == src)
        exitToRegion_[srcExit] = newRegionPtr;

    updateMappings();

    return newRegion2Ptr;
}

void RegionInfo::updateMappings() {
    entryToRegion_.clear();
    exitToRegion_.clear();

    // Rebuild mappings from current region hierarchy
    std::function<void(Region*)> rebuildMappings = [&](Region* region) {
        if (region && region->getEntry()) {
            entryToRegion_[region->getEntry()] = region;
            if (region->getExit()) {
                exitToRegion_[region->getExit()] = region;
            }

            for (Region* subRegion : region->getSubRegions()) {
                rebuildMappings(subRegion);
            }
        }
    };

    rebuildMappings(topRegion_.get());
}

bool RegionInfo::verify() const {
    if (!topRegion_) {
        return false;
    }

    std::function<bool(Region*)> verifyRegion = [&](Region* region) -> bool {
        if (!region || !region->getEntry()) {
            return false;
        }

        for (Region* subRegion : region->getSubRegions()) {
            if (subRegion->getParent() != region) {
                return false;
            }
            if (!verifyRegion(subRegion)) {
                return false;
            }
        }

        return true;
    };

    return verifyRegion(topRegion_.get());
}

void RegionInfo::print() const {
    std::cout << "RegionInfo for function: " << function_->getName() << "\n";
    if (topRegion_) {
        topRegion_->print();
    }
}

std::unique_ptr<AnalysisResult> RegionAnalysis::runOnFunction(
    Function& f, AnalysisManager& AM) {
    auto* postDomInfo =
        AM.getAnalysis<PostDominanceInfo>("PostDominanceAnalysis", f);
    if (!postDomInfo) {
        return nullptr;
    }

    return std::make_unique<RegionInfo>(&f, postDomInfo);
}

}  // namespace midend