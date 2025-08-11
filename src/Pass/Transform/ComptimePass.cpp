#include "Pass/Transform/ComptimePass.h"

#include <algorithm>
#include <iostream>
#include <queue>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instruction.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/DominanceInfo.h"

namespace midend {

void ComptimePass::getAnalysisUsage(
    std::unordered_set<std::string>& required,
    std::unordered_set<std::string>& /* preserved */) const {
    required.insert("DominanceAnalysis");
    required.insert("PostDominanceAnalysis");
}

bool ComptimePass::runOnModule(Module& module, AnalysisManager& am) {
    analysisManager = &am;
    globalValueMap.clear();
    changedValues.clear();
    bool changed = false;

    // Step 1: Collect global variables into global value map
    initializeGlobalValueMap(module);

    // Step 2: Evaluate main function if it exists
    Function* mainFunc = module.getFunction("main");
    if (mainFunc) {
        size_t initialValueMapSize = globalValueMap.size();
        evaluateFunction(mainFunc, {}, true);

        // Debug: Print value map size
        std::cerr << "[ComptimePass] Initial map size: " << initialValueMapSize
                  << ", Final map size: " << globalValueMap.size() << std::endl;

        // Check if we computed any new instruction values
        changed = (globalValueMap.size() > initialValueMapSize);

        // Step 3: Eliminate computed instructions
        size_t instructionsEliminated = eliminateComputedInstructions(mainFunc);

        // Debug: Print eliminated count
        std::cerr << "[ComptimePass] Instructions eliminated: "
                  << instructionsEliminated << std::endl;

        // If we eliminated any instructions, we made changes
        if (instructionsEliminated > 0) {
            changed = true;
        }

        // Step 4: Initialize arrays with computed values
        initializeArrays(module);
    }

    return changed;
}

void ComptimePass::initializeGlobalValueMap(Module& module) {
    for (auto* gv : module.globals()) {
        if (gv->hasInitializer()) {
            globalValueMap[gv] = gv->getInitializer();
        } else {
            // Initialize global variables with zero values
            Type* valueType = gv->getValueType();
            globalValueMap[gv] = createZeroInitializedConstant(valueType);
        }
    }
}

Value* ComptimePass::evaluateFunction(Function* func,
                                      const std::vector<Value*>& args,
                                      bool isMainFunction) {
    if (func->isDeclaration()) {
        return nullptr;
    }

    ValueMap localValueMap;
    ValueMap& valueMap = isMainFunction ? globalValueMap : localValueMap;

    // Bind function arguments
    for (size_t i = 0; i < args.size() && i < func->getNumArgs(); ++i) {
        valueMap[func->getArg(i)] = args[i];
    }

    BasicBlock* currentBlock = &func->getEntryBlock();
    BasicBlock* prevBlock = nullptr;
    Value* returnValue = nullptr;

    // Simulate execution through the function
    while (currentBlock) {
        // Evaluate all instructions in the current block
        evaluateBlock(currentBlock, prevBlock, valueMap, isMainFunction);

        auto* terminator = currentBlock->getTerminator();
        if (!terminator) break;

        if (auto* ret = dyn_cast<ReturnInst>(terminator)) {
            // Handle return instruction
            Value* retVal = ret->getReturnValue();
            if (retVal) {
                returnValue = getValueOrConstant(retVal, valueMap);
            }
            break;
        } else if (auto* br = dyn_cast<BranchInst>(terminator)) {
            prevBlock = currentBlock;

            if (br->isConditional()) {
                Value* cond = getValueOrConstant(br->getCondition(), valueMap);
                if (auto* constCond = dyn_cast<ConstantInt>(cond)) {
                    // Compile-time known condition
                    currentBlock = constCond->getValue() ? br->getTrueBB()
                                                         : br->getFalseBB();
                } else {
                    // Runtime condition - jump to post-immediate dominator
                    currentBlock = getPostImmediateDominator(currentBlock);
                    prevBlock = nullptr;  // Mark as non-compile-time state
                }
            } else {
                currentBlock = br->getSuccessor(0);
            }
        } else {
            break;
        }
    }

    return returnValue;
}

void ComptimePass::evaluateBlock(BasicBlock* block, BasicBlock* prevBlock,
                                 ValueMap& valueMap, bool isMainFunction) {
    // First handle PHI nodes
    handlePHINodes(block, prevBlock, valueMap);

    // Then handle other instructions
    for (auto* inst : *block) {
        if (isa<PHINode>(inst)) continue;  // Already handled

        // Skip terminator instructions
        if (inst->isTerminator()) continue;

        std::cerr << "[DEBUG] Evaluating instruction: "
                  << IRPrinter::toString(inst) << std::endl;

        Value* result = nullptr;

        if (auto* alloca = dyn_cast<AllocaInst>(inst)) {
            result = evaluateAllocaInst(alloca, valueMap);
        } else if (auto* binOp = dyn_cast<BinaryOperator>(inst)) {
            std::cerr << "[DEBUG] Found BinaryOperator" << std::endl;
            result = evaluateBinaryOp(binOp, valueMap);
            if (result) {
                std::cerr << "[DEBUG] BinaryOp evaluated successfully"
                          << std::endl;
            } else {
                std::cerr << "[DEBUG] BinaryOp evaluation failed" << std::endl;
            }
        } else if (auto* unOp = dyn_cast<UnaryOperator>(inst)) {
            result = evaluateUnaryOp(unOp, valueMap);
        } else if (auto* cmp = dyn_cast<CmpInst>(inst)) {
            std::cerr << "[DEBUG] Found CmpInst" << std::endl;
            result = evaluateCmpInst(cmp, valueMap);
            if (result) {
                std::cerr << "[DEBUG] CmpInst evaluated successfully"
                          << std::endl;
            } else {
                std::cerr << "[DEBUG] CmpInst evaluation failed" << std::endl;
            }
        } else if (auto* load = dyn_cast<LoadInst>(inst)) {
            result = evaluateLoadInst(load, valueMap);
        } else if (auto* store = dyn_cast<StoreInst>(inst)) {
            evaluateStoreInst(store, valueMap);
        } else if (auto* gep = dyn_cast<GetElementPtrInst>(inst)) {
            result = evaluateGEP(gep, valueMap);
        } else if (auto* call = dyn_cast<CallInst>(inst)) {
            result = evaluateCallInst(call, valueMap, isMainFunction);
        } else if (auto* cast = dyn_cast<CastInst>(inst)) {
            std::cerr << "[DEBUG] Found CastInst" << std::endl;
            result = evaluateCastInst(cast, valueMap);
            if (result) {
                std::cerr << "[DEBUG] CastInst evaluated successfully"
                          << std::endl;
            } else {
                std::cerr << "[DEBUG] CastInst evaluation failed" << std::endl;
            }
        }

        // Store the result if it was computed
        if (result) {
            if (valueMap.count(inst) && valueMap[inst] != result) {
                changedValues.insert(inst);
            }
            valueMap[inst] = result;

            // Debug output to understand what values we're storing
            if (isa<ConstantInt>(result)) {
                std::cerr << "[DEBUG] Stored ConstantInt for "
                          << inst->getName() << std::endl;
            } else if (isa<ConstantFP>(result)) {
                std::cerr << "[DEBUG] Stored ConstantFP for " << inst->getName()
                          << std::endl;
            } else {
                std::cerr << "[DEBUG] Stored non-constant value for "
                          << inst->getName() << std::endl;
            }
        }
    }
}

void ComptimePass::handlePHINodes(BasicBlock* block, BasicBlock* prevBlock,
                                  ValueMap& valueMap) {
    std::vector<PHINode*> phiNodes;

    // Collect all PHI nodes in this block
    for (auto* inst : *block) {
        if (auto* phi = dyn_cast<PHINode>(inst)) {
            phiNodes.push_back(phi);
        } else {
            break;  // PHI nodes are always at the beginning
        }
    }

    if (phiNodes.empty()) return;

    // If we don't know the previous block, we can't evaluate PHI nodes
    if (!prevBlock) {
        for (auto* phi : phiNodes) {
            valueMap.erase(phi);
        }
        return;
    }

    // Build dependency graph for PHI nodes
    std::unordered_map<PHINode*, std::vector<PHINode*>> dependencies;
    std::unordered_map<PHINode*, int> inDegree;

    for (auto* phi : phiNodes) {
        dependencies[phi] = {};
        inDegree[phi] = 0;
    }

    // Find dependencies between PHI nodes
    for (auto* phi : phiNodes) {
        Value* incomingVal = phi->getIncomingValueForBlock(prevBlock);
        if (incomingVal) {
            if (auto* depPhi = dyn_cast<PHINode>(incomingVal)) {
                // Check if depPhi is in the same block
                if (std::find(phiNodes.begin(), phiNodes.end(), depPhi) !=
                    phiNodes.end()) {
                    dependencies[depPhi].push_back(phi);
                    inDegree[phi]++;
                }
            }
        }
    }

    // Topological sort to handle non-cyclic PHIs first
    std::queue<PHINode*> queue;
    std::vector<PHINode*> processed;

    for (auto* phi : phiNodes) {
        if (inDegree[phi] == 0) {
            queue.push(phi);
        }
    }

    while (!queue.empty()) {
        PHINode* phi = queue.front();
        queue.pop();
        processed.push_back(phi);

        for (auto* dep : dependencies[phi]) {
            if (--inDegree[dep] == 0) {
                queue.push(dep);
            }
        }
    }

    // Handle non-cyclic PHI nodes
    for (auto* phi : processed) {
        evaluatePHINode(phi, prevBlock, valueMap);
    }

    // Handle cyclic PHI nodes (swap values using temporary storage)
    std::unordered_map<PHINode*, Value*> tempValues;
    for (auto* phi : phiNodes) {
        if (inDegree[phi] > 0) {  // Part of a cycle
            if (valueMap.count(phi)) {
                tempValues[phi] = valueMap[phi];
            }
        }
    }

    for (auto* phi : phiNodes) {
        if (inDegree[phi] > 0) {  // Part of a cycle
            Value* incomingVal = phi->getIncomingValueForBlock(prevBlock);
            if (incomingVal) {
                if (auto* srcPhi = dyn_cast<PHINode>(incomingVal)) {
                    if (tempValues.count(srcPhi)) {
                        valueMap[phi] = tempValues[srcPhi];
                        continue;
                    }
                }

                Value* val = getValueOrConstant(incomingVal, valueMap);
                if (val) {
                    valueMap[phi] = val;
                } else {
                    valueMap.erase(phi);
                }
            } else {
                valueMap.erase(phi);
            }
        }
    }
}

void ComptimePass::evaluatePHINode(PHINode* phi, BasicBlock* prevBlock,
                                   ValueMap& valueMap) {
    if (!prevBlock) {
        valueMap.erase(phi);
        return;
    }

    Value* incomingVal = phi->getIncomingValueForBlock(prevBlock);
    if (!incomingVal) {
        valueMap.erase(phi);
        return;
    }

    Value* val = getValueOrConstant(incomingVal, valueMap);
    if (val) {
        // Check for special case: incoming value is current PHI itself
        // (self-loop)
        if (val == phi && valueMap.count(phi)) {
            // Keep current value
            return;
        }
        valueMap[phi] = val;
    } else {
        valueMap.erase(phi);
    }
}

Value* ComptimePass::evaluateAllocaInst(AllocaInst* alloca,
                                        ValueMap& valueMap) {
    Type* allocatedType = alloca->getAllocatedType();
    Value* initialValue = createZeroInitializedConstant(allocatedType);
    valueMap[alloca] = initialValue;
    return initialValue;
}

Value* ComptimePass::evaluateBinaryOp(BinaryOperator* binOp,
                                      ValueMap& valueMap) {
    Value* lhs = getValueOrConstant(binOp->getOperand(0), valueMap);
    Value* rhs = getValueOrConstant(binOp->getOperand(1), valueMap);

    if (!lhs || !rhs) return nullptr;

    Opcode op = binOp->getOpcode();

    // Handle integer operations
    if (auto* lhsInt = dyn_cast<ConstantInt>(lhs)) {
        if (auto* rhsInt = dyn_cast<ConstantInt>(rhs)) {
            int32_t l = lhsInt->getSignedValue();
            int32_t r = rhsInt->getSignedValue();

            switch (op) {
                case Opcode::Add:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l + r);
                case Opcode::Sub:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l - r);
                case Opcode::Mul:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l * r);
                case Opcode::Div:
                    if (r != 0)
                        return ConstantInt::get(
                            cast<IntegerType>(lhsInt->getType()), l / r);
                    break;
                case Opcode::Rem:
                    if (r != 0)
                        return ConstantInt::get(
                            cast<IntegerType>(lhsInt->getType()), l % r);
                    break;
                case Opcode::And:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l & r);
                case Opcode::Or:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l | r);
                case Opcode::Xor:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l ^ r);
                case Opcode::LAnd:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l && r);
                case Opcode::LOr:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l || r);
                default:
                    break;
            }
        }
    }

    // Handle float operations
    if (auto* lhsFP = dyn_cast<ConstantFP>(lhs)) {
        if (auto* rhsFP = dyn_cast<ConstantFP>(rhs)) {
            float l = lhsFP->getValue();
            float r = rhsFP->getValue();

            switch (op) {
                case Opcode::FAdd:
                    return ConstantFP::get(cast<FloatType>(lhsFP->getType()),
                                           l + r);
                case Opcode::FSub:
                    return ConstantFP::get(cast<FloatType>(lhsFP->getType()),
                                           l - r);
                case Opcode::FMul:
                    return ConstantFP::get(cast<FloatType>(lhsFP->getType()),
                                           l * r);
                case Opcode::FDiv:
                    if (r != 0.0f)
                        return ConstantFP::get(
                            cast<FloatType>(lhsFP->getType()), l / r);
                    break;
                default:
                    break;
            }
        }
    }

    return nullptr;
}

