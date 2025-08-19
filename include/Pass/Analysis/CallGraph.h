#pragma once

#include <map>
#include <memory>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IR/Function.h"
#include "IR/Module.h"
#include "Pass/Pass.h"

namespace midend {

class CallGraphNode {
   private:
    Function* function_;
    std::unordered_set<CallGraphNode*> callees_;
    std::unordered_set<CallGraphNode*> callers_;

   public:
    explicit CallGraphNode(Function* F) : function_(F) {}

    Function* getFunction() const { return function_; }

    void addCallee(CallGraphNode* callee) {
        if (callee) {
            callees_.insert(callee);
            callee->callers_.insert(this);
        }
    }

    const std::unordered_set<CallGraphNode*>& getCallees() const {
        return callees_;
    }

    const std::unordered_set<CallGraphNode*>& getCallers() const {
        return callers_;
    }

    bool isLeaf() const { return callees_.empty(); }
    bool isRoot() const { return callers_.empty(); }
};

// Forward declarations
class SuperGraph;
class CallGraph;

/// SuperNode represents a strongly connected component in the super graph
class SuperNode {
   private:
    size_t sccIndex_;
    std::unordered_set<Function*> functions_;
    std::unordered_set<SuperNode*> successors_;
    std::unordered_set<SuperNode*> predecessors_;
    const CallGraph* callGraph_;

   public:
    explicit SuperNode(size_t sccIndex, const CallGraph* cg = nullptr)
        : sccIndex_(sccIndex), callGraph_(cg) {}

    void setCallGraph(const CallGraph* cg) { callGraph_ = cg; }

    /// Get the SCC index this super node represents
    size_t getSCCIndex() const { return sccIndex_; }

    /// Add a function to this super node
    void addFunction(Function* F) { functions_.insert(F); }

    /// Get all functions in this super node
    const std::unordered_set<Function*>& getFunctions() const {
        return functions_;
    }

    /// Check if this super node contains a specific function
    bool containsFunction(Function* F) const { return functions_.count(F) > 0; }

    /// Add a successor super node
    void addSuccessor(SuperNode* successor) {
        if (successor && successor != this) {
            successors_.insert(successor);
            successor->predecessors_.insert(this);
        }
    }

    /// Get successor super nodes
    const std::unordered_set<SuperNode*>& getSuccessors() const {
        return successors_;
    }

    /// Get predecessor super nodes
    const std::unordered_set<SuperNode*>& getPredecessors() const {
        return predecessors_;
    }

    /// Check if this is a leaf node (no successors)
    bool isLeaf() const { return successors_.empty(); }

    /// Check if this is a root node (no predecessors)
    bool isRoot() const { return predecessors_.empty(); }

    /// Get the number of functions in this super node
    size_t size() const { return functions_.size(); }

    /// Check if this super node represents a single function SCC
    bool isTrivial() const { return functions_.size() == 1; }

    /// Check if this super node represents a true SCC (recursive)
    /// This includes both multi-function SCCs and self-recursive functions
    bool isSCC() const;
};

/// SuperGraph represents the DAG of strongly connected components
class SuperGraph {
   private:
    std::vector<std::unique_ptr<SuperNode>> superNodes_;
    std::unordered_map<size_t, SuperNode*> sccIndexToSuperNode_;
    std::unordered_map<Function*, SuperNode*> functionToSuperNode_;
    mutable std::vector<SuperNode*> reverseTopologicalOrder_;
    mutable bool reverseTopologicalOrderCached_;

    /// Get reverse topological order (bottom-up, leaves first)
    const std::vector<SuperNode*>& getReverseTopologicalOrder() const;

   public:
    SuperGraph() : reverseTopologicalOrderCached_(false) {}
    /// Iterator for bottom-up traversal (reverse topological order)
    class ReverseTopologicalIterator {
       private:
        std::vector<SuperNode*>::const_iterator it_;

       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = SuperNode*;
        using difference_type = std::ptrdiff_t;
        using pointer = SuperNode**;
        using reference = SuperNode*&;

        explicit ReverseTopologicalIterator(
            std::vector<SuperNode*>::const_iterator it)
            : it_(it) {}

        SuperNode* operator*() const { return *it_; }
        SuperNode* operator->() const { return *it_; }

        ReverseTopologicalIterator& operator++() {
            ++it_;
            return *this;
        }

