#pragma once

#include <algorithm>
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

class CallGraph : public AnalysisResult {
   public:
    using NodeMap =
        std::unordered_map<Function*, std::unique_ptr<CallGraphNode>>;
    using SCCVector = std::vector<std::unordered_set<Function*>>;

   private:
    Module* module_;
    NodeMap nodes_;
    SCCVector sccs_;
    std::unordered_map<Function*, size_t> functionToSCC_;
    std::unordered_map<Function*, bool> sideEffectCache_;

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
                                std::unordered_set<Function*>& visited);

   public:
    explicit CallGraph(Module* M);

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

    /// Check if a function has side effects
    bool hasSideEffects(Function* F) const {
        auto it = sideEffectCache_.find(F);
        return it != sideEffectCache_.end() ? it->second : true;
    }

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

    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override {
        return std::make_unique<CallGraph>(&m);
    }

    bool supportsModule() const override { return true; }

    static Result run(Module& M) { return std::make_unique<CallGraph>(&M); }
};

}  // namespace midend