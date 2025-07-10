#pragma once

#include "IR/Function.h"
#include "IR/BasicBlock.h"
#include "Pass/Pass.h"
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <memory>

namespace midend {

class DominatorTree;

/// Represents dominance information for a function
class DominanceInfo : public AnalysisResult {
public:
    using BBSet = std::set<BasicBlock*>;
    using BBVector = std::vector<BasicBlock*>;

private:
    Function* function_;
    std::unordered_map<BasicBlock*, BBSet> dominators_;
    std::unordered_map<BasicBlock*, BasicBlock*> immediateDominators_;
    std::unordered_map<BasicBlock*, BBSet> dominanceFrontier_;
    std::unique_ptr<DominatorTree> domTree_;
    mutable std::unordered_map<BasicBlock*, BBSet> dominatedCache_;
    
    void computeDominators();
    void computeImmediateDominators();
    void computeDominanceFrontier();
    void buildDominatorTree();

public:
    explicit DominanceInfo(Function* F);
    ~DominanceInfo();
    
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
    const DominatorTree* getDominatorTree() const;
    
    /// Verify the dominance information is correct
    bool verify() const;
    
    /// Print dominance information for debugging
    void print() const;
    
    /// Get the function this dominance info is for
    Function* getFunction() const { return function_; }
};

/// Represents the dominator tree structure
class DominatorTree {
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
    const DominanceInfo* domInfo_;
    
public:
    explicit DominatorTree(const DominanceInfo& domInfo);
    
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
    void collectNodesAtLevel(Node* node, int targetLevel, std::vector<Node*>& result) const;
};

/// Analysis pass that computes dominance information
class DominanceAnalysis : public AnalysisBase<DominanceAnalysis> {
public:
    using Result = std::unique_ptr<DominanceInfo>;
    
    DominanceAnalysis() : AnalysisBase("DominanceAnalysis") {}
    
    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<DominanceInfo>(&f);
    }
    
    bool supportsFunction() const override { return true; }
    
    static Result run(Function& F) {
        return std::make_unique<DominanceInfo>(&F);
    }
};

} // namespace midend