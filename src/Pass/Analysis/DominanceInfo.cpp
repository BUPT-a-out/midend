#include "Pass/Analysis/DominanceInfo.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <stack>
#include <unordered_set>

#include "IR/IRBuilder.h"
#include "IR/Instructions/TerminatorOps.h"

namespace midend {

template <bool IsPostDom>
DominanceInfoBase<IsPostDom>::DominanceInfoBase(Function* F) : function_(F) {
    if (!F || F->empty()) return;

    computeDominators();
    computeImmediateDominators();
    computeDominanceFrontier();
    buildDominatorTree();
}

template <bool IsPostDom>
DominanceInfoBase<IsPostDom>::~DominanceInfoBase() = default;

template <bool IsPostDom>
bool DominanceInfoBase<IsPostDom>::createdVirtualExit() const {
    if constexpr (IsPostDom) {
        return useVirtualBlock_;
    }
    return false;
}

template <bool IsPostDom>
std::vector<BasicBlock*> DominanceInfoBase<IsPostDom>::getPreds(
    BasicBlock* BB) const {
    if constexpr (IsPostDom) {
        if (isVirtualExit(BB)) {
            return {};
        }
        if (exitBlocksSet_.find(BB) != exitBlocksSet_.end()) {
            return {getVirtualExit()};
        }
        return BB->getSuccessors();
    } else {
        return BB->getPredecessors();
    }
}

template <bool IsPostDom>
std::vector<BasicBlock*> DominanceInfoBase<IsPostDom>::getSuccs(
    BasicBlock* BB) const {
    if constexpr (IsPostDom) {
        if (isVirtualExit(BB)) {
            return getVirtualExitPreds();
        }
        return BB->getPredecessors();
    } else {
        return BB->getSuccessors();
    }
}

template <bool IsPostDom>
BasicBlock* DominanceInfoBase<IsPostDom>::getEntry() const {
    if constexpr (IsPostDom) {
        return getVirtualExit();
    } else {
        return &function_->front();
    }
}

template <bool IsPostDom>
BasicBlock* DominanceInfoBase<IsPostDom>::getVirtualExit() const {
    if constexpr (IsPostDom) {
        if (!virtualExit_) {
            // Find all exit blocks (blocks with no successors)
            exitBlocks_.clear();
            useVirtualBlock_ = false;
            exitBlocksSet_.clear();
            for (auto& BB : *function_) {
                if (BB->getSuccessors().empty()) {
                    exitBlocks_.push_back(BB);
                }
            }

            // Only create virtual exit if there are multiple exit blocks
            if (exitBlocks_.size() == 1) {
                virtualExit_ = exitBlocks_[0];
            } else {
                virtualExit_ = BasicBlock::Create(function_->getContext(),
                                                  "_virtual_exit", function_);
                virtualExit_->isVirtual = true;
                IRBuilder builder(virtualExit_);
                builder.createRetVoid();
                useVirtualBlock_ = true;
                if (exitBlocks_.size() > 1) {
                    exitBlocksSet_.insert(exitBlocks_.begin(),
                                          exitBlocks_.end());
                } else {
                    // For infinite loops
                    for (auto& BB : *function_) {
                        exitBlocks_.push_back(BB);
                        exitBlocksSet_.insert(BB);
                    }
                }
            }
            assert(virtualExit_);
        }
        return virtualExit_;
    } else {
        return nullptr;
    }
}

template <bool IsPostDom>
std::vector<BasicBlock*> DominanceInfoBase<IsPostDom>::getVirtualExitPreds()
    const {
    if constexpr (IsPostDom) {
        auto virtualExit = getVirtualExit();
        if (useVirtualBlock_) {
            return exitBlocks_;
        }
        return virtualExit->getPredecessors();
    }
    return {};
}

template <bool IsPostDom>
bool DominanceInfoBase<IsPostDom>::isVirtualExit(BasicBlock* BB) const {
    if constexpr (IsPostDom) {
        return BB == getVirtualExit();
    } else {
        return false;
    }
}

// https://oi-wiki.org/graph/dominator-tree/#%E6%95%B0%E6%8D%AE%E6%B5%81%E8%BF%AD%E4%BB%A3%E6%B3%95
template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::computeDominators() {
    if (function_->empty()) return;

    auto* entry = getEntry();

    BBVector rpoBlocks = computeReversePostOrder();

    for (auto* BB : rpoBlocks) {
        dominators_[BB] = BBSet();
    }

    // Initialize: entry dominates only itself, others dominate all
    dominators_[entry].insert(entry);
    for (auto* BB : rpoBlocks) {
        if (BB != entry) {
            dominators_[BB] = BBSet(rpoBlocks.begin(), rpoBlocks.end());
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto* BB : rpoBlocks) {
            if (BB == entry) continue;

            BBSet newDominators;

            // Intersection of dominators of all predecessors
            auto predecessors = getPreds(BB);
            if (!predecessors.empty()) {
                bool first = true;
                for (auto* Pred : predecessors) {
                    if (first) {
                        newDominators = dominators_[Pred];
                        first = false;
                    } else {
                        BBSet intersection;
                        std::set_intersection(
                            newDominators.begin(), newDominators.end(),
                            dominators_[Pred].begin(), dominators_[Pred].end(),
                            std::inserter(intersection, intersection.begin()));
                        newDominators = std::move(intersection);
                    }
                }
                newDominators.insert(BB);  // Block always dominates itself
            } else {
                // If no predecessors and not entry, this block is unreachable
                // In a well-formed CFG, only entry should have no predecessors
                newDominators.insert(BB);
            }

            if (newDominators != dominators_[BB]) {
                dominators_[BB] = std::move(newDominators);
                changed = true;
            }
        }
    }
}

// https://en.wikipedia.org/wiki/Dominator_(graph_theory)
template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::computeImmediateDominators() {
    if (function_->empty()) return;

    auto* entry = getEntry();
    immediateDominators_[entry] = nullptr;  // Entry has no immediate dominator

    for (auto& BB : *function_) {
        if (BB == entry) continue;

        BasicBlock* idom = nullptr;
        const auto& dominatorsOfBB = dominators_[BB];

        for (auto* dominator : dominatorsOfBB) {
            if (dominator == BB) continue;

            // Check if this dominator is dominated by all other dominators
            bool isImmediate = true;
            for (auto* otherDom : dominatorsOfBB) {
                if (otherDom == BB || otherDom == dominator) continue;

                if (dominators_[dominator].find(otherDom) ==
                    dominators_[dominator].end()) {
                    isImmediate = false;
                    break;
                }
            }

            if (isImmediate) {
                idom = dominator;
                break;
            }
        }

        immediateDominators_[BB] = idom;
    }
}

template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::computeDominanceFrontier() {
    if (function_->empty()) return;

    // Initialize empty frontiers
    for (auto& BB : *function_) {
        dominanceFrontier_[BB] = BBSet();
    }

    // For each basic block
    for (auto& X : *function_) {
        const auto& preds = getPreds(X);
        if (preds.size() >= 2) {  // Join nodes
            for (auto* pred : preds) {
                auto* runner = pred;

                // Walk up the dominator tree from pred
                while (runner && runner != getImmediateDominator(X)) {
                    dominanceFrontier_[runner].insert(X);
                    runner = getImmediateDominator(runner);
                }
            }
        }
    }
}

template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::buildDominatorTree() {
    domTree_ = std::make_unique<DominatorTreeBase<IsPostDom>>(*this);
}

template <bool IsPostDom>
bool DominanceInfoBase<IsPostDom>::dominates(BasicBlock* A,
                                             BasicBlock* B) const {
    if (!A || !B) return false;
    auto it = dominators_.find(B);
    if (it == dominators_.end()) return false;
    return it->second.find(A) != it->second.end();
}

template <bool IsPostDom>
bool DominanceInfoBase<IsPostDom>::strictlyDominates(BasicBlock* A,
                                                     BasicBlock* B) const {
    return A != B && dominates(A, B);
}

template <bool IsPostDom>
BasicBlock* DominanceInfoBase<IsPostDom>::getImmediateDominator(
    BasicBlock* BB) const {
    auto it = immediateDominators_.find(BB);
    return it != immediateDominators_.end() ? it->second : nullptr;
}

template <bool IsPostDom>
const typename DominanceInfoBase<IsPostDom>::BBSet&
DominanceInfoBase<IsPostDom>::getDominators(BasicBlock* BB) const {
    static BBSet empty;
    auto it = dominators_.find(BB);
    return it != dominators_.end() ? it->second : empty;
}

template <bool IsPostDom>
const typename DominanceInfoBase<IsPostDom>::BBSet&
DominanceInfoBase<IsPostDom>::getDominanceFrontier(BasicBlock* BB) const {
    static BBSet empty;
    auto it = dominanceFrontier_.find(BB);
    return it != dominanceFrontier_.end() ? it->second : empty;
}

template <bool IsPostDom>
const typename DominanceInfoBase<IsPostDom>::BBSet&
DominanceInfoBase<IsPostDom>::getDominated(BasicBlock* BB) const {
    auto it = dominatedCache_.find(BB);
    if (it != dominatedCache_.end()) {
        return it->second;
    }

    BBSet dominated;
    for (auto& other : *function_) {
        if (dominates(BB, other)) {
            dominated.insert(other);
        }
    }

    dominatedCache_[BB] = dominated;
    return dominatedCache_[BB];
}

template <bool IsPostDom>
const DominatorTreeBase<IsPostDom>*
DominanceInfoBase<IsPostDom>::getDominatorTree() const {
    return domTree_.get();
}

template <bool IsPostDom>
bool DominanceInfoBase<IsPostDom>::verify() const {
    if (function_->empty()) return true;

    auto* entry = getEntry();

    if (!dominates(entry, entry)) return false;
    if (getImmediateDominator(entry) != nullptr) return false;

    for (auto& BB : *function_) {
        if (!dominates(entry, BB)) return false;

        if (!dominates(BB, BB)) return false;

        auto* idom = getImmediateDominator(BB);
        if (BB != entry) {
            if (!idom) return false;
            if (!strictlyDominates(idom, BB)) return false;
        }
    }

    return true;
}

template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::print() const {
    std::cout << "Dominance Information for function: " << function_->getName()
              << "\n";

    for (auto& BB : *function_) {
        std::cout << "BB " << BB->getName() << ":\n";

        std::cout << "  Dominators: ";
        for (auto* dom : getDominators(BB)) {
            std::cout << dom->getName() << " ";
        }
        std::cout << "\n";

        auto* idom = getImmediateDominator(BB);
        std::cout << "  Immediate Dominator: "
                  << (idom ? idom->getName() : "none") << "\n";

        std::cout << "  Dominance Frontier: ";
        for (auto* df : getDominanceFrontier(BB)) {
            std::cout << df->getName() << " ";
        }
        std::cout << "\n\n";
    }

    if (domTree_) {
        std::cout << "Dominator Tree:\n";
        domTree_->print();
    }
}

template <bool IsPostDom>
typename DominanceInfoBase<IsPostDom>::BBVector
DominanceInfoBase<IsPostDom>::computeReversePostOrder() const {
    if (function_->empty()) return {};

    BBVector postOrder;
    std::unordered_set<BasicBlock*> visited;

    std::stack<std::pair<BasicBlock*, bool>> stack;
    auto* entry = getEntry();
    stack.push({entry, false});

    while (!stack.empty()) {
        auto current = stack.top();
        stack.pop();
        auto* bb = current.first;
        bool processed = current.second;

        if (processed) {
            postOrder.push_back(bb);
        } else {
            if (visited.count(bb)) continue;
            visited.insert(bb);

            stack.push({bb, true});

            auto successors = getSuccs(bb);
            for (auto it = successors.rbegin(); it != successors.rend(); ++it) {
                if (!visited.count(*it)) {
                    stack.push({*it, false});
                }
            }
        }
    }

    std::reverse(postOrder.begin(), postOrder.end());
    return postOrder;
}

template <bool IsPostDom>
DominatorTreeBase<IsPostDom>::DominatorTreeBase(
    const DominanceInfoBase<IsPostDom>& domInfo)
    : domInfo_(&domInfo) {
    if (domInfo.getFunction()->empty()) return;

    auto& func = *domInfo.getFunction();

    auto* entry = domInfo.getEntry();
    root_ = std::make_unique<Node>(entry);
    nodes_[entry] = root_.get();

    std::queue<Node*> queue;
    queue.push(root_.get());

    while (!queue.empty()) {
        auto* current = queue.front();
        queue.pop();

        for (auto& BB : func) {
            if (domInfo.getImmediateDominator(BB) == current->bb) {
                auto child = std::make_unique<Node>(BB, current);
                nodes_[BB] = child.get();
                queue.push(child.get());
                current->children.push_back(std::move(child));
            }
        }
    }
}

template <bool IsPostDom>
typename DominatorTreeBase<IsPostDom>::Node*
DominatorTreeBase<IsPostDom>::getNode(BasicBlock* BB) const {
    auto it = nodes_.find(BB);
    return it != nodes_.end() ? it->second : nullptr;
}

template <bool IsPostDom>
bool DominatorTreeBase<IsPostDom>::dominates(BasicBlock* A,
                                             BasicBlock* B) const {
    return domInfo_->dominates(A, B);
}

template <bool IsPostDom>
typename DominatorTreeBase<IsPostDom>::Node*
DominatorTreeBase<IsPostDom>::findLCA(BasicBlock* A, BasicBlock* B) const {
    auto* nodeA = getNode(A);
    auto* nodeB = getNode(B);
    if (!nodeA || !nodeB) return nullptr;

    while (nodeA->level > nodeB->level) {
        nodeA = nodeA->parent;
    }
    while (nodeB->level > nodeA->level) {
        nodeB = nodeB->parent;
    }

    while (nodeA != nodeB) {
        nodeA = nodeA->parent;
        nodeB = nodeB->parent;
    }

    return nodeA;
}

template <bool IsPostDom>
std::vector<typename DominatorTreeBase<IsPostDom>::Node*>
DominatorTreeBase<IsPostDom>::getNodesAtLevel(int level) const {
    std::vector<Node*> result;
    if (root_) {
        collectNodesAtLevel(root_.get(), level, result);
    }
    return result;
}

template <bool IsPostDom>
void DominatorTreeBase<IsPostDom>::collectNodesAtLevel(
    Node* node, int targetLevel, std::vector<Node*>& result) const {
    if (!node) return;

    if (node->level == targetLevel) {
        result.push_back(node);
    } else if (node->level > targetLevel) {
        return;
    }

    for (auto& child : node->children) {
        collectNodesAtLevel(child.get(), targetLevel, result);
    }
}

template <bool IsPostDom>
void DominatorTreeBase<IsPostDom>::print() const {
    if (root_) {
        printNode(root_.get());
    }
}

template <bool IsPostDom>
void DominatorTreeBase<IsPostDom>::printNode(Node* node, int indent) const {
    if (!node) return;

    for (int i = 0; i < indent; ++i) {
        std::cout << "  ";
    }
    std::cout << node->bb->getName() << " (level " << node->level << ")\n";

    for (auto& child : node->children) {
        printNode(child.get(), indent + 1);
    }
}

template class DominanceInfoBase<false>;
template class DominanceInfoBase<true>;
template class DominatorTreeBase<false>;
template class DominatorTreeBase<true>;

}  // namespace midend