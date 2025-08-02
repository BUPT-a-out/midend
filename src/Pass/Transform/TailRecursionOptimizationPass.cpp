#include "Pass/Transform/TailRecursionOptimizationPass.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Pass.h"
#include "Support/Casting.h"

namespace midend {

static int tail_recursion_loop_cnt = 0;

bool TailRecursionOptimizationPass::runOnFunction(Function& function,
                                                  AnalysisManager& am) {
    if (!function.isDefinition()) {
        return false;
    }

    auto* callGraph = am.getAnalysis<CallGraph>(CallGraphAnalysis::getName(),
                                                *function.getParent());
    std::vector<CallInst*> allRecursiveCalls;
    std::vector<TailCall> tailCalls;

    for (auto* block : function) {
        for (auto* inst : *block) {
            if (auto* callInst = dyn_cast<CallInst>(inst)) {
                if (callInst->getCalledFunction() == &function) {
                    allRecursiveCalls.push_back(callInst);
                    if (auto* retInst =
                            getTailCallReturnInst(callInst, callGraph)) {
                        tailCalls.push_back({callInst, retInst, block});
                    }
                }
            }
        }
    }

    if (allRecursiveCalls.empty()) {
        return false;
    }

    if (tailCalls.size() != allRecursiveCalls.size()) {
        return false;
    }

    return transformToLoop(function, tailCalls);
}

ReturnInst* TailRecursionOptimizationPass::getTailCallReturnInst(
    CallInst* callInst, const CallGraph* callGraph) {
    BasicBlock* block = callInst->getParent();

    // If the call has no uses, check for void return after verifying no side
    // effects
    if (callInst->users().empty()) {
        auto* terminator = block->getTerminator();
        if (auto* retInst = dyn_cast<ReturnInst>(terminator)) {
            if (retInst->getReturnValue() == nullptr) {
                if (hasSideEffectsBetween(callInst, retInst, callGraph)) {
                    return nullptr;
                }
                return retInst;
            }
        }
        return nullptr;
    }

    ReturnInst* tailCallReturn = nullptr;
    for (auto* use : callInst->users()) {
        auto* user = use->getUser();
        if (auto* retInst = dyn_cast<ReturnInst>(user)) {
            if (retInst->getReturnValue() == callInst) {
                if (tailCallReturn == nullptr) {
                    tailCallReturn = retInst;
                } else {
                    return nullptr;
                }
                continue;
            }
        }
        return nullptr;  // Used by non-return instruction
    }

    if (tailCallReturn) {
        if (hasSideEffectsBetween(callInst, tailCallReturn, callGraph)) {
            return nullptr;
        }
    }

    return tailCallReturn;
}

bool TailRecursionOptimizationPass::hasSideEffectsBetween(
    CallInst* callInst, ReturnInst* retInst, const CallGraph* callGraph) {
    BasicBlock* block = callInst->getParent();

    if (retInst->getParent() != block) {
        return true;
    }

    auto callIt = std::find(block->begin(), block->end(), callInst);
    auto retIt = std::find(block->begin(), block->end(), retInst);

    if (callIt == block->end() || retIt == block->end()) {
        return true;
    }

    auto it = callIt;
    ++it;

    while (it != retIt && it != block->end()) {
        Instruction* inst = *it;

        if (isa<StoreInst>(inst)) {
            return true;
        }

        if (auto* otherCall = dyn_cast<CallInst>(inst)) {
            Function* calledFunc = otherCall->getCalledFunction();
            if (!calledFunc || !callGraph ||
                callGraph->hasSideEffects(calledFunc)) {
                return true;
            }
        }

        ++it;
    }

    return false;
}

bool TailRecursionOptimizationPass::transformToLoop(
    Function& function, const std::vector<TailCall>& tailCalls) {
    BasicBlock* loopHeader = createLoopHeader(function);

    BasicBlock* entryBlock = &function.getEntryBlock();
    std::vector<Instruction*> instructionsToMove;
    Instruction* terminatorToMove = nullptr;

    for (auto* inst : *entryBlock) {
        if (!inst->isTerminator()) {
            instructionsToMove.push_back(inst);
        } else {
            terminatorToMove = inst;
        }
    }

    entryBlock->replaceAllUsesWith(loopHeader);

    createPHINodes(loopHeader, function);

    for (auto* inst : instructionsToMove) {
        inst->removeFromParent();
        loopHeader->push_back(inst);
    }

    if (terminatorToMove) {
        terminatorToMove->removeFromParent();
        loopHeader->push_back(terminatorToMove);
    }

    updateCallsToLoop(tailCalls, loopHeader);

    IRBuilder builder(entryBlock);
    builder.createBr(loopHeader);
    return true;
}

BasicBlock* TailRecursionOptimizationPass::createLoopHeader(
    Function& function) {
    auto* loopHeader = BasicBlock::Create(
        function.getContext(),
        "tail_recursion_loop." + std::to_string(++tail_recursion_loop_cnt));
    auto insertPos = std::next(function.begin());
    function.insert(insertPos, loopHeader);
    return loopHeader;
}

void TailRecursionOptimizationPass::createPHINodes(BasicBlock* loopHeader,
                                                   Function& function) {
    BasicBlock* entryBlock = &function.getEntryBlock();
    IRBuilder builder(loopHeader);

    std::vector<PHINode*> phiNodes;
    for (size_t i = 0; i < function.getNumArgs(); ++i) {
        auto* arg = function.getArg(i);
        auto* phi = builder.createPHI(arg->getType());
        phi->setName(arg->getName() + ".phi");
        phiNodes.push_back(phi);
    }

    for (size_t i = 0; i < function.getNumArgs(); ++i) {
        auto* arg = function.getArg(i);
        auto* phi = phiNodes[i];

        phi->addIncoming(arg, entryBlock);

        arg->replaceAllUsesBy([&](Use* use) {
            auto* user = use->getUser();
            if (user != phi) {
                use->set(phi);
            }
        });
    }
}

void TailRecursionOptimizationPass::updateCallsToLoop(
    const std::vector<TailCall>& tailCalls, BasicBlock* loopHeader) {
    for (const auto& tailCall : tailCalls) {
        CallInst* callInst = tailCall.callInst;
        ReturnInst* retInst = tailCall.returnInst;
        BasicBlock* block = tailCall.block;

        size_t argIndex = 0;
        for (auto* phi : *loopHeader) {
            if (auto* phiNode = dyn_cast<PHINode>(phi)) {
                if (argIndex < callInst->getNumArgOperands()) {
                    Value* newValue = callInst->getArgOperand(argIndex);
                    phiNode->addIncoming(newValue, block);
                }
                ++argIndex;
            }
        }

        callInst->eraseFromParent();
        retInst->eraseFromParent();

        IRBuilder builder(block);
        builder.createBr(loopHeader);
    }
}

REGISTER_PASS(TailRecursionOptimizationPass, "tailrec")

}  // namespace midend