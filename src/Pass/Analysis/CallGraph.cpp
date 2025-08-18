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

/**
 * @brief Gets the base pointer of a value, traversing through GEPs and casts.
 *
 * @param V The value to find the base pointer for.
 * @return Value* The base pointer (e.g., a GlobalVariable, Argument, or AllocaInst).
 */
static Value* getBasePointer(Value* V) {
    // This is a simplified version. A complete one would handle more cast instructions.
    while (auto* GEP = dyn_cast<GetElementPtrInst>(V)) {
        V = GEP->getPointerOperand();
    }
    return V;
}

/**
 * @brief Recursively collects all values (global variables and pointer arguments)
 * that the given function F might modify.
 *
 * This is the internal implementation that performs the traversal.
 *
 * @param F The function to analyze.
 * @param visited The set of functions currently in the recursion stack to detect cycles.
 * @return A set of base pointers that are affected by F.
 */
std::unordered_set<Value*> CallGraph::collectAffectedValues_recursive(
    Function* F, std::unordered_set<Function*>& visited) const {
    
    // 1. Cache Check: If already computed, return the result.
    if (affectedValuesCache_.count(F)) {
        return affectedValuesCache_.at(F);
    }

    // 2. Cycle Detection: If we are already visiting this function in the current path,
    // return an empty set to break the cycle. The effects will be aggregated up the stack.
    if (visited.count(F)) {
        return {};
    }
    visited.insert(F);

    std::unordered_set<Value*> affectedValues;

    // 3. Conservative assumption for external functions.
    if (F->isDeclaration()) {
        // Assume external functions can modify any pointer argument and any global variable.
        for (auto it = F->arg_begin(); it != F->arg_end(); ++it) {
        
            if (it->get()->getType()->isPointerType()) {
                affectedValues.insert(it->get());
            }
        }
        for (GlobalVariable* GV : module_->globals()) {
            affectedValues.insert(GV);
        }
    } else {
        // 4. Analyze the function body.
        AliasAnalysis::Result* aliasInfo = nullptr;
        if (analysisManager_) {
            aliasInfo = analysisManager_->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *F);
        }

        for (BasicBlock* BB : *F) {
            for (Instruction* I : *BB) {
                // Case A: Direct modification via a store instruction.
                if (auto* store = dyn_cast<StoreInst>(I)) {
                    Value* storedPtr = getBasePointer(store->getPointerOperand());
                    if (isa<GlobalVariable>(storedPtr) || isa<Argument>(storedPtr)) {
                        affectedValues.insert(storedPtr);
                    }
                }
                // Case B: Indirect modification via a call instruction.
                else if (auto* call = dyn_cast<CallInst>(I)) {
                    Function* callee = call->getCalledFunction();
                    
                    if (callee) { // Direct call
                        auto calleeAffected = collectAffectedValues_recursive(callee, visited);

                        for (Value* affectedVal : calleeAffected) {
                            // If callee modifies a global, it's a side effect in the caller.
                            if (isa<GlobalVariable>(affectedVal)) {
                                affectedValues.insert(affectedVal);
                            }
                            // If callee modifies one of its arguments, map it back to the caller's value.
                            else if (auto* arg = dyn_cast<Argument>(affectedVal)) {
                                unsigned int argNo = arg->getArgNo();
                                if (argNo < call->getNumArgOperands()) {
                                    Value* callerArg = getBasePointer(call->getArgOperand(argNo));
                                    affectedValues.insert(callerArg);
                                }
                            }
                        }
                    } else { // Indirect call (function pointer)
                        // Conservatively assume it can modify any passed pointer argument and any global.
                        for (unsigned i = 0; i < call->getNumArgOperands(); ++i) {
                            Value* arg = call->getArgOperand(i);
                            if (arg->getType()->isPointerType()) {
                                affectedValues.insert(getBasePointer(arg));
                            }
                        }
                        for (GlobalVariable* GV : module_->globals()) {
                            affectedValues.insert(GV);
                        }
                    }
                }
            }
        }
    }

    // 5. Backtrack and Cache the result.
    visited.erase(F);
    affectedValuesCache_[F] = affectedValues;
    return affectedValues;
}

/**
 * @brief Returns the set of all global variables and arguments that Function F might modify.
 * * This is the public interface that manages the top-level call.
 *
 * @param F The function to analyze.
 * @return const std::unordered_set<Value*>& A reference to the cached set of affected values.
 */
