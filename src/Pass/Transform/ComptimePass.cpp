#include "Pass/Transform/ComptimePass.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <queue>

// Debug output macro - only outputs when A_OUT_DEBUG is defined
#ifdef A_OUT_DEBUG
#define DEBUG_OUT() std::cerr
#else
#define DEBUG_OUT() \
    if constexpr (false) std::cerr
#endif

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
    runtimeValues.clear();
    comptimeInsts.clear();
    isPropagation = true;

    bool changed = false;

    Function* mainFunc = module.getFunction("main");
    if (mainFunc) {
        initializeGlobalValueMap(module);

        // Pass 1: Propagation - identify compile-time and runtime instructions
        evaluateFunction(mainFunc, {}, true, nullptr, globalValueMap, {});

        // Determine compile-time instruction set (value map - runtime set)
        for (auto& [val, result] : globalValueMap) {
            if (auto* inst = dyn_cast<Instruction>(val)) {
                if (!runtimeValues.count(inst)) {
                    comptimeInsts.insert(inst);
                    DEBUG_OUT() << "[DEBUG] Added to compile-time: "
                                << IRPrinter::toString(inst);
                }
            }
        }

        // Pass 2: Computation - compute actual values with fresh value map
        globalValueMap.clear();
        runtimeValues.clear();
        initializeGlobalValueMap(module);
        isPropagation = false;
        evaluateFunction(mainFunc, {}, true, &comptimeInsts, globalValueMap,
                         {});

        // Step 3: Eliminate computed instructions
        changed |= eliminateComputedInstructions(mainFunc) > 0;

        // Step 4: Initialize arrays with computed values
        changed |= initializeValues(module);
    }

    return changed;
}

static ConstantArray* flattenConstantArray(ConstantArray* nestedArray) {
    if (!nestedArray) return nullptr;

    Type* arrayType = nestedArray->getType();
    Type* baseType = arrayType->getBaseElementType();
    size_t totalElements =
        arrayType->getSizeInBytes() / baseType->getSizeInBytes();

    std::vector<Constant*> flatElements;
    flatElements.reserve(totalElements);

    auto createZero = [&]() -> Constant* {
        if (auto* intType = dyn_cast<IntegerType>(baseType)) {
            return ConstantInt::get(intType, 0);
        } else if (auto* floatType = dyn_cast<FloatType>(baseType)) {
            return ConstantFP::get(floatType, 0.0f);
        }
        return nullptr;
    };
    std::function<void(Constant*, ArrayType*)> flattenWithPadding =
        [&](Constant* c, ArrayType* expectedType) {
            if (!c) {
                size_t numElements =
                    expectedType->getSizeInBytes() / baseType->getSizeInBytes();
                for (size_t i = 0; i < numElements; ++i) {
                    flatElements.push_back(createZero());
                }
                return;
            }

            if (auto* innerArray = dyn_cast<ConstantArray>(c)) {
                size_t expectedElements = expectedType->getNumElements();
                auto* elemType = expectedType->getElementType();

                for (size_t i = 0; i < expectedElements; ++i) {
                    if (i < innerArray->getNumElements()) {
                        if (auto* nextArrayType =
                                dyn_cast<ArrayType>(elemType)) {
                            flattenWithPadding(innerArray->getElement(i),
                                               nextArrayType);
                        } else {
                            flatElements.push_back(innerArray->getElement(i));
                        }
                    } else {
                        if (auto* nextArrayType =
                                dyn_cast<ArrayType>(elemType)) {
                            flattenWithPadding(nullptr, nextArrayType);
                        } else {
                            flatElements.push_back(createZero());
                        }
                    }
                }
            } else {
                flatElements.push_back(c);
            }
        };

    flattenWithPadding(nestedArray, dyn_cast<ArrayType>(arrayType));

    auto* flatArrayType = ArrayType::get(baseType, totalElements);
    return ConstantArray::get(flatArrayType, flatElements);
}