Value* ComptimePass::evaluateUnaryOp(UnaryOperator* unOp, ValueMap& valueMap) {
    Value* operand = getValueOrConstant(unOp->getOperand(), valueMap);
    if (!operand) return nullptr;

    if (auto* intVal = dyn_cast<ConstantInt>(operand)) {
        int32_t val = intVal->getSignedValue();

        switch (unOp->getOpcode()) {
            case Opcode::UAdd:
                return intVal;
            case Opcode::USub:
                return ConstantInt::get(cast<IntegerType>(intVal->getType()),
                                        -val);
            case Opcode::Not:
                return ConstantInt::get(cast<IntegerType>(intVal->getType()),
                                        !val);
            default:
                break;
        }
    } else if (auto* fpVal = dyn_cast<ConstantFP>(operand)) {
        float val = fpVal->getValue();

        switch (unOp->getOpcode()) {
            case Opcode::UAdd:
                return fpVal;
            case Opcode::USub:
                return ConstantFP::get(cast<FloatType>(fpVal->getType()), -val);
            default:
                break;
        }
    }

    return nullptr;
}

Value* ComptimePass::evaluateCmpInst(CmpInst* cmp, ValueMap& valueMap) {
    Value* lhs = getValueOrConstant(cmp->getOperand(0), valueMap);
    Value* rhs = getValueOrConstant(cmp->getOperand(1), valueMap);

    if (!lhs || !rhs) {
        return nullptr;
    }

    bool result = false;
    bool wasEvaluated = false;
    if (isa<ConstantInt>(lhs) && isa<ConstantInt>(rhs)) {
        auto* lhsInt = dyn_cast<ConstantInt>(lhs);
        auto* rhsInt = dyn_cast<ConstantInt>(rhs);
        int32_t l = lhsInt->getSignedValue();
        int32_t r = rhsInt->getSignedValue();

        switch (cmp->getPredicate()) {
            case CmpInst::ICMP_EQ:
                result = (l == r);
                wasEvaluated = true;
                break;
            case CmpInst::ICMP_NE:
                result = (l != r);
                wasEvaluated = true;
                break;
            case CmpInst::ICMP_SLT:
                result = (l < r);
                wasEvaluated = true;
                break;
            case CmpInst::ICMP_SLE:
                result = (l <= r);
                wasEvaluated = true;
                break;
            case CmpInst::ICMP_SGT:
                result = (l > r);
                wasEvaluated = true;
                break;
            case CmpInst::ICMP_SGE:
                result = (l >= r);
                wasEvaluated = true;
                break;
            default:
                break;
        }
    } else if (isa<ConstantFP>(lhs) && isa<ConstantFP>(rhs)) {
        auto* lhsFP = dyn_cast<ConstantFP>(lhs);
        auto* rhsFP = dyn_cast<ConstantFP>(rhs);
        float l = lhsFP->getValue();
        float r = rhsFP->getValue();

        switch (cmp->getPredicate()) {
            case CmpInst::FCMP_OEQ:
                result = (l == r);
                wasEvaluated = true;
                break;
            case CmpInst::FCMP_ONE:
                result = (l != r);
                wasEvaluated = true;
                break;
            case CmpInst::FCMP_OLT:
                result = (l < r);
                wasEvaluated = true;
                break;
            case CmpInst::FCMP_OLE:
                result = (l <= r);
                wasEvaluated = true;
                break;
            case CmpInst::FCMP_OGT:
                result = (l > r);
                wasEvaluated = true;
                break;
            case CmpInst::FCMP_OGE:
                result = (l >= r);
                wasEvaluated = true;
                break;
            default:
                break;
        }
    }

    if (!wasEvaluated) {
        std::cerr << "[DEBUG CmpInst] Not evaluated: "
                  << IRPrinter::toString(cmp) << std::endl;
        return nullptr;
    }

    auto* ctx = cmp->getContext();
    return result ? ConstantInt::getTrue(ctx) : ConstantInt::getFalse(ctx);
}