const std::unordered_set<Value*>& CallGraph::getAffectedValues(Function* F) const {
    if (affectedValuesCache_.find(F) == affectedValuesCache_.end()) {
        std::unordered_set<Function*> visited;
        collectAffectedValues_recursive(F, visited);
    }
    return affectedValuesCache_.at(F);
}

/**
 * @brief Checks if a function F has a side effect on a specific value.
 *
 * A side effect is defined as a potential modification (store) to the memory
 * location pointed to by `value`. `value` should be a global variable or a
 * pointer-type argument.
 *
 * @param F The function to check.
 * @param value The value (e.g., a global or argument) to check for modification.
 * @return true if F or any of its callees might modify `value`.
 * @return false otherwise.
 */
bool CallGraph::hasSideEffectsOn(Function* F, Value* value) {
    // 1. Get the set of all values modified by F and its callees.
    const auto& affectedSet = getAffectedValues(F);

    // 2. Get the base pointer of the value we are querying.
    Value* baseValue = getBasePointer(value);

    // 3. Check if the base pointer is in the affected set.
    if (affectedSet.count(baseValue)) {
        return true;
    }

    // 4. Use Alias Analysis for more precision.
    // Check if any value in the affected set may alias with our target value.
    AliasAnalysis::Result* aliasInfo = nullptr;
    if (analysisManager_) {
        // Alias analysis is function-specific, so we run it on F's context.
        aliasInfo = analysisManager_->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *F);
    }

    if (aliasInfo) {
        for (Value* affected : affectedSet) {
            if (aliasInfo->alias(affected, baseValue) != AliasAnalysis::AliasResult::NoAlias) {
                return true;
            }
        }
    }
    
    return false;
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

bool CallGraph::isPureFunction(Function* F) const {
    auto it = pureCache_.find(F);
    if (it != pureCache_.end()) {
        return it->second;
    }

    std::unordered_set<Function*> visited;
    return isPureFunctionInternal(F, visited);
}

bool CallGraph::isPureFunctionInternal(
    Function* F, std::unordered_set<Function*>& visited) const {
    auto it = pureCache_.find(F);
    if (it != pureCache_.end()) {
        return it->second;
    }

    // If we're visiting this function, assume it's pure for now to break cycles
    // This will be corrected if we find evidence it's not pure
    if (visited.count(F)) {
        return true;
    }

    visited.insert(F);

    // Must not have side effects
    if (hasSideEffects(F)) {
        pureCache_[F] = false;
        return false;
    }

    if (F->isDeclaration()) {
        pureCache_[F] = false;
        return false;
    }

    AliasAnalysis::Result* aliasInfo = nullptr;
    if (analysisManager_) {
        aliasInfo = analysisManager_->getAnalysis<AliasAnalysis::Result>(
            "AliasAnalysis", *F);
    }

    for (BasicBlock* BB : *F) {
        for (Instruction* I : *BB) {
            if (auto* load = dyn_cast<LoadInst>(I)) {
                Value* ptr = load->getPointerOperand();

                // Walk through GEPs to find the base pointer
                while (auto* gep = dyn_cast<GetElementPtrInst>(ptr)) {
                    ptr = gep->getPointerOperand();
                }

                // Check if loading from global variable
                if (isa<GlobalVariable>(ptr)) {
                    pureCache_[F] = false;
                    return false;
                }

                if (aliasInfo) {
                    for (auto it = F->arg_begin(); it != F->arg_end(); ++it) {
                        Argument* arg = it->get();
                        if (aliasInfo->alias(arg, ptr) !=
                            AliasAnalysis::AliasResult::NoAlias) {
                            pureCache_[F] = false;
                            return false;
                        }
                    }
                } else {
                    return false;
                }
            }

            if (auto* call = dyn_cast<CallInst>(I)) {
                if (Function* callee = call->getCalledFunction()) {
                    if (!isPureFunctionInternal(callee, visited)) {
                        pureCache_[F] = false;
                        return false;
                    }
                } else {
                    pureCache_[F] = false;
                    return false;
                }
            }
        }
    }

    pureCache_[F] = true;
    return true;
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
        std::cout << "  Is pure function: "
                  << (isPureFunction(F) ? "Yes" : "No") << "\n";
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