static Constant* unflattenConstantArray(ConstantArray* flatArray,
                                        Type* targetType, size_t& index) {
    std::function<bool(Constant*)> isZero = [&isZero](Constant* c) -> bool {
        if (!c) return true;
        if (auto* intConst = dyn_cast<ConstantInt>(c)) {
            return intConst->getValue() == 0;
        } else if (auto* fpConst = dyn_cast<ConstantFP>(c)) {
            return fpConst->getValue() == 0.0f;
        } else if (auto* arrayConst = dyn_cast<ConstantArray>(c)) {
            for (size_t i = 0; i < arrayConst->getNumElements(); ++i) {
                if (!isZero(arrayConst->getElement(i))) {
                    return false;
                }
            }
            return true;
        }
        return false;
    };

    std::function<Constant*(ArrayType*, const std::vector<Constant*>&)>
        compactArray =
            [&isZero, &compactArray](
                ArrayType* arrayType,
                const std::vector<Constant*>& elements) -> Constant* {
        int lastNonZero = -1;
        for (int i = elements.size() - 1; i >= 0; --i) {
            if (!isZero(elements[i])) {
                lastNonZero = i;
                break;
            }
        }

        if (lastNonZero == -1) {
            std::vector<Constant*> minimal;
            auto* elemType = arrayType->getElementType();
            if (auto* innerArrayType = dyn_cast<ArrayType>(elemType)) {
                minimal.push_back(ConstantArray::get(innerArrayType, {}));
            }
            return ConstantArray::get(arrayType, minimal);
        }

        std::vector<Constant*> compacted(elements.begin(),
                                         elements.begin() + lastNonZero + 1);

        auto* elemType = arrayType->getElementType();
        if (auto* innerArrayType = dyn_cast<ArrayType>(elemType)) {
            for (size_t i = 0; i < compacted.size(); ++i) {
                if (auto* innerArray = dyn_cast<ConstantArray>(compacted[i])) {
                    std::vector<Constant*> innerElements;
                    for (size_t j = 0; j < innerArray->getNumElements(); ++j) {
                        innerElements.push_back(innerArray->getElement(j));
                    }
                    compacted[i] = compactArray(innerArrayType, innerElements);
                }
            }
        }

        return ConstantArray::get(arrayType, compacted);
    };

    if (auto* arrayType = dyn_cast<ArrayType>(targetType)) {
        std::vector<Constant*> elements;
        for (size_t i = 0; i < arrayType->getNumElements(); ++i) {
            elements.push_back(unflattenConstantArray(
                flatArray, arrayType->getElementType(), index));
        }
        return compactArray(arrayType, elements);
    } else {
        if (index < flatArray->getNumElements()) {
            return flatArray->getElement(index++);
        }
        return nullptr;
    }
}

void ComptimePass::initializeGlobalValueMap(Module& module) {
    for (auto* gv : module.globals()) {
        if (gv->hasInitializer()) {
            auto* initializer = gv->getInitializer();
            if (auto* constArray = dyn_cast<ConstantArray>(initializer)) {
                globalValueMap[gv] = flattenConstantArray(constArray);
            } else {
                globalValueMap[gv] = initializer;
            }
        } else {
            Type* valueType = gv->getValueType();
            globalValueMap[gv] = createZeroInitializedConstant(valueType);
        }
    }
}

std::pair<Value*, bool> ComptimePass::evaluateFunction(
    Function* func, const std::vector<Value*>& args, bool isMainFunction,
    const ComptimeSet* comptimeSet, ValueMap& upperValueMap,
    const std::vector<Value*>& argsRef) {
    if (func->isDeclaration()) {
        return std::make_pair(nullptr, false);
    }

    // Use appropriate value map based on mode and function
    ValueMap localValueMap;
    ValueMap& valueMap = isMainFunction ? globalValueMap : localValueMap;
    bool runtime = false;

    // Bind function arguments
    for (size_t i = 0; i < args.size() && i < func->getNumArgs(); ++i) {
        valueMap[func->getArg(i)] = args[i];
        DEBUG_OUT() << "[DEBUG] init func arg " << func->getArg(i)->getName()
                    << " = " << IRPrinter::toString(args[i]) << std::endl;
    }

    BasicBlock* currentBlock = &func->getEntryBlock();
    BasicBlock* prevBlock = nullptr;
    Value* returnValue = nullptr;

    // Simulate execution through the function
    while (currentBlock) {
        // Evaluate all instructions in the current block
        bool blockHasRuntime = evaluateBlock(currentBlock, prevBlock, valueMap,
                                             isMainFunction, comptimeSet);
        runtime = runtime || blockHasRuntime;

        auto* terminator = currentBlock->getTerminator();
        if (!terminator) break;

        if (auto* ret = dyn_cast<ReturnInst>(terminator)) {
            // Handle return instruction
            Value* retVal = ret->getReturnValue();
            if (retVal) {
                returnValue = getValueOrConstant(retVal, valueMap);
            } else {
                returnValue = UndefValue::get(func->getReturnType());
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
                    // Runtime condition - perform runtime propagation
                    BasicBlock* postDom =
                        getPostImmediateDominator(currentBlock);
                    runtime = true;
                    performRuntimePropagation(currentBlock, postDom, valueMap);
                    currentBlock = postDom;
                    prevBlock = nullptr;  // Mark as non-compile-time state
                }
            } else {
                currentBlock = br->getTargetBB();
            }
        } else {
            break;
        }
    }

    for (size_t i = 0; i < args.size() && i < func->getNumArgs(); ++i) {
        if (runtimeValues.count(func->getArg(i))) {
            markAsRuntime(argsRef[i], upperValueMap);
        }
    }

    DEBUG_OUT() << "[DEBUG] Function " << func->getName()
                << " evaluated with return value: "
                << (returnValue ? IRPrinter::toString(returnValue) : "nullptr")
                << std::endl;

    return std::make_pair(returnValue, returnValue != nullptr && !runtime);
}

