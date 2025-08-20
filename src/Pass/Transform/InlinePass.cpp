#include "Pass/Transform/InlinePass.h"

#include <algorithm>
#include <iostream>
#include <vector>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/CallGraph.h"
#include "Support/Casting.h"

namespace midend {

unsigned InlinePass::inlineThreshold_ = 100;
unsigned InlinePass::maxSizeGrowthThreshold_ = 1000;

bool InlinePass::runOnModule(Module& module, AnalysisManager& am) {
    auto* cg = am.getAnalysis<CallGraph>(CallGraphAnalysis::getName(), module);
    if (!cg) {
        std::cout << "Warning: CallGraph analysis not available." << std::endl;
        return false;
    }

    bool changed = false;

    const SuperGraph& superGraph = cg->getSuperGraph();

    for (SuperNode* superNode : superGraph) {
        std::unordered_set<Function*> functions;
        for (Function* func : superNode->getFunctions()) {
            functions.insert(func);
        }

        for (Function* function : superNode->getFunctions()) {
            if (function->isDeclaration()) {
                continue;
            }

            std::vector<std::tuple<CallInst*, Function*, unsigned>>
                inlineCandidates;

            for (auto& bb : *function) {
                for (auto* inst : *bb) {
                    if (auto* call = dyn_cast<CallInst>(inst)) {
                        Function* callee = call->getCalledFunction();
                        if (callee && !callee->isDeclaration() &&
                            callee != function && !functions.count(callee)) {
                            InlineCost cost =
                                calculateInlineCost(callee, call, *cg);

                            if (cost.shouldInline) {
                                inlineCandidates.push_back(
                                    {call, callee, cost.cost});
                            }
                        }
                    }
                }
            }

            std::sort(inlineCandidates.begin(), inlineCandidates.end(),
                      [](const auto& a, const auto& b) {
                          return std::get<2>(a) < std::get<2>(b);
                      });

            unsigned totalSizeGrowth = 0;
            for (auto& [call, callee, cost] : inlineCandidates) {
                if (totalSizeGrowth + cost > maxSizeGrowthThreshold_) {
                    break;
                }
                try {
                    if (inlineFunction(call)) {
                        totalSizeGrowth += cost;
                        changed = true;
                    }
                } catch (...) {
                    continue;
                }
            }
        }
    }

    return changed;
}

InlinePass::InlineCost InlinePass::calculateInlineCost(Function* callee,
                                                       CallInst*,
                                                       const CallGraph& cg) {
    InlineCost result = {0, false};

    if (cg.isInSCC(callee)) {
        return result;
    }

    if (callee->isDeclaration()) {
        return result;
    }

    if (cg.hasSideEffects(callee)) {
        result.cost += 20;
    }

    unsigned instructionCount = 0;
    unsigned callCount = 0;

    for (auto& bb : *callee) {
        for (auto* inst : *bb) {
            instructionCount++;

            switch (inst->getOpcode()) {
                case Opcode::Call:
                    callCount++;
                    result.cost += 10;
                    break;
                case Opcode::Load:
                case Opcode::Store:
                    result.cost += 2;
                    break;
                case Opcode::Br:
                    if (auto* br = dyn_cast<BranchInst>(inst)) {
                        if (br->isConditional()) {
                            result.cost += 3;
                        }
                    }
                    break;
                case Opcode::Alloca:
                    result.cost += 5;
                    break;
                case Opcode::Mul:
                case Opcode::Div:
                case Opcode::Rem:
                    result.cost += 4;
                    break;
                default:
                    result.cost += 1;
                    break;
            }
        }
    }

    if (callCount > 2) {
        result.cost += callCount * 5;
    }

    if (instructionCount < 10) {
        result.cost = result.cost * 80 / 100;
    }

    result.shouldInline = (result.cost <= inlineThreshold_);
    return result;
}

bool InlinePass::shouldInline(Function* callee, CallInst* callSite,
                              const CallGraph& cg) {
    InlineCost cost = calculateInlineCost(callee, callSite, cg);
    return cost.shouldInline;
}

bool InlinePass::inlineFunction(CallInst* callSite) {
    Function* callee = callSite->getCalledFunction();
    if (!callee || callee->isDeclaration()) {
        return false;
    }

    BasicBlock* callBB = callSite->getParent();
    Function* caller = callBB->getParent();

    std::unordered_set<PHINode*> callBBPhiUsers;

    for (auto use : callBB->users()) {
        if (auto phi = dyn_cast<PHINode>(use->getUser())) {
            callBBPhiUsers.insert(phi);
        }
    }

    if (callee->getNumArgs() != callSite->getNumArgOperands()) {
        return false;
    }

    unsigned inlineId = ++inlineCounter_;
    std::string inlineSuffix = ".inline" + std::to_string(inlineId);

    auto* afterCallBB = BasicBlock::Create(
        callBB->getContext(), callBB->getName() + inlineSuffix + "_after");

    auto bbIt = callBB->getIterator();
    ++bbIt;
    if (bbIt != caller->end()) {
        afterCallBB->insertBefore(*bbIt);
    } else {
        caller->push_back(afterCallBB);
    }

    auto callIt = callSite->getIterator();
    ++callIt;
    std::vector<Instruction*> toMove;
    for (auto it = callIt; it != callBB->end(); ++it) {
        toMove.push_back(*it);
    }
    for (auto* inst : toMove) {
        inst->removeFromParent();
        afterCallBB->push_back(inst);
    }

    std::unordered_map<Value*, Value*> valueMap;

    for (unsigned i = 0; i < callee->getNumArgs(); ++i) {
        valueMap[callee->getArg(i)] = callSite->getArgOperand(i);
    }

    Value* returnValue = nullptr;
    cloneAndMapInstructions(caller, callee, callBB, afterCallBB, valueMap,
                            &returnValue, inlineId);

    if (returnValue) {
        callSite->replaceAllUsesWith(returnValue);
    } else if (callSite->getType() && !callSite->getType()->isVoidType()) {
        returnValue = UndefValue::get(callSite->getType());
        callSite->replaceAllUsesWith(returnValue);
    }

    // Fix up any moved instructions that might reference the return value
    for (auto* inst : *afterCallBB) {
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            if (inst->getOperand(i) == callSite && returnValue) {
                inst->setOperand(i, returnValue);
            }
        }
    }