Value* ComptimePass::evaluateLoadInst(LoadInst* load, ValueMap& valueMap) {
    Value* ptr = load->getPointerOperand();

    // If the pointer is in the value map, return its value
    if (valueMap.count(ptr)) {
        Value* v = valueMap[ptr];
        if (auto* gepConst = dyn_cast<ConstantGEP>(v)) {
            return gepConst->getElement();
        }
        return v;
    }

    return nullptr;
}

void ComptimePass::evaluateStoreInst(StoreInst* store, ValueMap& valueMap) {
    Value* value = getValueOrConstant(store->getValueOperand(), valueMap);
    Value* ptr = store->getPointerOperand();

    if (!value) return;

    if (valueMap.count(ptr)) {
        if (auto* gepConst = dyn_cast<ConstantGEP>(valueMap[ptr])) {
            gepConst->setElementValue(value);
        } else {
            valueMap[ptr] = value;
        }
        if (valueMap.count(store)) {
            changedValues.insert(store);
        }
        valueMap[store] = value;
    }
}

Value* ComptimePass::evaluateGEP(GetElementPtrInst* gep, ValueMap& valueMap) {
    Value* ptr = gep->getPointerOperand();

    if (!valueMap.count(ptr)) {
        return nullptr;
    }

    Value* baseVal = valueMap[ptr];

    // Collect indices as ConstantInt*
    std::vector<ConstantInt*> ciIndices;
    ciIndices.reserve(gep->getNumIndices());
    for (size_t i = 0; i < gep->getNumIndices(); ++i) {
        Value* idx = getValueOrConstant(gep->getIndex(i), valueMap);
        if (auto* ci = dyn_cast<ConstantInt>(idx)) {
            ciIndices.push_back(ci);
        } else {
            return nullptr;
        }
    }

    // Build a ConstantGEP chain
    if (auto* baseArr = dyn_cast<ConstantArray>(baseVal)) {
        return ConstantGEP::get(baseArr, ciIndices);
    }
    if (auto* baseGep = dyn_cast<ConstantGEP>(baseVal)) {
        return ConstantGEP::get(baseGep, ciIndices);
    }

    return nullptr;
}