bool ComptimePass::evaluateBlock(BasicBlock* block, BasicBlock* prevBlock,
                                 ValueMap& valueMap, bool isMainFunction,
                                 const ComptimeSet* comptimeSet) {
    bool runtime = false;
    // First handle PHI nodes
    handlePHINodes(block, prevBlock, valueMap);

    // Then handle other instructions
    for (auto* inst : *block) {
        if (isa<PHINode>(inst)) continue;
        if (inst->isTerminator()) continue;

        bool skipSideEffect = false;

        // In computation mode, check if instruction should be processed
        if (comptimeSet && !comptimeSet->count(inst)) {
            if (isa<CallInst>(inst) || isa<StoreInst>(inst)) {
                DEBUG_OUT()
                    << "[DEBUG Compute] Skipping non-comptime instruction: "
                    << IRPrinter::toString(inst) << std::endl;
                skipSideEffect = true;
            }
        }

        DEBUG_OUT() << (isPropagation ? "[DEBUG Propagate] "
                                      : "[DEBUG Compute] ")
                    << "Evaluating instruction: " << IRPrinter::toString(inst);

        Value* result = nullptr;

        if (auto* alloca = dyn_cast<AllocaInst>(inst)) {
            result = evaluateAllocaInst(alloca, valueMap);
        } else if (auto* binOp = dyn_cast<BinaryOperator>(inst)) {
            result = evaluateBinaryOp(binOp, valueMap);
        } else if (auto* unOp = dyn_cast<UnaryOperator>(inst)) {
            result = evaluateUnaryOp(unOp, valueMap);
        } else if (auto* cmp = dyn_cast<CmpInst>(inst)) {
            result = evaluateCmpInst(cmp, valueMap);
        } else if (auto* load = dyn_cast<LoadInst>(inst)) {
            result = evaluateLoadInst(load, valueMap);
        } else if (auto* store = dyn_cast<StoreInst>(inst)) {
            result = evaluateStoreInst(store, valueMap, skipSideEffect);
        } else if (auto* gep = dyn_cast<GetElementPtrInst>(inst)) {
            result = evaluateGEP(gep, valueMap);
        } else if (auto* call = dyn_cast<CallInst>(inst)) {
            auto [res, comptime] = evaluateCallInst(
                call, valueMap, isMainFunction, skipSideEffect);
            result = res;
            if (!comptime || !res) {
                markAsRuntime(inst, valueMap);
                runtime = true;
            }
        } else if (auto* cast = dyn_cast<CastInst>(inst)) {
            result = evaluateCastInst(cast, valueMap);
        } else {
            throw std::runtime_error(
                "Unknown instruction type in block evaluation: " +
                IRPrinter::toString(inst));
        }
        DEBUG_OUT() << "\t= " << IRPrinter::toString(result) << std::endl;

        updateValueMap(inst, result, valueMap);
    }
    return runtime;
}