    callBB->replaceAllUsesBy([&](Use*& use) {
        auto user = use->getUser();
        if (auto phi = dyn_cast<PHINode>(user)) {
            use->set(afterCallBB);
        }
    });

    callSite->eraseFromParent();

    return true;
}

void InlinePass::cloneAndMapInstructions(
    Function* caller, Function* callee, BasicBlock* callBB,
    BasicBlock* afterCallBB, std::unordered_map<Value*, Value*>& valueMap,
    Value** returnValue, unsigned inlineId) {
    std::vector<BasicBlock*> clonedBlocks;
    std::unordered_map<BasicBlock*, BasicBlock*> blockMap;
    std::vector<std::pair<BasicBlock*, Value*>> returnBlocks;

    std::string inlineSuffix = ".inline" + std::to_string(inlineId);

    for (auto* srcBB : *callee) {
        auto* newBB = BasicBlock::Create(
            callBB->getContext(), srcBB->getName() + inlineSuffix, nullptr);
        newBB->insertBefore(afterCallBB);
        blockMap[srcBB] = newBB;
        clonedBlocks.push_back(newBB);
    }

    auto bbIt = callee->begin();
    for (auto* clonedBB : clonedBlocks) {
        BasicBlock* srcBB = *bbIt;

        for (auto* srcInst : *srcBB) {
            if (!srcInst->isTerminator()) {
                if (auto* phi = dyn_cast<PHINode>(srcInst)) {
                    auto* newPhi =
                        PHINode::Create(phi->getType(), phi->getName());
                    clonedBB->push_back(newPhi);
                    valueMap[srcInst] = newPhi;
                } else if (auto* alloca = dyn_cast<AllocaInst>(srcInst)) {
                    Instruction* clonedInst = srcInst->clone();
                    if (!clonedInst) {
                        continue;
                    }
                    caller->getEntryBlock().push_front(clonedInst);
                    valueMap[srcInst] = clonedInst;
                } else {
                    Instruction* clonedInst = srcInst->clone();
                    if (!clonedInst) {
                        continue;
                    }
                    clonedBB->push_back(clonedInst);

                    for (unsigned i = 0; i < srcInst->getNumOperands(); ++i) {
                        Value* op = srcInst->getOperand(i);
                        if (op && valueMap.count(op)) {
                            clonedInst->setOperand(i, valueMap[op]);
                        }
                    }

                    valueMap[srcInst] = clonedInst;
                }
            }
        }
        ++bbIt;
    }

    bbIt = callee->begin();
    for (auto* clonedBB : clonedBlocks) {
        BasicBlock* srcBB = *bbIt;

        for (auto* srcInst : *srcBB) {
            if (auto* srcPhi = dyn_cast<PHINode>(srcInst)) {
                auto* clonedPhi = cast<PHINode>(valueMap[srcInst]);

                for (unsigned i = 0; i < srcPhi->getNumIncomingValues(); ++i) {
                    Value* incomingVal = srcPhi->getIncomingValue(i);
                    BasicBlock* incomingBB = srcPhi->getIncomingBlock(i);

                    if (valueMap.count(incomingVal)) {
                        incomingVal = valueMap[incomingVal];
                    }

                    if (blockMap.count(incomingBB)) {
                        incomingBB = blockMap[incomingBB];
                    }

                    clonedPhi->addIncoming(incomingVal, incomingBB);
                }
            }
        }
        ++bbIt;
    }

    IRBuilder builder(callBB->getContext());
    bbIt = callee->begin();
    for (auto* clonedBB : clonedBlocks) {
        BasicBlock* srcBB = *bbIt;
        builder.setInsertPoint(clonedBB);

        auto* terminator = srcBB->getTerminator();
        if (auto* ret = dyn_cast<ReturnInst>(terminator)) {
            Value* retVal = nullptr;
            if (Value* originalRetVal = ret->getReturnValue()) {
                retVal = originalRetVal;
                if (valueMap.count(originalRetVal)) {
                    retVal = valueMap[originalRetVal];
                }
            }
            returnBlocks.push_back({clonedBB, retVal});
            builder.createBr(afterCallBB);
        } else if (auto* br = dyn_cast<BranchInst>(terminator)) {
            if (br->isConditional()) {
                Value* cond = br->getCondition();
                if (valueMap.count(cond)) {
                    cond = valueMap[cond];
                }
                BasicBlock* trueBB = blockMap[br->getTrueBB()];
                BasicBlock* falseBB = blockMap[br->getFalseBB()];
                builder.createCondBr(cond, trueBB, falseBB);
            } else {
                BasicBlock* dest = blockMap[br->getSuccessor(0)];
                builder.createBr(dest);
            }
        }
        ++bbIt;
    }

    if (!clonedBlocks.empty()) {
        if (auto* term = callBB->getTerminator()) {
            term->eraseFromParent();
        }
        builder.setInsertPoint(callBB);
        builder.createBr(clonedBlocks[0]);
    }

    if (returnValue && !returnBlocks.empty()) {
        builder.setInsertPoint(afterCallBB, afterCallBB->begin());

        if (returnBlocks.size() == 1 && returnBlocks[0].second) {
            // Single return point
            Value* retVal = returnBlocks[0].second;

            // If the return value is a PHI node, we need to create a new PHI
            // in afterCallBB to collect the value
            if (auto* phi = dyn_cast<PHINode>(retVal)) {
                auto* newPhi = builder.createPHI(phi->getType());
                // Add incoming value from the return block
                newPhi->addIncoming(retVal, returnBlocks[0].first);
                *returnValue = newPhi;
            } else {
                *returnValue = retVal;
            }
        } else if (returnBlocks.size() > 1 && returnBlocks[0].second) {
            // Multiple return points
            auto* phi = builder.createPHI(returnBlocks[0].second->getType());
            for (auto& [block, value] : returnBlocks) {
                if (value) {
                    phi->addIncoming(value, block);
                }
            }
            *returnValue = phi;
        }
    }
}

}  // namespace midend