Value* ComptimePass::evaluateCallInst(CallInst* call, ValueMap& valueMap,
                                      bool isMainFunction) {
    Function* func = call->getCalledFunction();

    if (!func || func->isDeclaration()) {
        // Declaration function - cannot evaluate at compile time
        if (isMainFunction) {
            // Remove global variables and PHI nodes that depend on this call
            std::vector<Value*> toErase;
            for (auto& [key, val] : valueMap) {
                if (isa<GlobalVariable>(key) || isa<PHINode>(key)) {
                    toErase.push_back(key);
                }
            }
            for (auto* key : toErase) {
                valueMap.erase(key);
            }
        }
        return nullptr;
    }

    // Collect arguments
    std::vector<Value*> args;
    for (size_t i = 0; i < call->getNumArgOperands(); ++i) {
        Value* arg = getValueOrConstant(call->getArgOperand(i), valueMap);
        if (!arg) return nullptr;
        args.push_back(arg);
    }

    // Recursively evaluate the function
    return evaluateFunction(func, args, false);
}

Value* ComptimePass::evaluateCastInst(CastInst* castInst, ValueMap& valueMap) {
    Value* operand = getValueOrConstant(castInst->getOperand(0), valueMap);
    if (!operand) return nullptr;

    Type* destType = castInst->getDestType();

    // Handle different cast operations based on opcode
    switch (castInst->getCastOpcode()) {
        case CastInst::SIToFP:  // Signed integer to float
            if (auto* intVal = dyn_cast<ConstantInt>(operand)) {
                return ConstantFP::get(cast<FloatType>(destType),
                                       (float)intVal->getSignedValue());
            }
            break;

        case CastInst::FPToSI:  // Float to signed integer
            if (auto* fpVal = dyn_cast<ConstantFP>(operand)) {
                return ConstantInt::get(cast<IntegerType>(destType),
                                        (int32_t)fpVal->getValue());
            }
            break;

        case CastInst::Trunc:  // Truncate integer
            if (auto* intVal = dyn_cast<ConstantInt>(operand)) {
                // Truncate to smaller bit width
                return ConstantInt::get(cast<IntegerType>(destType),
                                        intVal->getUnsignedValue());
            }
            break;

        case CastInst::ZExt:  // Zero extend
            if (auto* intVal = dyn_cast<ConstantInt>(operand)) {
                // Zero extend preserves the unsigned value
                return ConstantInt::get(cast<IntegerType>(destType),
                                        intVal->getUnsignedValue());
            }
            break;

        case CastInst::SExt:  // Sign extend
            if (auto* intVal = dyn_cast<ConstantInt>(operand)) {
                // Sign extend preserves the signed value
                return ConstantInt::get(cast<IntegerType>(destType),
                                        intVal->getSignedValue());
            }
            break;

        case CastInst::BitCast:  // Type cast
            // BitCast doesn't change the value, just the type interpretation
            if (isa<Constant>(operand)) {
                return operand;
            }
            break;

        case CastInst::PtrToInt:  // Pointer to integer
        case CastInst::IntToPtr:  // Integer to pointer
            // These are not compile-time computable in general
            break;

        default:
            break;
    }

    return nullptr;
}