bool ComptimePass::updateValueMap(Value* key, Value* result,
                                  ValueMap& valueMap) {
#ifdef A_OUT_DEBUG
    if (!key) {
        throw std::runtime_error("Null key in value map update");
    }
#endif
    bool runtime = false;
    if (result) {
        if (valueMap.count(key)) {
            if (valueMap[key] != result) {
                DEBUG_OUT() << "[DEBUG] Value changed for " << key->getName()
                            << ", marking as runtime" << std::endl;
                markAsRuntime(key, valueMap, true);
                runtime = true;
            }
        }
        valueMap[key] = result;
    } else if (valueMap.find(key) != valueMap.end()) {
        if (!isPropagation) {
            if (auto gv = dyn_cast<GlobalVariable>(key);
                gv && !gv->getValueType()->isArrayType()) {
                gv->setInitializer(valueMap[key]);
            }
        }
        valueMap.erase(key);
        markAsRuntime(key, valueMap);
        runtime = true;
    }
    return runtime;
}

void ComptimePass::handlePHINodes(BasicBlock* block, BasicBlock* prevBlock,
                                  ValueMap& valueMap) {
    std::vector<PHINode*> phiNodes;

    for (auto* inst : *block) {
        if (auto* phi = dyn_cast<PHINode>(inst)) {
            phiNodes.push_back(phi);
        } else {
            break;
        }
    }

    if (phiNodes.empty()) return;

    if (!prevBlock) {
        for (auto* phi : phiNodes) {
            updateValueMap(phi, nullptr, valueMap);
        }
    }

    std::unordered_map<PHINode*, std::vector<PHINode*>> dependencies;
    std::unordered_map<PHINode*, int> inDegree;
    std::unordered_map<PHINode*, Value*> tempValues;
    std::queue<PHINode*> queue;

    // Build dependency graph for PHI nodes
    for (auto* phi : phiNodes) {
        dependencies[phi] = {};
        inDegree[phi] = 0;
    }

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
    for (auto* phi : phiNodes) {
        if (inDegree[phi] == 0) {
            queue.push(phi);
        }
    }

    while (!queue.empty()) {
        PHINode* phi = queue.front();
        queue.pop();
        updateValueMap(phi, evaluatePHINode(phi, prevBlock, valueMap),
                       valueMap);

        for (auto* dep : dependencies[phi]) {
            if (--inDegree[dep] == 0) {
                queue.push(dep);
            }
        }
    }

    for (auto* phi : phiNodes) {
        if (inDegree[phi] > 0) {
            if (valueMap.count(phi)) {
                tempValues[phi] = valueMap[phi];
            }
        }
    }

    for (auto* phi : phiNodes) {
        if (inDegree[phi] > 0) {
            Value* incomingVal = phi->getIncomingValueForBlock(prevBlock);
            Value* result = nullptr;
            if (incomingVal) {
                if (auto* srcPhi = dyn_cast<PHINode>(incomingVal)) {
                    if (tempValues.count(srcPhi)) {
                        result = tempValues[phi];
                    } else {
                        throw std::runtime_error(
                            "PHI node dependency not found: " +
                            IRPrinter::toString(srcPhi));
                    }
                } else {
                    result = getValueOrConstant(incomingVal, valueMap);
                }
            }
            updateValueMap(phi, result, valueMap);
        }
    }
}

Value* ComptimePass::evaluatePHINode(PHINode* phi, BasicBlock* prevBlock,
                                     ValueMap& valueMap) {
    if (!prevBlock) {
        return nullptr;
    }

    Value* incomingVal = phi->getIncomingValueForBlock(prevBlock);
    if (!incomingVal) {
        return nullptr;
    }

    return getValueOrConstant(incomingVal, valueMap);
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
                case Opcode::Shl:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l << r);
                case Opcode::Shr:
                    return ConstantInt::get(
                        cast<IntegerType>(lhsInt->getType()), l >> r);
                default:
                    throw std::runtime_error(
                        "Unsupported opcode: " +
                        std::to_string(static_cast<int>(op)));
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
        return nullptr;
    }

    auto* ctx = cmp->getContext();
    return result ? ConstantInt::getTrue(ctx) : ConstantInt::getFalse(ctx);
}

Value* ComptimePass::evaluateLoadInst(LoadInst* load, ValueMap& valueMap) {
    Value* ptr = load->getPointerOperand();

    auto& localValueMap = isa<GlobalVariable>(ptr) ? globalValueMap : valueMap;

    if (localValueMap.count(ptr)) {
        Value* v = localValueMap[ptr];
        if (auto* gepConst = dyn_cast<ConstantGEP>(v)) {
            return gepConst->getElement();
        }
        return v;
    }
    return nullptr;
}

