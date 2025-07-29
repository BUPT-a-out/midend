#include "Pass/Analysis/LoopInfo.h"

#include <algorithm>
#include <iostream>
#include <stack>

#include "Pass/Analysis/DominanceInfo.h"

namespace midend {

Loop::Loop(BasicBlock* header)
    : header_(header),
      parentLoop_(nullptr),
      preheader_(nullptr),
      loopDepth_(0) {}

void Loop::addSubLoop(std::unique_ptr<Loop> subLoop) {
    subLoop->setParentLoop(this);
    subLoop->setLoopDepth(loopDepth_ + 1);
    subLoops_.push_back(std::move(subLoop));
}

bool Loop::contains(const Loop* L) const {
    if (L == this) return true;

    for (const auto& subLoop : subLoops_) {
        if (subLoop->contains(L)) {
            return true;
        }
    }
    return false;
}

void Loop::computeExitInfo() {
    computeExitingBlocks();
    computeExitBlocks();
}

void Loop::computeExitingBlocks() {
    exitingBlocks_.clear();

    for (BasicBlock* bb : blocks_) {
        for (BasicBlock* succ : bb->getSuccessors()) {
            if (blocks_.count(succ) == 0) {
                // This block has a successor outside the loop
                exitingBlocks_.push_back(bb);
                break;
            }
        }
    }
}

void Loop::computeExitBlocks() {
    exitBlocks_.clear();
    std::unordered_set<BasicBlock*> exitBlockSet;

    for (BasicBlock* exitingBB : exitingBlocks_) {
        for (BasicBlock* succ : exitingBB->getSuccessors()) {
            if (blocks_.count(succ) == 0 && exitBlockSet.count(succ) == 0) {
                exitBlocks_.push_back(succ);
                exitBlockSet.insert(succ);
            }
        }
    }
}

void Loop::computePreheader() {
    preheader_ = nullptr;

    auto preds = header_->getPredecessors();
    BasicBlock* candidate = nullptr;
    int outsidePreds = 0;

    for (BasicBlock* pred : preds) {
        if (blocks_.count(pred) == 0) {
            // Predecessor is outside the loop
            candidate = pred;
            outsidePreds++;
        }
    }

    // A preheader exists only if there's exactly one predecessor from outside
    if (outsidePreds == 1) {
        preheader_ = candidate;
    }
}

bool Loop::isSimplified() const {
    // A simplified loop has:
    // 1. A preheader (single entry from outside)
    // 2. A single backedge (single latch)
    return preheader_ != nullptr && hasSingleBackedge();
}

std::vector<BasicBlock*> Loop::getLoopLatches() const {
    std::vector<BasicBlock*> latches;

    for (BasicBlock* pred : header_->getPredecessors()) {
        if (blocks_.count(pred) > 0) {
            // This predecessor is inside the loop, so it's a latch
            latches.push_back(pred);
        }
    }

    return latches;
}

void Loop::print() const {
    std::cout << "Loop Header: " << header_->getName() << std::endl;
    std::cout << "  Depth: " << loopDepth_ << std::endl;
    std::cout << "  Blocks: ";
    for (BasicBlock* bb : blocks_) {
        std::cout << bb->getName() << " ";
    }
    std::cout << std::endl;

    if (preheader_) {
        std::cout << "  Preheader: " << preheader_->getName() << std::endl;
    }

    if (!exitingBlocks_.empty()) {
        std::cout << "  Exiting blocks: ";
        for (BasicBlock* bb : exitingBlocks_) {
            std::cout << bb->getName() << " ";
        }
        std::cout << std::endl;
    }

    if (!exitBlocks_.empty()) {
        std::cout << "  Exit blocks: ";
        for (BasicBlock* bb : exitBlocks_) {
            std::cout << bb->getName() << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "  Simplified: " << (isSimplified() ? "Yes" : "No")
              << std::endl;

    if (!subLoops_.empty()) {
        std::cout << "  Sub-loops:" << std::endl;
        for (const auto& subLoop : subLoops_) {
            subLoop->print();
        }
    }
}

LoopInfo::LoopInfo(Function* F, const DominanceInfo* DI)
    : function_(F), domInfo_(DI) {
    analyze();
}

Loop* LoopInfo::getLoopFor(BasicBlock* BB) const {
    auto it = blockToLoop_.find(BB);
    return it != blockToLoop_.end() ? it->second : nullptr;
}

unsigned LoopInfo::getLoopDepth(BasicBlock* BB) const {
    Loop* L = getLoopFor(BB);
    return L ? L->getLoopDepth() : 0;
}

bool LoopInfo::isLoopHeader(BasicBlock* BB) const {
    Loop* L = getLoopFor(BB);
    return L && L->getHeader() == BB;
}

void LoopInfo::addTopLevelLoop(std::unique_ptr<Loop> L) {
    // Update block-to-loop mapping for all blocks in the loop
    std::function<void(Loop*)> updateMapping = [&](Loop* loop) {
        for (BasicBlock* bb : loop->getBlocks()) {
            blockToLoop_[bb] = loop;
        }
        for (const auto& subLoop : loop->getSubLoops()) {
            updateMapping(subLoop.get());
        }
    };

    updateMapping(L.get());
    topLevelLoops_.push_back(std::move(L));
}

void LoopInfo::changeLoopFor(BasicBlock* BB, Loop* L) {
    if (L) {
        blockToLoop_[BB] = L;
    } else {
        blockToLoop_.erase(BB);
    }
}

void LoopInfo::removeLoop(Loop* L) {
    // Remove from block-to-loop mapping
    for (BasicBlock* bb : L->getBlocks()) {
        blockToLoop_.erase(bb);
    }

    // Remove from top-level loops
    topLevelLoops_.erase(
        std::remove_if(
            topLevelLoops_.begin(), topLevelLoops_.end(),
            [L](const std::unique_ptr<Loop>& ptr) { return ptr.get() == L; }),
        topLevelLoops_.end());
}

void LoopInfo::analyze() {
    discoverLoops();
    buildLoopHierarchy();
    computeLoopProperties();
}

void LoopInfo::discoverLoops() {
    auto backEdges = findBackEdges();

    // Group backedges by header
    std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> headerToLatches;
    for (const auto& [latch, header] : backEdges) {
        headerToLatches[header].push_back(latch);
    }

    for (const auto& [header, latches] : headerToLatches) {
        auto loop = std::make_unique<Loop>(header);

        // Add all blocks reachable from latches to header
        std::unordered_set<BasicBlock*> visited;
        std::stack<BasicBlock*> worklist;

        // Start with the header and all latches
        loop->addBlock(header);
        visited.insert(header);

        for (BasicBlock* latch : latches) {
            if (visited.count(latch) == 0) {
                worklist.push(latch);
                visited.insert(latch);
                loop->addBlock(latch);
            }
        }

        // Find all blocks that can reach the header through latches
        while (!worklist.empty()) {
            BasicBlock* current = worklist.top();
            worklist.pop();

            for (BasicBlock* pred : current->getPredecessors()) {
                if (visited.count(pred) == 0 &&
                    domInfo_->dominates(header, pred)) {
                    visited.insert(pred);
                    loop->addBlock(pred);
                    worklist.push(pred);
                }
            }
        }

        addTopLevelLoop(std::move(loop));
    }
}

std::vector<std::pair<BasicBlock*, BasicBlock*>> LoopInfo::findBackEdges() {
    std::vector<std::pair<BasicBlock*, BasicBlock*>> backEdges;

    std::unordered_set<BasicBlock*> visited;
    std::unordered_set<BasicBlock*> inDFS;

    std::function<void(BasicBlock*)> dfs = [&](BasicBlock* bb) {
        visited.insert(bb);
        inDFS.insert(bb);

        for (BasicBlock* succ : bb->getSuccessors()) {
            if (inDFS.count(succ)) {
                backEdges.emplace_back(bb, succ);
            } else if (visited.count(succ) == 0) {
                dfs(succ);
            }
        }

        inDFS.erase(bb);
    };

    if (!function_->empty()) {
        dfs(&function_->getEntryBlock());
    }

    return backEdges;
}

void LoopInfo::buildLoopHierarchy() {
    std::sort(
        topLevelLoops_.begin(), topLevelLoops_.end(),
        [](const std::unique_ptr<Loop>& a, const std::unique_ptr<Loop>& b) {
            return a->getBlocks().size() < b->getBlocks().size();
        });

    // Build parent-child relationships
    for (size_t i = 0; i < topLevelLoops_.size(); ++i) {
        for (size_t j = i + 1; j < topLevelLoops_.size(); ++j) {
            Loop* smaller = topLevelLoops_[i].get();
            Loop* larger = topLevelLoops_[j].get();

            // Check if smaller loop is contained in larger loop
            if (larger->getBlocks().count(smaller->getHeader()) > 0) {
                // Find the most immediate parent
                bool isImmediate = true;
                for (size_t k = i + 1; k < j; ++k) {
                    Loop* middle = topLevelLoops_[k].get();
                    if (larger->getBlocks().count(middle->getHeader()) > 0 &&
                        middle->getBlocks().count(smaller->getHeader()) > 0) {
                        isImmediate = false;
                        break;
                    }
                }

                if (isImmediate) {
                    // Move smaller loop as a sub-loop of larger loop
                    auto smallerPtr = std::move(topLevelLoops_[i]);
                    topLevelLoops_.erase(topLevelLoops_.begin() + i);

                    if (j > i) j--;

                    larger->addSubLoop(std::move(smallerPtr));
                    i--;
                    break;
                }
            }
        }
    }
}

void LoopInfo::computeLoopProperties() {
    std::function<void(Loop*, unsigned)> computeForLoop = [&](Loop* loop,
                                                              unsigned depth) {
        loop->setLoopDepth(depth);
        loop->computeExitInfo();
        loop->computePreheader();

        // Update block-to-loop mapping for innermost loops
        for (BasicBlock* bb : loop->getBlocks()) {
            if (blockToLoop_[bb] == nullptr ||
                blockToLoop_[bb]->getLoopDepth() < loop->getLoopDepth()) {
                blockToLoop_[bb] = loop;
            }
        }

        for (const auto& subLoop : loop->getSubLoops()) {
            computeForLoop(subLoop.get(), depth + 1);
        }
    };

    for (const auto& loop : topLevelLoops_) {
        computeForLoop(loop.get(), 1);
    }
}

bool LoopInfo::verify() const {
    std::function<bool(Loop*)> verifyLoop = [&](Loop* loop) -> bool {
        if (!loop->getHeader()) return false;

        for (BasicBlock* bb : loop->getBlocks()) {
            if (!domInfo_->dominates(loop->getHeader(), bb)) {
                return false;
            }
        }

        for (const auto& subLoop : loop->getSubLoops()) {
            if (!verifyLoop(subLoop.get())) {
                return false;
            }
        }

        return true;
    };

    for (const auto& loop : topLevelLoops_) {
        if (!verifyLoop(loop.get())) {
            return false;
        }
    }

    return true;
}

void LoopInfo::print() const {
    std::cout << "Loop Information for function: " << function_->getName()
              << std::endl;

    if (topLevelLoops_.empty()) {
        std::cout << "  No loops found." << std::endl;
        return;
    }

    for (const auto& loop : topLevelLoops_) {
        loop->print();
        std::cout << std::endl;
    }
}

std::unique_ptr<AnalysisResult> LoopAnalysis::runOnFunction(Function& f) {
    auto domInfo = std::make_unique<DominanceInfo>(&f);
    return std::make_unique<LoopInfo>(&f, domInfo.get());
}

std::unique_ptr<AnalysisResult> LoopAnalysis::runOnFunction(
    Function& f, AnalysisManager& AM) {
    auto* domInfo = AM.getAnalysis<DominanceInfo>("DominanceAnalysis", f);
    if (!domInfo) {
        std::cerr << "Error: Could not get DominanceAnalysis" << std::endl;
        return nullptr;
    }

    return std::make_unique<LoopInfo>(&f, domInfo);
}

}  // namespace midend