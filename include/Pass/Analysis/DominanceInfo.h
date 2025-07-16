#pragma once

#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "Pass/Pass.h"

namespace midend {

template <bool IsPostDom>
class DominatorTreeBase;

/// Template base class for dominance information
template <bool IsPostDom>
class DominanceInfoBase : public AnalysisResult {
   public:
    using BBSet = std::set<BasicBlock*>;
    using BBVector = std::vector<BasicBlock*>;

   private:
    Function* function_;
    std::unordered_map<BasicBlock*, BBSet> dominators_;
    std::unordered_map<BasicBlock*, BasicBlock*> immediateDominators_;
    std::unordered_map<BasicBlock*, BBSet> dominanceFrontier_;
    std::unique_ptr<DominatorTreeBase<IsPostDom>> domTree_;
    mutable BBVector exitBlocks_;
    mutable BBSet exitBlocksSet_;
    mutable BasicBlock* virtualExit_ = nullptr;
    mutable bool useVirtualBlock_ = false;
    mutable std::unordered_map<BasicBlock*, BBSet> dominatedCache_;

    void computeDominators();
    void computeImmediateDominators();
    void computeDominanceFrontier();
    void buildDominatorTree();

   public:
    explicit DominanceInfoBase(Function* F);
    ~DominanceInfoBase();

    bool createdVirtualExit() const;

    /// Check if A dominates B
    bool dominates(BasicBlock* A, BasicBlock* B) const;

    /// Check if A strictly dominates B
    bool strictlyDominates(BasicBlock* A, BasicBlock* B) const;

    /// Get immediate dominator of BB
    BasicBlock* getImmediateDominator(BasicBlock* BB) const;

    /// Get all dominators of BB
    const BBSet& getDominators(BasicBlock* BB) const;

    /// Get dominance frontier of BB
    const BBSet& getDominanceFrontier(BasicBlock* BB) const;

    /// Get all basic blocks dominated by BB
    const BBSet& getDominated(BasicBlock* BB) const;

    /// Get the dominator tree
    const DominatorTreeBase<IsPostDom>* getDominatorTree() const;

    /// Verify the dominance information is correct
    bool verify() const;

    /// Print dominance information for debugging
    void print() const;

    /// Get the function this dominance info is for
    Function* getFunction() const { return function_; }

    /// Compute reverse post-order traversal of the CFG
    BBVector computeReversePostOrder() const;

    /// Helper functions for handling forward/reverse CFG traversal
    std::vector<BasicBlock*> getPreds(BasicBlock* BB) const;
    std::vector<BasicBlock*> getSuccs(BasicBlock* BB) const;
    BasicBlock* getEntry() const;
    BasicBlock* getVirtualExit() const;
    std::vector<BasicBlock*> getVirtualExitPreds() const;
    bool isVirtualExit(BasicBlock* BB) const;

   private:
};

/// Template base class for dominator tree structure
template <bool IsPostDom>
class DominatorTreeBase {
   public:
    struct Node {
        BasicBlock* bb;
        Node* parent;
        std::vector<std::unique_ptr<Node>> children;
        int level;

        Node(BasicBlock* BB, Node* Parent = nullptr)
            : bb(BB), parent(Parent), level(Parent ? Parent->level + 1 : 0) {}
    };

   private:
    std::unique_ptr<Node> root_;
    std::unordered_map<BasicBlock*, Node*> nodes_;
    const DominanceInfoBase<IsPostDom>* domInfo_;

   public:
    explicit DominatorTreeBase(const DominanceInfoBase<IsPostDom>& domInfo);

    /// Get the root node (entry block)
    Node* getRoot() const { return root_.get(); }

    /// Get node for a basic block
    Node* getNode(BasicBlock* BB) const;

    /// Check if A dominates B using the tree
    bool dominates(BasicBlock* A, BasicBlock* B) const;

    /// Find lowest common ancestor in dominator tree
    Node* findLCA(BasicBlock* A, BasicBlock* B) const;

    /// Get all nodes at a given level
    std::vector<Node*> getNodesAtLevel(int level) const;

    /// Print the dominator tree
    void print() const;

   private:
    void printNode(Node* node, int indent = 0) const;
    void collectNodesAtLevel(Node* node, int targetLevel,
                             std::vector<Node*>& result) const;
};

/// Analysis pass that computes dominance information
class DominanceAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<DominanceInfoBase<false>>;

    static const std::string& getName() {
        static const std::string name = "DominanceAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<DominanceInfoBase<false>>(&f);
    }

    bool supportsFunction() const override { return true; }

    static Result run(Function& F) {
        return std::make_unique<DominanceInfoBase<false>>(&F);
    }
};

/// Analysis pass that computes post-dominance information
class PostDominanceAnalysis : public AnalysisBase {
   public:
    using Result = std::unique_ptr<DominanceInfoBase<true>>;

    static const std::string& getName() {
        static const std::string name = "PostDominanceAnalysis";
        return name;
    }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<DominanceInfoBase<true>>(&f);
    }

    bool supportsFunction() const override { return true; }

    static Result run(Function& F) {
        return std::make_unique<DominanceInfoBase<true>>(&F);
    }
};

// Type aliases for convenience
using DominanceInfo = DominanceInfoBase<false>;
using PostDominanceInfo = DominanceInfoBase<true>;
using DominatorTree = DominatorTreeBase<false>;
using PostDominatorTree = DominatorTreeBase<true>;

}  // namespace midend