Value* ComptimePass::evaluateStoreInst(StoreInst* store, ValueMap& valueMap,
                                       bool skipSideEffect) {
    Value* value = getValueOrConstant(store->getValueOperand(), valueMap);
    Value* ptr = store->getPointerOperand();

    if (!value || skipSideEffect) {
        if (updateValueMap(ptr, nullptr, valueMap)) {
            markAsRuntime(store, valueMap);
        }
        DEBUG_OUT() << "\tside effect skipped or value not found" << std::endl;
        return nullptr;
    }

    if (isa<GlobalVariable>(ptr) && globalValueMap.count(ptr)) {
        globalValueMap[ptr] = value;
        DEBUG_OUT() << "\tupdated global value for " << ptr->getName() << " = "
                    << IRPrinter::toString(value) << std::endl;
    } else if (valueMap.count(ptr)) {
        if (auto* gepConst = dyn_cast<ConstantGEP>(valueMap[ptr])) {
            gepConst->setElementValue(value);
        } else {
            valueMap[ptr] = value;
        }
        DEBUG_OUT() << "\tupdated value for " << ptr->getName() << " = "
                    << IRPrinter::toString(value) << std::endl;
    } else {
        markAsRuntime(store, valueMap);
    }
    return store;
}

Value* ComptimePass::evaluateGEP(GetElementPtrInst* gep, ValueMap& valueMap) {
    Value* ptr = gep->getPointerOperand();

    if (isa<Argument>(ptr)) {
        ptr = valueMap[ptr];
    }

    auto& localValueMap = isa<GlobalVariable>(ptr) ? globalValueMap : valueMap;

    if (!localValueMap.count(ptr)) {
        return nullptr;
    }

    Value* baseVal = localValueMap[ptr];

    // Collect indices as ConstantInt*
    std::vector<ConstantInt*> ciIndices;
    ciIndices.reserve(gep->getNumIndices());
    for (size_t i = 0; i < gep->getNumIndices(); ++i) {
        Value* idx = getValueOrConstant(gep->getIndex(i), localValueMap);
        if (auto* ci = dyn_cast<ConstantInt>(idx)) {
            ciIndices.push_back(ci);
        } else {
            return nullptr;
        }
    }

    ConstantGEP* resultGEP = nullptr;
    if (auto* baseArr = dyn_cast<ConstantArray>(baseVal)) {
        Type* indexType = gep->getSourceElementType();
        resultGEP = ConstantGEP::get(baseArr, indexType, ciIndices);
    }
    if (auto* baseGep = dyn_cast<ConstantGEP>(baseVal)) {
        resultGEP = ConstantGEP::get(baseGep, ciIndices);
    }
    if (resultGEP && !resultGEP->getArrayPointer())
        resultGEP->setArrayPointer(gep->getBasePointer());

    return resultGEP;
}

std::pair<Value*, bool> ComptimePass::evaluateCallInst(CallInst* call,
                                                       ValueMap& valueMap,
                                                       bool isMainFunction,
                                                       bool skipSideEffect) {
    Function* func = call->getCalledFunction();

    if (!func || func->isDeclaration()) {
        if (isMainFunction) {
            DEBUG_OUT() << "Invalidating arrays from call: " << call->getName()
                        << std::endl;
            invalidateValuesFromCall(call, valueMap);
        }
        markAsRuntime(call, valueMap);
        return std::make_pair(nullptr, false);
    }

    std::vector<Value*> args;
    std::vector<Value*> argsRef;
    bool allArgsCompileTime = true;
    for (size_t i = 0; i < call->getNumArgOperands(); ++i) {
        if (runtimeValues.count(call->getArgOperand(i))) {
            allArgsCompileTime = false;
            break;
        }
        Value* arg = getValueOrConstant(call->getArgOperand(i), valueMap);
        if (!arg) {
            allArgsCompileTime = false;
            break;
        }
        args.push_back(arg);
        argsRef.push_back(call->getArgOperand(i));
    }

    if (!allArgsCompileTime) {
        // Non-compile-time call
        if (isMainFunction) {
            DEBUG_OUT() << "Invalidating arrays from call: " << call->getName()
                        << std::endl;
            invalidateValuesFromCall(call, valueMap);
        }
        return std::make_pair(nullptr, false);
    }
    if (skipSideEffect) return std::make_pair(nullptr, false);

    DEBUG_OUT() << "[DEBUG] Evaluating call: " << func->getName()
                << " with args: ";
    for (auto* arg : args) {
        DEBUG_OUT() << IRPrinter::toString(arg) << " ";
    }
    DEBUG_OUT() << std::endl;

    return evaluateFunction(func, args, false, nullptr, valueMap, argsRef);
}

