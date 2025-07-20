#include "Pass/Analysis/CallGraph.h"

#include <iostream>

#include "IR/BasicBlock.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"

namespace midend {

CallGraph::CallGraph(Module* M) : module_(M) {
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
        for (BasicBlock* BB : *F) {
            for (Instruction* I : *BB) {
                // Check for stores to global variables
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
}

}  // namespace midend