size_t ComptimePass::eliminateComputedInstructions(Function* func) {
    std::vector<Instruction*> toRemove;

    // Collect instructions to remove
    for (auto* bb : func->getBasicBlocks()) {
        for (auto* inst : *bb) {
            if (globalValueMap.count(inst) && !changedValues.count(inst)) {
                Value* computedValue = globalValueMap[inst];

                if (isa<GetElementPtrInst>(inst) || isa<AllocaInst>(inst) ||
                    isa<BranchInst>(inst)) {
                    std::cerr << "[DEBUG] Skipping Inst: " << inst->getName()
                              << std::endl;
                    continue;
                }

                if (isa<ConstantArray>(computedValue) &&
                    !(isa<ConstantInt>(computedValue) ||
                      isa<ConstantFP>(computedValue))) {
                    std::cerr << "[DEBUG] Skipping ConstantArray value: "
                              << inst->getName() << " = "
                              << IRPrinter::toString(computedValue)
                              << std::endl;
                    continue;
                }

                toRemove.push_back(inst);
            }
        }
    }

    // Remove instructions and replace uses
    for (auto* inst : toRemove) {
        Value* replacement = globalValueMap[inst];
        if (replacement && !inst->getType()->isVoidType()) {
            inst->replaceAllUsesWith(replacement);
        }
        inst->eraseFromParent();
    }

    return toRemove.size();
}