Value* ComptimePass::evaluateCastInst(CastInst* castInst, ValueMap& valueMap) {
    Value* operand = getValueOrConstant(castInst->getOperand(0), valueMap);
    if (!operand) return nullptr;

    Type* destType = castInst->getDestType();

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
        case CastInst::ZExt:
            if (auto* intVal = dyn_cast<ConstantInt>(operand)) {
                return ConstantInt::get(cast<IntegerType>(destType),
                                        intVal->getUnsignedValue());
            }
            break;
        default:
            break;
    }

    return nullptr;
}

size_t ComptimePass::eliminateComputedInstructions(Function*) {
    std::vector<Instruction*> toRemove;

    // Collect instructions to remove - must be compile-time and not runtime
    for (auto [value, computedValue] : globalValueMap) {
        if (runtimeValues.find(value) != runtimeValues.end()) continue;

        if (auto inst = dyn_cast<Instruction>(value)) {
            if (isa<GetElementPtrInst>(inst) || isa<AllocaInst>(inst) ||
                isa<BranchInst>(inst)) {
                DEBUG_OUT()
                    << "[DEBUG] Skipping Inst: " << IRPrinter::toString(inst);
                continue;
            }

            // Skip non-scalar constant values
            if (isa<ConstantArray>(computedValue) &&
                !(isa<ConstantInt>(computedValue) ||
                  isa<ConstantFP>(computedValue))) {
                DEBUG_OUT() << "[DEBUG] Skipping ConstantArray value: "
                            << inst->getName() << " = "
                            << IRPrinter::toString(computedValue) << std::endl;
                continue;
            }

            DEBUG_OUT() << "[DEBUG] Eliminating Inst: "
                        << IRPrinter::toString(inst);

            toRemove.push_back(inst);
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

void setGlobalInitializer(GlobalVariable* gv, Value* value) {
    if (auto* flatArray = dyn_cast<ConstantArray>(value)) {
        Type* originalType = gv->getValueType();
        if (originalType->isArrayType()) {
            size_t index = 0;
            auto* nestedArray =
                unflattenConstantArray(flatArray, originalType, index);
            gv->setInitializer(nestedArray);
        } else {
            gv->setInitializer(value);
        }
    } else {
        gv->setInitializer(value);
    }
}

bool ComptimePass::initializeValues(Module& module) {
    // Initialize global values
    for (auto* gv : module.globals()) {
        if (globalValueMap.count(gv)) {
            auto* value = globalValueMap[gv];
            setGlobalInitializer(gv, value);
        }
    }

    // Initialize local arrays in main function
    Function* mainFunc = module.getFunction("main");
    if (!mainFunc) return false;
    BasicBlock* entryBlock = &mainFunc->getEntryBlock();
    std::vector<std::pair<AllocaInst*, Value*>> localValues;

    // Find local arrays that need initialization
    for (auto* inst : *entryBlock) {
        if (auto* alloca = dyn_cast<AllocaInst>(inst)) {
            if (globalValueMap.count(alloca)) {
                localValues.push_back({alloca, globalValueMap[alloca]});
            }
        }
    }
    std::reverse(localValues.begin(), localValues.end());

    for (auto& [alloca, value] : localValues) {
        if (auto* arrayValue = dyn_cast<ConstantArray>(value)) {
            initializeLocalArray(mainFunc, alloca, arrayValue);
        } else {
            IRBuilder builder(mainFunc->getContext());
            BasicBlock* entryBlock = &mainFunc->getEntryBlock();

            BasicBlock::iterator insertPoint = entryBlock->begin();
            while (insertPoint != entryBlock->end() &&
                   isa<AllocaInst>(*insertPoint)) {
                ++insertPoint;
            }

            builder.setInsertPoint(*insertPoint);
            builder.createStore(value, alloca);
        }
    }

    return !localValues.empty();
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

    Type* elemType = arrayValue->getType()->getBaseElementType();
    Type* flattenedArrayType = ArrayType::get(elemType, totalElements);

    auto it = alloca->getIterator();
    while (isa<AllocaInst>(*it) && it != entryBlock->end()) ++it;

    if (useFullInit) {
        // Initialize all elements with GEP + store
        std::vector<std::pair<int, Constant*>> allIndices;
        collectFlatIndices(arrayValue, allIndices);

        builder.setInsertPoint(*it);
        for (int i = 0; i < totalElements; ++i) {
            auto* idx = builder.getInt32(i);
            auto* gep =
                GetElementPtrInst::Create(flattenedArrayType, alloca, {idx});

            Constant* val = allIndices[i].second;

            if (!val) {
                val = createZeroInitializedConstant(
                    arrayValue->getType()->getSingleElementType());
            }

            builder.insert(gep);
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
        auto* gep =
            GetElementPtrInst::Create(flattenedArrayType, alloca, {phi});
        Constant* zeroVal = createZeroInitializedConstant(elemType);
        builder.insert(gep);
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
                GetElementPtrInst::Create(flattenedArrayType, alloca, {idx});
            newBB->push_front(StoreInst::Create(constant, gep));
            newBB->push_front(gep);
        }
        array_init_cnt++;
    }
}

Constant* ComptimePass::createZeroInitializedConstant(Type* type) {
    if (auto* arrayType = dyn_cast<ArrayType>(type)) {
        Type* baseElemType = arrayType->getBaseElementType();
        size_t totalElements =
            arrayType->getSizeInBytes() / baseElemType->getSizeInBytes();

        std::vector<Constant*> elements;
        elements.reserve(totalElements);

        for (size_t i = 0; i < totalElements; ++i) {
            if (auto* intType = dyn_cast<IntegerType>(baseElemType)) {
                elements.push_back(ConstantInt::get(intType, 0));
            } else if (auto* floatType = dyn_cast<FloatType>(baseElemType)) {
                elements.push_back(ConstantFP::get(floatType, 0.0f));
            } else {
                elements.push_back(nullptr);
            }
        }

        auto* flatArrayType = ArrayType::get(baseElemType, totalElements);
        return ConstantArray::get(flatArrayType, elements);
    } else if (auto* intType = dyn_cast<IntegerType>(type)) {
        return ConstantInt::get(intType, 0);
    } else if (auto* floatType = dyn_cast<FloatType>(type)) {
        return ConstantFP::get(floatType, 0.0f);
    }

    return nullptr;
}

int ComptimePass::countNonZeroElements(Constant* constant) {
    if (auto* constArray = dyn_cast<ConstantArray>(constant)) {
        int count = 0;
        for (size_t i = 0; i < constArray->getNumElements(); ++i) {
            auto* elem = constArray->getElement(i);
            if (auto* constInt = dyn_cast<ConstantInt>(elem)) {
                count += (constInt->getValue() != 0) ? 1 : 0;
            } else if (auto* constFP = dyn_cast<ConstantFP>(elem)) {
                count += (constFP->getValue() != 0.0f) ? 1 : 0;
            }
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
    if (auto* constArray = dyn_cast<ConstantArray>(constant)) {
        return constArray->getNumElements();
    }
    return 1;
}

void ComptimePass::collectFlatIndices(
    Constant* constant, std::vector<std::pair<int, Constant*>>& indices,
    bool onlyNonZero, int baseIndex) {
    if (auto* constArray = dyn_cast<ConstantArray>(constant)) {
        for (size_t i = 0; i < constArray->getNumElements(); ++i) {
            auto* elem = constArray->getElement(i);
            bool isZero = false;
            if (auto* constInt = dyn_cast<ConstantInt>(elem)) {
                isZero = (constInt->getValue() == 0);
            } else if (auto* constFP = dyn_cast<ConstantFP>(elem)) {
                isZero = (constFP->getValue() == 0.0f);
            }

            if (!onlyNonZero || !isZero) {
                indices.push_back({baseIndex + i, elem});
            }
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
    auto it = valueMap.find(v);
    if (it != valueMap.end()) return it->second;
    auto it2 = globalValueMap.find(v);
    if (it2 != globalValueMap.end()) return it2->second;
    return nullptr;
}

void ComptimePass::performRuntimePropagation(BasicBlock* startBlock,
                                             BasicBlock* endBlock,
                                             ValueMap& valueMap) {
    if (!startBlock || !endBlock) return;

    // TODO: Ensure that the startBlock can also be propagated to (if some
    // blocks loop back to startBlock)

    DEBUG_OUT()
        << "[DEBUG RuntimePropagation] Performing runtime propagation from "
        << startBlock->getName() << " to " << endBlock->getName() << std::endl;
    // BFS from all successors of startBlock to endBlock (exclusive)
    std::queue<BasicBlock*> worklist;
    VisitedSet visited;

    // Add all successors of startBlock to worklist
    for (auto* succ : startBlock->getSuccessors()) {
        if (succ != endBlock) {
            worklist.push(succ);
            visited.insert(succ);
        }
    }

    while (!worklist.empty()) {
        BasicBlock* current = worklist.front();
        worklist.pop();

        // Process instructions in this block
        for (auto* inst : *current) {
            if (auto* store = dyn_cast<StoreInst>(inst)) {
                // Mark store target as runtime
                Value* ptr = store->getPointerOperand();
                markAsRuntime(ptr, valueMap);
                valueMap.erase(ptr);
                globalValueMap.erase(ptr);
                DEBUG_OUT()
                    << "[DEBUG RuntimePropagation] Marking store target "
                       "as runtime: "
                    << IRPrinter::toString(ptr) << std::endl;
            } else if (auto* call = dyn_cast<CallInst>(inst)) {
                // Treat as runtime call - invalidate arrays
                invalidateValuesFromCall(call, valueMap);
                valueMap.erase(inst);
                globalValueMap.erase(inst);
                markAsRuntime(call, valueMap);
                DEBUG_OUT() << "[DEBUG RuntimePropagation] Invalidating values "
                               "and function result from call: "
                            << IRPrinter::toString(call) << std::endl;
            } else {
                markAsRuntime(inst, valueMap);
                valueMap.erase(inst);
                globalValueMap.erase(inst);
                DEBUG_OUT() << "[DEBUG RuntimePropagation] Marking instruction "
                               "as runtime: "
                            << IRPrinter::toString(inst);
            }
        }

        // Add unvisited successors
        for (auto* succ : current->getSuccessors()) {
            if (succ != endBlock && !visited.count(succ)) {
                worklist.push(succ);
                visited.insert(succ);
            }
        }
    }
}

void ComptimePass::markAsRuntime(Value* value, ValueMap& valueMap,
                                 bool no_gep) {
    if (!value) return;
    DEBUG_OUT() << "[DEBUG] Marking as runtime: " << IRPrinter::toString(value)
                << std::endl;

    if (auto gv = dyn_cast<GlobalVariable>(value)) {
        if (globalValueMap.count(gv)) {
            if (!isPropagation) setGlobalInitializer(gv, globalValueMap[gv]);
            globalValueMap.erase(gv);
        }
    }

    runtimeValues.insert(value);

    if (!no_gep) {
        if (auto* gep = dyn_cast<GetElementPtrInst>(value)) {
            markAsRuntime(gep->getPointerOperand(), valueMap);
        }
        if (auto* constGEP = dyn_cast<ConstantGEP>(value)) {
            markAsRuntime(constGEP->getArrayPointer(), valueMap);
        }
    }
}

void ComptimePass::invalidateValuesFromCall(CallInst* call,
                                            ValueMap& valueMap) {
    auto* func = call->getCalledFunction();
    // Invalidate all global variables
    std::vector<Value*> toErase;
    for (auto& [key, val] : valueMap) {
        if (isa<GlobalVariable>(key) && !func->isDeclaration()) {
            toErase.push_back(key);
        }
    }

    // Also invalidate arrays passed as arguments
    for (size_t i = 0; i < call->getNumArgOperands(); ++i) {
        Value* arg = call->getArgOperand(i);

        // Follow through GEPs to find base arrays
        while (auto* gep = dyn_cast<GetElementPtrInst>(arg)) {
            arg = gep->getPointerOperand();
        }

        if (isa<AllocaInst>(arg) || isa<GlobalVariable>(arg)) {
            toErase.push_back(arg);
        }
    }

    for (auto* key : toErase) {
        markAsRuntime(key, valueMap);
    }
}

BasicBlock* ComptimePass::getPostImmediateDominator(BasicBlock* block) {
    if (!analysisManager || !block->getParent()) {
        return nullptr;
    }

    // Use the existing PostDominanceInfo
    auto* postDomInfo = analysisManager->getAnalysis<PostDominanceInfo>(
        PostDominanceAnalysis::getName(), *block->getParent());
    if (!postDomInfo) {
        return nullptr;
    }

    return postDomInfo->getImmediateDominator(block);
}

REGISTER_PASS(ComptimePass, "comptime");

}  // namespace midend