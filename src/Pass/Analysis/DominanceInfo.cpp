#include "Pass/Analysis/DominanceInfo.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <iostream>
#include <queue>
#include <stack>
#include <unordered_set>

#include "IR/IRBuilder.h"

namespace midend {

template <bool IsPostDom>
DominanceInfoBase<IsPostDom>::DominanceInfoBase(Function* F) : function_(F) {
    if (!F || F->empty()) return;

    computeLengauerTarjan();
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
                                                  "_virtual_exit");
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

// DFS numbering for Lengauer-Tarjan algorithm
template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::dfsNumbering(BasicBlock* v,
                                                LengauerTarjanData& data) {
    data.dfn[v] = ++data.dfsNum;
    data.vertex.resize(data.dfsNum + 1);
    data.vertex[data.dfsNum] = v;
    data.label[v] = v;
    data.sdom[v] = v;
    data.ancestor[v] = nullptr;

    for (auto* w : getSuccs(v)) {
        if (data.dfn.find(w) == data.dfn.end()) {
            data.parent[w] = v;
            dfsNumbering(w, data);
        }
    }
}

// Link operation for DSU in Lengauer-Tarjan
template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::link(BasicBlock* v, BasicBlock* w,
                                        LengauerTarjanData& data) {
    data.ancestor[w] = v;
}

// Compress path in DSU and update minimum sdom label
template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::compress(BasicBlock* v,
                                            LengauerTarjanData& data) {
    if (data.ancestor[data.ancestor[v]] != nullptr) {
        compress(data.ancestor[v], data);
        if (data.dfn[data.sdom[data.label[data.ancestor[v]]]] <
            data.dfn[data.sdom[data.label[v]]]) {
            data.label[v] = data.label[data.ancestor[v]];
        }
        data.ancestor[v] = data.ancestor[data.ancestor[v]];
    }
}

// Eval operation for finding minimum sdom on path
template <bool IsPostDom>
BasicBlock* DominanceInfoBase<IsPostDom>::eval(BasicBlock* v,
                                               LengauerTarjanData& data) {
    if (data.ancestor[v] == nullptr) {
        return v;
    }
    compress(v, data);
    return data.label[v];
}

template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::computeLengauerTarjan() {
    if (function_->empty()) return;

    LengauerTarjanData data;
    auto* entry = getEntry();

    // Step 1: DFS numbering
    dfsNumbering(entry, data);

    // Step 2: Compute semi-dominators in reverse DFS order
    for (int i = data.dfsNum; i >= 2; --i) {
        BasicBlock* w = data.vertex[i];

        // Consider all predecessors of w
        for (auto* v : getPreds(w)) {
            if (data.dfn.find(v) == data.dfn.end())
                continue;  // Skip unreachable blocks

            BasicBlock* u = eval(v, data);
            if (data.dfn[data.sdom[u]] < data.dfn[data.sdom[w]]) {
                data.sdom[w] = data.sdom[u];
            }
        }

        // Add w to bucket of its semidominator
        data.bucket[data.sdom[w]].push_back(w);
        link(data.parent[w], w, data);

        // Process vertices in parent's bucket
        for (auto* v : data.bucket[data.parent[w]]) {
            BasicBlock* u = eval(v, data);
            if (data.sdom[u] == data.sdom[v]) {
                data.idom[v] = data.parent[w];
            } else {
                data.idom[v] = u;
            }
        }
        data.bucket[data.parent[w]].clear();
    }

    // Step 3: Finalize immediate dominators
    for (int i = 2; i <= data.dfsNum; ++i) {
        BasicBlock* w = data.vertex[i];
        if (data.idom[w] != data.sdom[w]) {
            data.idom[w] = data.idom[data.idom[w]];
        }
    }

    // Step 4: Fill in the immediate dominators map
    immediateDominators_.clear();
    immediateDominators_[entry] = nullptr;
    for (int i = 2; i <= data.dfsNum; ++i) {
        BasicBlock* w = data.vertex[i];
        immediateDominators_[w] = data.idom[w];
    }

    // Step 5: Compute full dominator sets from immediate dominators
    computeDominators();
}

// Compute full dominator sets from immediate dominators
template <bool IsPostDom>
void DominanceInfoBase<IsPostDom>::computeDominators() {
    if (function_->empty()) return;

    dominators_.clear();

    // Entry dominates itself
    auto* entry = getEntry();
    dominators_[entry].insert(entry);

    // For each block, compute its dominators by walking up the idom chain
    for (auto& BB : *function_) {
        if (BB == entry) continue;

        BBSet doms;
        BasicBlock* current = BB;
        while (current != nullptr) {
            doms.insert(current);
            current = immediateDominators_[current];
        }
        dominators_[BB] = std::move(doms);
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

void ReverseIDFCalculator::setDefiningBlocks(
    const std::unordered_set<BasicBlock*>& Blocks) {
    DefiningBlocks_.clear();
    for (auto* BB : Blocks) {
        DefiningBlocks_.insert(BB);
    }
}

void ReverseIDFCalculator::setLiveInBlocks(
    const std::unordered_set<BasicBlock*>& Blocks) {
    LiveInBlocks_.clear();
    for (auto* BB : Blocks) {
        LiveInBlocks_.insert(BB);
    }
    useLiveIn_ = true;
}

void ReverseIDFCalculator::updateDFSNumbers() {
    DFSNumbers_.clear();
    if (!PDT_.getFunction() || PDT_.getFunction()->empty()) {
        return;
    }

    unsigned DFSNum = 0;
    std::unordered_set<BasicBlock*> visited;

    // Post-order DFS traversal for post-dominance using PDT's getSuccs
    std::function<void(BasicBlock*)> dfs = [&](BasicBlock* BB) {
        if (visited.count(BB)) return;
        visited.insert(BB);
        DFSNumbers_[BB] = DFSNum++;

        // Visit successors first (using PDT's getSuccs for correct post-dom
        // traversal)
        for (auto* Succ : PDT_.getSuccs(BB)) {
            if (!visited.count(Succ)) {
                dfs(Succ);
            }
        }
    };

    // Start from post-dominance tree root (virtual exit or single exit)
    auto* entry = PDT_.getEntry();
    if (entry) {
        dfs(entry);
    }
}

void ReverseIDFCalculator::calculatePostDomLevels() {
    PostDomLevels_.clear();
    if (!PDT_.getFunction() || PDT_.getFunction()->empty()) {
        return;
    }

    for (auto& BB : *PDT_.getFunction()) {
        unsigned level = 0;
        BasicBlock* current = BB;

        while (current) {
            BasicBlock* idom = PDT_.getImmediateDominator(current);
            if (!idom) break;
            level++;
            current = idom;
        }

        PostDomLevels_[BB] = level;
    }
}

std::vector<BasicBlock*> ReverseIDFCalculator::getPostDomSuccessors(
    BasicBlock* BB) {
    return PDT_.getSuccs(BB);
}

void ReverseIDFCalculator::calculate(BBVector& IDFBlocks) {
    IDFBlocks.clear();

    // Use a priority queue keyed on dominator tree level so that inserted
    // nodes are handled from the bottom of the dominator tree upwards. We
    // also augment the level with a DFS number to ensure that the blocks
    // are ordered in a deterministic way.
    using DomTreeNodePair =
        std::pair<BasicBlock*, std::pair<unsigned, unsigned>>;

    // Custom comparator for priority queue
    auto cmp = [](const DomTreeNodePair& a, const DomTreeNodePair& b) {
        // Compare by level first, then by DFS number
        if (a.second.first != b.second.first) {
            return a.second.first < b.second.first;
        }
        return a.second.second > b.second.second;
    };

    using IDFPriorityQueue =
        std::priority_queue<DomTreeNodePair, std::vector<DomTreeNodePair>,
                            decltype(cmp)>;

    IDFPriorityQueue PQ(cmp);

    // Update DFS numbers and post-dom levels
    updateDFSNumbers();
    calculatePostDomLevels();

    std::vector<BasicBlock*> Worklist;
    std::set<BasicBlock*> VisitedPQ;
    std::set<BasicBlock*> VisitedWorklist;

    // Initialize priority queue with defining blocks
    for (BasicBlock* BB : DefiningBlocks_) {
        unsigned level = PostDomLevels_[BB];
        unsigned dfsNum = DFSNumbers_[BB];
        PQ.push({BB, {level, dfsNum}});
    }

    while (!PQ.empty()) {
        auto RootPair = PQ.top();
        PQ.pop();
        BasicBlock* Root = RootPair.first;
        unsigned RootLevel = RootPair.second.first;

        // Walk all dominator tree children of Root, inspecting their CFG
        // edges with targets elsewhere on the dominator tree. Only targets
        // whose level is at most Root's level are added to the iterated
        // dominance frontier of the definition set.

        Worklist.clear();
        Worklist.push_back(Root);
        VisitedWorklist.insert(Root);

        while (!Worklist.empty()) {
            BasicBlock* Node = Worklist.back();
            Worklist.pop_back();

            // DoWork lambda - processes each successor (predecessor in
            // post-dom)
            auto DoWork = [&](BasicBlock* Succ) {
                // Get the post-dominance level of successor
                unsigned SuccLevel = PostDomLevels_[Succ];

                // Succ post-dominates Root
                if (SuccLevel > RootLevel) return;

                // If already visited, skip
                if (!VisitedPQ.insert(Succ).second) return;

                // Check liveness constraint
                if (useLiveIn_ && !LiveInBlocks_.count(Succ)) return;

                IDFBlocks.push_back(Succ);

                // Add to priority queue if not in defining blocks
                if (!DefiningBlocks_.count(Succ)) {
                    unsigned succDfsNum = DFSNumbers_[Succ];
                    PQ.push({Succ, {SuccLevel, succDfsNum}});
                }
            };

            // For post-dominance, successors are predecessors in normal CFG
            for (auto* Succ : getPostDomSuccessors(Node)) {
                DoWork(Succ);
            }

            // Process post-dominator tree children
            for (auto& Succ :
                 PDT_.getDominatorTree()->getNode(Node)->children) {
                auto SuccNode = Succ.get();
                if (VisitedWorklist.insert(SuccNode->bb).second) {
                    Worklist.push_back(SuccNode->bb);
                }
            }
        }
    }
}

}  // namespace midend