        ReverseTopologicalIterator operator++(int) {
            ReverseTopologicalIterator tmp = *this;
            ++it_;
            return tmp;
        }

        bool operator==(const ReverseTopologicalIterator& other) const {
            return it_ == other.it_;
        }

        bool operator!=(const ReverseTopologicalIterator& other) const {
            return it_ != other.it_;
        }
    };

    using iterator = ReverseTopologicalIterator;
    using const_iterator = ReverseTopologicalIterator;

    /// Iterators for super nodes (bottom-up, reverse topological order)
    iterator begin() {
        const auto& order = getReverseTopologicalOrder();
        return iterator(order.begin());
    }

    iterator end() {
        const auto& order = getReverseTopologicalOrder();
        return iterator(order.end());
    }

    const_iterator begin() const {
        const auto& order = getReverseTopologicalOrder();
        return const_iterator(order.begin());
    }

    const_iterator end() const {
        const auto& order = getReverseTopologicalOrder();
        return const_iterator(order.end());
    }

    /// Get the number of super nodes
    size_t size() const { return superNodes_.size(); }
    bool empty() const { return superNodes_.empty(); }

    /// Add a super node
    SuperNode* addSuperNode(size_t sccIndex, const CallGraph* cg = nullptr) {
        auto superNode = std::make_unique<SuperNode>(sccIndex, cg);
        SuperNode* ptr = superNode.get();
        superNodes_.push_back(std::move(superNode));
        sccIndexToSuperNode_[sccIndex] = ptr;
        return ptr;
    }

    /// Get super node by SCC index
    SuperNode* getSuperNode(size_t sccIndex) const {
        auto it = sccIndexToSuperNode_.find(sccIndex);
        return it != sccIndexToSuperNode_.end() ? it->second : nullptr;
    }

    /// Get super node containing a specific function
    SuperNode* getSuperNode(Function* F) const {
        auto it = functionToSuperNode_.find(F);
        return it != functionToSuperNode_.end() ? it->second : nullptr;
    }

    /// Map a function to its super node
    void mapFunction(Function* F, SuperNode* superNode) {
        functionToSuperNode_[F] = superNode;
    }

    /// Print super graph for debugging
    void print() const;
};

class CallGraph : public AnalysisResult {
   public:
    using NodeMap =
        std::unordered_map<Function*, std::unique_ptr<CallGraphNode>>;
    using SCCVector = std::vector<std::unordered_set<Function*>>;

   private:
    Module* module_;
    AnalysisManager* analysisManager_;
    NodeMap nodes_;
    SCCVector sccs_;
    std::unordered_map<Function*, size_t> functionToSCC_;
    mutable std::unordered_map<Function*, bool> sideEffectCache_;
    mutable std::unordered_map<Function*, bool> pureCache_;
    mutable std::unique_ptr<SuperGraph> superGraph_;

    // Tarjan's algorithm state
    struct TarjanState {
        int index = 0;
        std::stack<CallGraphNode*> stack;
        std::unordered_map<CallGraphNode*, int> indices;
        std::unordered_map<CallGraphNode*, int> lowlinks;
        std::unordered_set<CallGraphNode*> onStack;
    };

    void buildCallGraph();
    void computeSCCs();
    void tarjanVisit(CallGraphNode* node, TarjanState& state);
    void analyzeSideEffects();
    bool hasSideEffectsInternal(Function* F,
                                std::unordered_set<Function*>& visited) const;
    bool isPureFunctionInternal(Function* F,
                                std::unordered_set<Function*>& visited) const;
    void buildSuperGraph() const;

   public:
    explicit CallGraph(Module* M, AnalysisManager* AM);

    /// Get the call graph node for a function
    CallGraphNode* getNode(Function* F) const {
        auto it = nodes_.find(F);
        return it != nodes_.end() ? it->second.get() : nullptr;
    }

    /// Check if a function is in a strongly connected component (may recurse)
    bool isInSCC(Function* F) const {
        auto it = functionToSCC_.find(F);
        if (it == functionToSCC_.end()) return false;

        // A function is in an SCC if:
        // 1. The SCC has more than one function, OR
        // 2. The function calls itself (self-recursive)
        if (sccs_[it->second].size() > 1) return true;

        // Check if the function calls itself
        auto* node = getNode(F);
        if (node) {
            for (auto* callee : node->getCallees()) {
                if (callee->getFunction() == F) {
                    return true;
                }
            }
        }
        return false;
    }

