#include "Pass/Analysis/DominanceInfo.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <stack>
#include <unordered_set>

namespace midend {

DominanceInfo::DominanceInfo(Function* F) : function_(F) {
    if (!F || F->empty()) return;

    computeDominators();
    computeImmediateDominators();
    computeDominanceFrontier();
    buildDominatorTree();
}

DominanceInfo::~DominanceInfo() = default;

// https://oi-wiki.org/graph/dominator-tree/#%E6%95%B0%E6%8D%AE%E6%B5%81%E8%BF%AD%E4%BB%A3%E6%B3%95
void DominanceInfo::computeDominators() {
    if (function_->empty()) return;

    auto* entry = &function_->front();

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
            auto predecessors = BB->getPredecessors();
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
void DominanceInfo::computeImmediateDominators() {
    if (function_->empty()) return;

    auto* entry = &function_->front();
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

void DominanceInfo::computeDominanceFrontier() {
    if (function_->empty()) return;

    // Initialize empty frontiers
    for (auto& BB : *function_) {
        dominanceFrontier_[BB] = BBSet();
    }

    // For each basic block
    for (auto& X : *function_) {
        const auto& preds = X->getPredecessors();
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

void DominanceInfo::buildDominatorTree() {
    domTree_ = std::make_unique<DominatorTree>(*this);
}

bool DominanceInfo::dominates(BasicBlock* A, BasicBlock* B) const {
    if (!A || !B) return false;
    auto it = dominators_.find(B);
    if (it == dominators_.end()) return false;
    return it->second.find(A) != it->second.end();
}

bool DominanceInfo::strictlyDominates(BasicBlock* A, BasicBlock* B) const {
    return A != B && dominates(A, B);
}

BasicBlock* DominanceInfo::getImmediateDominator(BasicBlock* BB) const {
    auto it = immediateDominators_.find(BB);
    return it != immediateDominators_.end() ? it->second : nullptr;
}

const DominanceInfo::BBSet& DominanceInfo::getDominators(BasicBlock* BB) const {
    static BBSet empty;
    auto it = dominators_.find(BB);
    return it != dominators_.end() ? it->second : empty;
}

const DominanceInfo::BBSet& DominanceInfo::getDominanceFrontier(
    BasicBlock* BB) const {
    static BBSet empty;
    auto it = dominanceFrontier_.find(BB);
    return it != dominanceFrontier_.end() ? it->second : empty;
}

const DominanceInfo::BBSet& DominanceInfo::getDominated(BasicBlock* BB) const {
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

const DominatorTree* DominanceInfo::getDominatorTree() const {
    return domTree_.get();
}

bool DominanceInfo::verify() const {
    if (function_->empty()) return true;

    auto* entry = &function_->front();

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

void DominanceInfo::print() const {
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

DominatorTree::DominatorTree(const DominanceInfo& domInfo)
    : domInfo_(&domInfo) {
    if (domInfo.getFunction()->empty()) return;

    auto& func = *domInfo.getFunction();

    auto* entry = &func.front();
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

DominatorTree::Node* DominatorTree::getNode(BasicBlock* BB) const {
    auto it = nodes_.find(BB);
    return it != nodes_.end() ? it->second : nullptr;
}

bool DominatorTree::dominates(BasicBlock* A, BasicBlock* B) const {
    return domInfo_->dominates(A, B);
}

DominatorTree::Node* DominatorTree::findLCA(BasicBlock* A,
                                            BasicBlock* B) const {
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

std::vector<DominatorTree::Node*> DominatorTree::getNodesAtLevel(
    int level) const {
    std::vector<Node*> result;
    if (root_) {
        collectNodesAtLevel(root_.get(), level, result);
    }
    return result;
}

void DominatorTree::collectNodesAtLevel(Node* node, int targetLevel,
                                        std::vector<Node*>& result) const {
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

void DominatorTree::print() const {
    if (root_) {
        printNode(root_.get());
    }
}

void DominatorTree::printNode(Node* node, int indent) const {
    if (!node) return;

    for (int i = 0; i < indent; ++i) {
        std::cout << "  ";
    }
    std::cout << node->bb->getName() << " (level " << node->level << ")\n";

    for (auto& child : node->children) {
        printNode(child.get(), indent + 1);
    }
}

DominanceInfo::BBVector DominanceInfo::computeReversePostOrder() const {
    if (function_->empty()) return {};

    BBVector postOrder;
    std::unordered_set<BasicBlock*> visited;

    std::stack<std::pair<BasicBlock*, bool>> stack;
    auto* entry = &function_->front();
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

            auto successors = bb->getSuccessors();
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

}  // namespace midend