void ComptimePass::initializeArrays(Module& module) {
    // Initialize global arrays
    for (auto* gv : module.globals()) {
        if (globalValueMap.count(gv)) {
            if (auto* arrayVal = dyn_cast<ConstantArray>(globalValueMap[gv])) {
                gv->setInitializer(arrayVal);
            }
        }
    }

    // Initialize local arrays in main function
    Function* mainFunc = module.getFunction("main");
    if (!mainFunc) return;

    BasicBlock* entryBlock = &mainFunc->getEntryBlock();
    std::vector<std::pair<AllocaInst*, ConstantArray*>> localArrays;

    // Find local arrays that need initialization
    for (auto* inst : *entryBlock) {
        if (auto* alloca = dyn_cast<AllocaInst>(inst)) {
            if (globalValueMap.count(alloca)) {
                if (auto* arrayVal =
                        dyn_cast<ConstantArray>(globalValueMap[alloca])) {
                    localArrays.push_back({alloca, arrayVal});
                }
            }
        }
    }

    // Initialize each local array
    for (auto& [alloca, arrayVal] : localArrays) {
        initializeLocalArray(mainFunc, alloca, arrayVal);
    }
}

void ComptimePass::initializeLocalArray(Function* mainFunc, AllocaInst* alloca,
                                        ConstantArray* arrayValue) {
    IRBuilder builder(mainFunc->getContext());
    BasicBlock* entryBlock = &mainFunc->getEntryBlock();

    // Find insertion point after all allocas
    BasicBlock::iterator insertPoint = entryBlock->begin();
    while (insertPoint != entryBlock->end() && isa<AllocaInst>(*insertPoint)) {
        ++insertPoint;
    }

    int totalElements = getTotalElements(arrayValue);
    int nonZeroCount = countNonZeroElements(arrayValue);

    bool useFullInit =
        (nonZeroCount > totalElements * 0.8) || (totalElements < 10);

    auto it = alloca->getIterator();
    while (isa<AllocaInst>(*it) && it != entryBlock->end()) ++it;
    if (useFullInit) {
        // Initialize all elements with GEP + store
        std::vector<std::pair<int, Constant*>> allIndices;
        collectFlatIndices(arrayValue, allIndices);

        builder.setInsertPoint(*it);
        for (int i = 0; i < totalElements; ++i) {
            auto* idx = builder.getInt32(i);
            auto* gep = builder.createGEP(alloca, idx);

            Constant* val = allIndices[i].second;

            if (!val) {
                val = createZeroInitializedConstant(
                    arrayValue->getType()->getSingleElementType());
            }

            builder.createStore(val, gep);
        }
    } else {
        static int array_init_cnt = 0;
        auto oldBB = alloca->getParent();
        BasicBlock* loopCond = BasicBlock::Create(
            mainFunc->getContext(),
            "comptime.array.cond." + std::to_string(array_init_cnt));
        BasicBlock* loopBody = BasicBlock::Create(
            mainFunc->getContext(),
            "comptime.array.body." + std::to_string(array_init_cnt));

        auto newBB = oldBB->split(it, {loopCond, loopBody});
        oldBB->push_back(BranchInst::Create(loopCond));

        // Loop cond
        builder.setInsertPoint(loopCond);
        auto* phi = builder.createPHI(
            builder.getInt32Type(),
            "comptime.array.i." + std::to_string(array_init_cnt));
        phi->addIncoming(builder.getInt32(0), oldBB);

        auto* cond =
            builder.createICmpSLT(phi, builder.getInt32(totalElements));
        builder.createCondBr(cond, loopBody, newBB);

        // Loop body
        builder.setInsertPoint(loopBody);
        auto* gep = builder.createGEP(alloca, phi);
        Type* elemType = arrayValue->getType()->getSingleElementType();
        Constant* zeroVal = createZeroInitializedConstant(elemType);
        builder.createStore(zeroVal, gep);

        auto* nextIdx = builder.createAdd(phi, builder.getInt32(1));
        phi->addIncoming(nextIdx, loopBody);
        builder.createBr(loopCond);

        std::vector<std::pair<int, Constant*>> nonZeroIndices;
        collectFlatIndices(arrayValue, nonZeroIndices, true);
        std::reverse(nonZeroIndices.begin(), nonZeroIndices.end());

        for (auto& [index, constant] : nonZeroIndices) {
            auto* idx = builder.getInt32(index);
            auto* gep =
                GetElementPtrInst::Create(alloca->getType(), alloca, {idx});
            newBB->push_front(StoreInst::Create(constant, gep));
            newBB->push_front(gep);
        }
    }
}