    mutable std::map<Function*, std::unordered_set<Value*>>
        affectedValuesCache_;
    std::unordered_set<Value*> collectAffectedValues_recursive(
        Function* F, std::unordered_set<Function*>& visited) const;
    const std::unordered_set<Value*>& getAffectedValues(Function* F) const;
    bool hasSideEffectsOn(Function* F, Value* value);

    mutable std::unordered_map<Function*, std::unordered_set<Value*>>
        requiredValuesCache_;
    std::unordered_set<Value*> collectRequiredValues_recursive(
        Function* F, std::unordered_set<Function*>& visited) const;
    const std::unordered_set<Value*>& getRequiredValues(Function* F) const;

    /// Check if a function has side effects
    bool hasSideEffects(Function* F) const {
        // auto it = sideEffectCache_.find(F);
        // if (it != sideEffectCache_.end()) {
        //     return it->second;
        // }
        std::unordered_set<Function*> visited;
        return hasSideEffectsInternal(F, visited);
    }

    /// Check if a function is pure
    bool isPureFunction(Function* F) const;

    /// Get the SCC containing a function
    const std::unordered_set<Function*>* getSCC(Function* F) const {
        auto it = functionToSCC_.find(F);
        return it != functionToSCC_.end() ? &sccs_[it->second] : nullptr;
    }

    /// Get all SCCs
    const SCCVector& getSCCs() const { return sccs_; }

    /// Iterator for downstream call chain
    class CallChainIterator {
       private:
        std::stack<CallGraphNode*> workStack_;
        std::unordered_set<Function*> visited_;
        Function* current_;

        void advance();

       public:
        CallChainIterator(Function* F, const CallGraph& CG);

        class iterator {
           public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Function*;
            using difference_type = std::ptrdiff_t;
            using pointer = Function**;
            using reference = Function*&;

           private:
            CallChainIterator* container_;
            bool isEnd_;
            Function* current_;

           public:
            iterator(CallChainIterator* container, bool isEnd)
                : container_(container), isEnd_(isEnd), current_(nullptr) {
                if (!isEnd_ && container_) {
                    current_ = container_->current_;
                }
            }

            Function* operator*() const { return current_; }
            Function* operator->() const { return current_; }

            iterator& operator++() {
                if (container_ && !isEnd_) {
                    container_->advance();
                    current_ = container_->current_;
                    if (!current_) {
                        isEnd_ = true;
                    }
                }
                return *this;
            }

            iterator operator++(int) {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const iterator& other) const {
                if (isEnd_ && other.isEnd_) return true;
                if (isEnd_ != other.isEnd_) return false;
                return current_ == other.current_;
            }

            bool operator!=(const iterator& other) const {
                return !(*this == other);
            }
        };

        iterator begin() {
            return iterator(current_ ? this : nullptr, current_ == nullptr);
        }
        iterator end() { return iterator(nullptr, true); }

        Function* next() {
            Function* result = current_;
            advance();
            return result;
        }
    };

    /// Get iterator for downstream call chain of a function
    CallChainIterator getDownstreamIterator(Function* F) const {
        return CallChainIterator(F, *this);
    }

    /// Get the super graph (SCC condensation graph)
    const SuperGraph& getSuperGraph() const {
        if (!superGraph_) {
            buildSuperGraph();
        }
        return *superGraph_;
    }

    /// Get the super node containing a specific function
    SuperNode* getSuperNode(Function* F) const {
        const SuperGraph& sg = getSuperGraph();
        return sg.getSuperNode(F);
    }

    /// Print call graph for debugging
    void print() const;

    /// Get the module this call graph is for
    Module* getModule() const { return module_; }
};

/// Analysis pass that computes call graph information
class CallGraphAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<CallGraph>;

    static const std::string& getName() {
        static const std::string name = "CallGraphAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnModule(Module& m,
                                                AnalysisManager& AM) override {
        return std::make_unique<CallGraph>(&m, &AM);
    }

    bool supportsModule() const override { return true; }

    static Result run(Module& M, AnalysisManager& AM) {
        return std::make_unique<CallGraph>(&M, &AM);
    }
};

}  // namespace midend