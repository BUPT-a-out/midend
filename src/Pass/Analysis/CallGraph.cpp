#include "Pass/Analysis/CallGraph.h"

#include <iostream>
#include <queue>

#include "IR/BasicBlock.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/AliasAnalysis.h"

namespace midend {

CallGraph::CallGraph(Module* M, AnalysisManager* AM)
    : module_(M), analysisManager_(AM) {
    buildCallGraph();
    computeSCCs();
    analyzeSideEffects();
}

void CallGraph::buildCallGraph() {
    for (Function* F : *module_) {
        nodes_[F] = std::make_unique<CallGraphNode>(F);
    }

    for (Function* F : *module_) {
        if (F->isDeclaration()) continue;

        CallGraphNode* callerNode = nodes_[F].get();

        for (BasicBlock* BB : *F) {
            for (Instruction* I : *BB) {
                if (auto* call = dyn_cast<CallInst>(I)) {
                    if (Function* callee = call->getCalledFunction()) {
                        if (auto* calleeNode = getNode(callee)) {
                            callerNode->addCallee(calleeNode);
                        }
                    }
                }
            }
        }
    }
}

void CallGraph::computeSCCs() {
    TarjanState state;

    for (auto& [F, node] : nodes_) {
        if (state.indices.find(node.get()) == state.indices.end()) {
            tarjanVisit(node.get(), state);
        }
    }
}

void CallGraph::tarjanVisit(CallGraphNode* node, TarjanState& state) {
    state.indices[node] = state.index;
    state.lowlinks[node] = state.index;
    state.index++;
    state.stack.push(node);
    state.onStack.insert(node);

    for (CallGraphNode* callee : node->getCallees()) {
        if (state.indices.find(callee) == state.indices.end()) {
            tarjanVisit(callee, state);
            state.lowlinks[node] =
                std::min(state.lowlinks[node], state.lowlinks[callee]);
        } else if (state.onStack.count(callee)) {
            state.lowlinks[node] =
                std::min(state.lowlinks[node], state.indices[callee]);
        }
    }

    if (state.lowlinks[node] == state.indices[node]) {
        std::unordered_set<Function*> scc;
        CallGraphNode* current;

        do {
            current = state.stack.top();
            state.stack.pop();
            state.onStack.erase(current);
            scc.insert(current->getFunction());
        } while (current != node);

        size_t sccIndex = sccs_.size();
        sccs_.push_back(std::move(scc));

        for (Function* F : sccs_.back()) {
            functionToSCC_[F] = sccIndex;
        }
    }
}

void CallGraph::analyzeSideEffects() {
    for (Function* F : *module_) {
        std::unordered_set<Function*> visited;
        hasSideEffectsInternal(F, visited);
    }
}

bool CallGraph::hasSideEffectsInternal(Function* F,
                                       std::unordered_set<Function*>& visited) {
    if (visited.count(F)) {
        return false;
    }

    auto it = sideEffectCache_.find(F);
    if (it != sideEffectCache_.end()) {
        return it->second;
    }

    visited.insert(F);

    bool hasSideEffect = false;

    if (F->isDeclaration()) {
        hasSideEffect = true;
    } else {
        // Get alias analysis result - use AnalysisManager if available,
        // otherwise run directly
        AliasAnalysis::Result* aliasInfo = nullptr;
        std::unique_ptr<AnalysisResult> aliasResultOwner;

        if (analysisManager_) {
            aliasInfo = analysisManager_->getAnalysis<AliasAnalysis::Result>(
                "AliasAnalysis", *F);
        }

        for (BasicBlock* BB : *F) {
            for (Instruction* I : *BB) {
                // Check for stores to global variables or function parameters
                if (isa<StoreInst>(I)) {
                    if (auto* store = cast<StoreInst>(I)) {
                        Value* ptr = store->getPointerOperand();
                        // Walk through GEPs to find the base pointer
                        while (auto* gep = dyn_cast<GetElementPtrInst>(ptr)) {
                            ptr = gep->getPointerOperand();
                        }
                        if (isa<GlobalVariable>(ptr)) {
                            hasSideEffect = true;
                            break;
                        }
                        for (auto it = F->arg_begin(); it != F->arg_end();
                             ++it) {
                            Argument* arg = it->get();
                            if (aliasInfo->alias(arg, ptr) !=
                                AliasAnalysis::AliasResult::NoAlias) {
                                hasSideEffect = true;
                                break;
                            }
                        }
                    }
                } else if (auto* call = dyn_cast<CallInst>(I)) {
                    if (Function* callee = call->getCalledFunction()) {
                        if (hasSideEffectsInternal(callee, visited)) {
                            hasSideEffect = true;
                            break;
                        }
                    } else {
                        hasSideEffect = true;
                        break;
                    }
                }
            }
            if (hasSideEffect) break;
        }
    }

    sideEffectCache_[F] = hasSideEffect;
    return hasSideEffect;
}

CallGraph::CallChainIterator::CallChainIterator(Function* F,
                                                const CallGraph& CG)
    : current_(nullptr) {
    if (CallGraphNode* node = CG.getNode(F)) {
        for (CallGraphNode* callee : node->getCallees()) {
            workStack_.push(callee);
        }
        advance();
    }
}

void CallGraph::CallChainIterator::advance() {
    current_ = nullptr;

    while (!workStack_.empty()) {
        CallGraphNode* node = workStack_.top();
        workStack_.pop();

        if (visited_.count(node->getFunction())) {
            continue;
        }

        visited_.insert(node->getFunction());
        current_ = node->getFunction();

        for (CallGraphNode* callee : node->getCallees()) {
            workStack_.push(callee);
        }

        break;
    }
}

void CallGraph::buildSuperGraph() const {
    superGraph_ = std::make_unique<SuperGraph>();

    for (size_t i = 0; i < sccs_.size(); ++i) {
        SuperNode* superNode = superGraph_->addSuperNode(i, this);

        for (Function* F : sccs_[i]) {
            superNode->addFunction(F);
            superGraph_->mapFunction(F, superNode);
        }
    }

    struct PairHash {
        size_t operator()(const std::pair<SuperNode*, SuperNode*>& p) const {
            auto h1 = std::hash<void*>{}(p.first);
            auto h2 = std::hash<void*>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    std::unordered_set<std::pair<SuperNode*, SuperNode*>, PairHash> addedEdges;

    for (auto& [F, node] : nodes_) {
        SuperNode* fromSuperNode = superGraph_->getSuperNode(F);
        if (!fromSuperNode) continue;

        for (CallGraphNode* calleeNode : node->getCallees()) {
            Function* callee = calleeNode->getFunction();
            SuperNode* toSuperNode = superGraph_->getSuperNode(callee);

            if (!toSuperNode || fromSuperNode == toSuperNode) {
                continue;
            }

            std::pair<SuperNode*, SuperNode*> edge = {fromSuperNode,
                                                      toSuperNode};
            if (addedEdges.find(edge) == addedEdges.end()) {
                fromSuperNode->addSuccessor(toSuperNode);
                addedEdges.insert(edge);
            }
        }
    }
}

bool SuperNode::isSCC() const {
    if (functions_.size() > 1) {
        return true;
    }

    // For single-function SCCs, check if the function is self-recursive
    if (functions_.size() == 1 && callGraph_) {
        Function* singleFunction = *functions_.begin();
        return callGraph_->isInSCC(singleFunction);
    }

    return false;
}

const std::vector<SuperNode*>& SuperGraph::getReverseTopologicalOrder() const {
    if (!reverseTopologicalOrderCached_) {
        reverseTopologicalOrder_.clear();
        reverseTopologicalOrder_.reserve(superNodes_.size());

        std::unordered_map<SuperNode*, int> outDegree;
        std::queue<SuperNode*> zeroOutDegree;

        outDegree.reserve(superNodes_.size());

        for (const auto& superNode : superNodes_) {
            outDegree[superNode.get()] = superNode->getSuccessors().size();
            if (outDegree[superNode.get()] == 0) {
                zeroOutDegree.push(superNode.get());
            }
        }

        while (!zeroOutDegree.empty()) {
            SuperNode* current = zeroOutDegree.front();
            zeroOutDegree.pop();
            reverseTopologicalOrder_.push_back(current);

            for (SuperNode* predecessor : current->getPredecessors()) {
                outDegree[predecessor]--;
                if (outDegree[predecessor] == 0) {
                    zeroOutDegree.push(predecessor);
                }
            }
        }

        reverseTopologicalOrderCached_ = true;
    }
    return reverseTopologicalOrder_;
}

void SuperGraph::print() const {
    std::cout << "=== Super Graph (SCC Condensation) ===\n";

    for (const auto& superNode : superNodes_) {
        std::cout << "SuperNode " << superNode->getSCCIndex() << " (";
        std::cout << superNode->size() << " functions): ";

        for (Function* F : superNode->getFunctions()) {
            std::cout << F->getName() << " ";
        }
        std::cout << "\n";

        std::cout << "  Successors: ";
        for (SuperNode* successor : superNode->getSuccessors()) {
            std::cout << "SN" << successor->getSCCIndex() << " ";
        }
        std::cout << "\n";

        std::cout << "  Predecessors: ";
        for (SuperNode* predecessor : superNode->getPredecessors()) {
            std::cout << "SN" << predecessor->getSCCIndex() << " ";
        }
        std::cout << "\n\n";
    }

    std::cout << "=== Iteration Order ===\n";
    for (SuperNode* superNode : *this) {
        std::cout << "SN" << superNode->getSCCIndex() << " ";
    }
    std::cout << "\n";
}

void CallGraph::print() const {
    std::cout << "=== Call Graph ===\n";

    for (auto& [F, node] : nodes_) {
        std::cout << "Function: " << F->getName() << "\n";
        std::cout << "  Calls: ";
        for (CallGraphNode* callee : node->getCallees()) {
            std::cout << callee->getFunction()->getName() << " ";
        }
        std::cout << "\n";
        std::cout << "  Called by: ";
        for (CallGraphNode* caller : node->getCallers()) {
            std::cout << caller->getFunction()->getName() << " ";
        }
        std::cout << "\n";
        std::cout << "  In SCC: " << (isInSCC(F) ? "Yes" : "No") << "\n";
        std::cout << "  Has side effects: "
                  << (hasSideEffects(F) ? "Yes" : "No") << "\n";
        std::cout << "\n";
    }

    std::cout << "=== Strongly Connected Components ===\n";
    for (size_t i = 0; i < sccs_.size(); ++i) {
        if (sccs_[i].size() > 1) {
            std::cout << "SCC " << i << ": ";
            for (Function* F : sccs_[i]) {
                std::cout << F->getName() << " ";
            }
            std::cout << "\n";
        }
    }

    if (superGraph_) {
        superGraph_->print();
    }
}

}  // namespace midend