Constant* ComptimePass::createZeroInitializedConstant(Type* type) {
    if (auto* arrayType = dyn_cast<ArrayType>(type)) {
        std::vector<Constant*> elements;
        for (size_t i = 0; i < arrayType->getNumElements(); ++i) {
            elements.push_back(
                createZeroInitializedConstant(arrayType->getElementType()));
        }
        return ConstantArray::get(arrayType, elements);
    } else if (auto* intType = dyn_cast<IntegerType>(type)) {
        return ConstantInt::get(intType, 0);
    } else if (auto* floatType = dyn_cast<FloatType>(type)) {
        return ConstantFP::get(floatType, 0.0f);
    }

    return nullptr;
}

Value* ComptimePass::getFromNestedArray(Value* array,
                                        const std::vector<Value*>& indices) {
    if (indices.empty()) return array;

    auto* constArray = dyn_cast<ConstantArray>(array);
    if (!constArray) return array;

    auto* firstIdx = dyn_cast<ConstantInt>(indices[0]);
    if (!firstIdx) return nullptr;

    uint32_t idx = firstIdx->getUnsignedValue();
    if (idx >= constArray->getNumElements()) return nullptr;

    Value* element = constArray->getElement(idx);

    if (indices.size() > 1) {
        std::vector<Value*> restIndices(indices.begin() + 1, indices.end());
        return getFromNestedArray(element, restIndices);
    }

    return element;
}

