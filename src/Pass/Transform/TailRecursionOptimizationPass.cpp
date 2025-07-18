#include "Pass/Transform/TailRecursionOptimizationPass.h"

#include <iterator>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "Pass/Pass.h"
#include "Support/Casting.h"

namespace midend {

bool TailRecursionOptimizationPass::runOnFunction(Function& function,
                                                  AnalysisManager&) {
    if (!function.isDefinition()) {
        return false;
    }

    std::vector<CallInst*> allRecursiveCalls;
    std::vector<TailCall> tailCalls;

    for (auto* block : function) {
        for (auto* inst : *block) {
            if (auto* callInst = dyn_cast<CallInst>(inst)) {
                if (callInst->getCalledFunction() == &function) {
                    allRecursiveCalls.push_back(callInst);
                    if (auto* retInst = getTailCallReturnInst(callInst)) {
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

std::vector<TailRecursionOptimizationPass::TailCall>
TailRecursionOptimizationPass::findTailCalls(Function& function) {
    std::vector<TailCall> tailCalls;

    for (auto* block : function) {
        for (auto* inst : *block) {
            if (auto* callInst = dyn_cast<CallInst>(inst)) {
                if (callInst->getCalledFunction() == &function) {
                    if (auto* retInst = getTailCallReturnInst(callInst)) {
                        tailCalls.push_back({callInst, retInst, block});
                    }
                }
            }
        }
    }

    return tailCalls;
}

ReturnInst* TailRecursionOptimizationPass::getTailCallReturnInst(
    CallInst* callInst) {
    // If the call has no uses, it can only be a tail call if it's followed by a
    // void return
    if (callInst->users().empty()) {
        auto* terminator = callInst->getParent()->getTerminator();
        if (auto* retInst = dyn_cast<ReturnInst>(terminator)) {
            if (retInst->getReturnValue() == nullptr) {
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
        return nullptr;
    }

    return tailCallReturn;
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
    auto* loopHeader =
        BasicBlock::Create(function.getContext(), "tail_recursion_loop");
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