int ComptimePass::countNonZeroElements(Constant* constant) {
    if (auto* constArray = dyn_cast<ConstantArray>(constant)) {
        int count = 0;
        for (size_t i = 0; i < constArray->getNumElements(); ++i) {
            count += countNonZeroElements(constArray->getElement(i));
        }
        return count;
    } else if (auto* constInt = dyn_cast<ConstantInt>(constant)) {
        return constInt->getValue() != 0 ? 1 : 0;
    } else if (auto* constFP = dyn_cast<ConstantFP>(constant)) {
        return constFP->getValue() != 0.0f ? 1 : 0;
    }
    return 0;
}

int ComptimePass::getTotalElements(Constant* constant) {
    if (auto* type = dyn_cast<ArrayType>(constant->getType())) {
        return type->getSizeInBytes() /
               type->getSingleElementType()->getSizeInBytes();
    }
    return 1;
}

void ComptimePass::collectFlatIndices(
    Constant* constant, std::vector<std::pair<int, Constant*>>& indices,
    bool onlyNonZero, int baseIndex) {
    if (isa<ArrayType>(constant->getType())) {
        auto* constArray = dyn_cast<ConstantArray>(constant);
        int subSize = getTotalElements(constArray->getElement(0));
        for (size_t i = 0; i < constArray->getNumElements(); ++i) {
            collectFlatIndices(constArray->getElement(i), indices, onlyNonZero,
                               baseIndex + i * subSize);
        }
    } else {
        bool isZero = false;
        if (auto* constInt = dyn_cast<ConstantInt>(constant)) {
            isZero = (constInt->getValue() == 0);
        } else if (auto* constFP = dyn_cast<ConstantFP>(constant)) {
            isZero = (constFP->getValue() == 0.0f);
        }

        if (!onlyNonZero || !isZero) {
            indices.push_back({baseIndex, constant});
        }
    }
}

Value* ComptimePass::getValueOrConstant(Value* v, ValueMap& valueMap) {
    if (isa<Constant>(v)) return v;
    if (valueMap.count(v)) return valueMap[v];
    return nullptr;
}

BasicBlock* ComptimePass::getPostImmediateDominator(BasicBlock* block) {
    if (!analysisManager || !block->getParent()) {
        return nullptr;
    }

    // Use the existing PostDominanceInfo
    auto* postDomInfo = analysisManager->getAnalysis<PostDominanceInfo>(
        "PostDominanceAnalysis", *block->getParent());
    if (!postDomInfo) {
        return nullptr;
    }

    return postDomInfo->getImmediateDominator(block);
}

REGISTER_PASS(ComptimePass, "comptime");

}  // namespace midend