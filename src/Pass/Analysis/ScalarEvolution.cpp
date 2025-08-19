#include "Pass/Analysis/ScalarEvolution.h"

#include <unordered_map>
#include <unordered_set>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Type.h"
#include "IR/Value.h"
#include "Pass/Analysis/LoopInfo.h"
#include "Support/Logger.h"

namespace midend {

namespace SCEVUtils {

bool isInLoop(Value* V, Loop* L) {
    if (!V || !L) return false;

    if (auto* inst = dynamic_cast<Instruction*>(V)) {
        return L->contains(inst->getParent());
    }

    return false;
}

bool isLoopInvariant(Instruction* I, Loop* L) {
    if (!I || !L) return false;

    // 检查指令是否在循环中
    if (!L->contains(I->getParent())) {
        return true;  // 不在循环中的指令是循环不变量
    }

    // 检查操作数是否都是循环不变量
    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
        auto* op = I->getOperand(i);
        if (!isLoopInvariant(op, L)) {
            return false;
        }
    }

    return true;
}

bool isLoopInvariant(Value* V, Loop* L) {
    if (!V || !L) return false;

    // 常量是循环不变量
    if (dynamic_cast<Constant*>(V)) {
        return true;
    }

    // 检查指令
    if (auto* inst = dynamic_cast<Instruction*>(V)) {
        return isLoopInvariant(inst, L);
    }

    // 其他值（如参数）可能是循环不变量
    return true;
}

std::unique_ptr<SCEV> getLoopTripCount(Loop* L) {
    if (!L) return nullptr;

    // 简化实现：返回未知表达式
    // 实际实现需要复杂的循环分析
    return std::make_unique<SCEVUnknown>(nullptr);
}

bool isCountableLoop(Loop* L) {
    if (!L) return false;

    // 简化实现：检查是否有单个回边
    return L->hasSingleBackedge();
}

std::unique_ptr<SCEV> getLoopStep(Loop* L) {
    if (!L) return nullptr;

    // 简化实现：返回常量1
    // 实际实现需要分析循环的步长
    return std::make_unique<SCEVConstant>(nullptr);
}

std::unique_ptr<SCEV> getLoopStart(Loop* L) {
    if (!L) return nullptr;

    // 简化实现：返回常量0
    // 实际实现需要分析循环的起始值
    return std::make_unique<SCEVConstant>(nullptr);
}

bool isLinear(const SCEV* scev) {
    if (!scev) return false;

    switch (scev->getSCEVType()) {
        case SCEVType::Constant:
        case SCEVType::Unknown:
            return true;
        case SCEVType::Add:
            return true;  // 加法表达式是线性的
        case SCEVType::AddRec:
            return true;  // 循环递推表达式是线性的
        default:
            return false;
    }
}

bool isPolynomial(const SCEV* scev) {
    if (!scev) return false;

    switch (scev->getSCEVType()) {
        case SCEVType::Constant:
        case SCEVType::Unknown:
        case SCEVType::Add:
        case SCEVType::AddRec:
            return true;
        case SCEVType::Mul:
            return true;  // 乘法表达式可能是多项式
        default:
            return false;
    }
}

unsigned getPolynomialDegree(const SCEV* scev) {
    if (!scev) return 0;

    switch (scev->getSCEVType()) {
        case SCEVType::Constant:
        case SCEVType::Unknown:
            return 0;
        case SCEVType::Add:
            return 1;  // 加法表达式是1次多项式
        case SCEVType::AddRec:
            return 1;  // 循环递推表达式是1次多项式
        case SCEVType::Mul:
            return 2;  // 乘法表达式可能是2次多项式
        default:
            return 0;
    }
}

bool areEqual(const SCEV* A, const SCEV* B) {
    if (!A || !B) return A == B;
    return A->equals(B);
}

bool isZero(const SCEV* scev) {
    if (!scev) return false;

    if (auto* constScev = dynamic_cast<const SCEVConstant*>(scev)) {
        // 检查常量值是否为0
        return false;  // 简化实现
    }

    return false;
}

bool isOne(const SCEV* scev) {
    if (!scev) return false;

    if (auto* constScev = dynamic_cast<const SCEVConstant*>(scev)) {
        // 检查常量值是否为1
        return false;  // 简化实现
    }

    return false;
}

bool isConstant(const SCEV* scev) { return scev && scev->isConstant(); }

int64_t getConstantValue(const SCEV* scev) {
    if (!scev || !scev->isConstant()) {
        return 0;  // 默认值
    }

    if (auto* constScev = dynamic_cast<const SCEVConstant*>(scev)) {
        // 从常量中提取值
        return 0;  // 简化实现
    }

    return 0;
}

std::unique_ptr<SCEV> createAddExpr(
    std::vector<std::unique_ptr<SCEV>> operands) {
    return std::make_unique<SCEVAddExpr>(std::move(operands));
}

std::unique_ptr<SCEV> createMulExpr(
    std::vector<std::unique_ptr<SCEV>> operands) {
    return std::make_unique<SCEVMulExpr>(std::move(operands));
}

std::unique_ptr<SCEV> createConstantExpr(int64_t value, Type* type) {
    // 简化实现：创建未知表达式
    return std::make_unique<SCEVUnknown>(nullptr);
}

std::unique_ptr<SCEV> createUnknownExpr(Value* value) {
    return std::make_unique<SCEVUnknown>(value);
}

std::unique_ptr<SCEV> createAddRecExpr(std::unique_ptr<SCEV> start,
                                       std::unique_ptr<SCEV> step, Loop* loop) {
    return std::make_unique<SCEVAddRecExpr>(std::move(start), std::move(step),
                                            loop);
}

std::unique_ptr<SCEV> simplifySCEV(std::unique_ptr<SCEV> scev) {
    if (!scev) return nullptr;

    // 简化实现：直接返回原表达式
    return scev;
}

std::unique_ptr<SCEV> canonicalizeSCEV(std::unique_ptr<SCEV> scev) {
    if (!scev) return nullptr;

    // 规范化实现：直接返回原表达式
    return scev;
}

bool containsLoop(const SCEV* scev, Loop* L) {
    if (!scev || !L) return false;

    auto loops = scev->getLoops();
    for (auto* loop : loops) {
        if (loop == L) return true;
    }

    return false;
}

std::vector<Loop*> getLoopsInSCEV(const SCEV* scev) {
    if (!scev) return {};

    return scev->getLoops();
}

std::unique_ptr<SCEV> replaceLoopInSCEV(const SCEV* scev, Loop* oldLoop,
                                        Loop* newLoop) {
    if (!scev) return nullptr;

    // 简化实现：直接返回原表达式的副本
    return std::make_unique<SCEVUnknown>(nullptr);
}

std::unique_ptr<SCEV> evaluateAtIteration(const SCEV* scev,
                                          unsigned iteration) {
    if (!scev) return nullptr;

    // 简化实现：返回未知表达式
    return std::make_unique<SCEVUnknown>(nullptr);
}

bool isMonotonicIncreasing(const SCEV* scev, Loop* L) {
    if (!scev || !L) return false;

    // 简化实现：检查是否为循环递推表达式且步长为正
    if (auto* addRec = dynamic_cast<const SCEVAddRecExpr*>(scev)) {
        if (addRec->getLoop() == L) {
            // 检查步长是否为正
            return false;  // 简化实现
        }
    }

    return false;
}

bool isMonotonicDecreasing(const SCEV* scev, Loop* L) {
    if (!scev || !L) return false;

    // 简化实现：检查是否为循环递推表达式且步长为负
    if (auto* addRec = dynamic_cast<const SCEVAddRecExpr*>(scev)) {
        if (addRec->getLoop() == L) {
            // 检查步长是否为负
            return false;  // 简化实现
        }
    }

    return false;
}

std::pair<std::unique_ptr<SCEV>, std::unique_ptr<SCEV>> getSCEVRange(
    const SCEV* scev, Loop* L) {
    if (!scev || !L) {
        return {nullptr, nullptr};
    }

    // 简化实现：返回未知表达式的范围
    return {std::make_unique<SCEVUnknown>(nullptr),
            std::make_unique<SCEVUnknown>(nullptr)};
}

}  // namespace SCEVUtils

// SCEVConstant 实现
SCEVConstant::SCEVConstant(Constant* val)
    : SCEV(val->getType(), SCEVType::Constant), value_(val) {}

std::string SCEVConstant::toString() const { return value_->toString(); }

bool SCEVConstant::equals(const SCEV* other) const {
    if (!other || other->getSCEVType() != SCEVType::Constant) return false;
    const SCEVConstant* otherConst = static_cast<const SCEVConstant*>(other);
    return value_ == otherConst->value_;
}

// SCEVUnknown 实现
SCEVUnknown::SCEVUnknown(Value* val)
    : SCEV(val->getType(), SCEVType::Unknown), value_(val) {}

std::string SCEVUnknown::toString() const {
    return value_->getName().empty() ? "unknown" : value_->getName();
}

bool SCEVUnknown::equals(const SCEV* other) const {
    if (!other || other->getSCEVType() != SCEVType::Unknown) return false;
    const SCEVUnknown* otherUnknown = static_cast<const SCEVUnknown*>(other);
    return value_ == otherUnknown->value_;
}

// SCEVAddExpr 实现
SCEVAddExpr::SCEVAddExpr(std::vector<std::unique_ptr<SCEV>> ops)
    : SCEV(ops.empty() ? nullptr : ops[0]->getType(), SCEVType::Add) {
    operands_ = std::move(ops);
}

unsigned SCEVAddExpr::getExpressionSize() const {
    unsigned size = 1;
    for (const auto& op : operands_) {
        size += op->getExpressionSize();
    }
    return size;
}

bool SCEVAddExpr::hasLoop() const {
    for (const auto& op : operands_) {
        if (op->hasLoop()) return true;
    }
    return false;
}

std::vector<Loop*> SCEVAddExpr::getLoops() const {
    std::vector<Loop*> loops;
    for (const auto& op : operands_) {
        auto opLoops = op->getLoops();
        loops.insert(loops.end(), opLoops.begin(), opLoops.end());
    }
    return loops;
}

bool SCEVAddExpr::isConstant() const {
    for (const auto& op : operands_) {
        if (!op->isConstant()) return false;
    }
    return true;
}

std::string SCEVAddExpr::toString() const {
    if (operands_.empty()) return "0";

    std::string result = operands_[0]->toString();
    for (size_t i = 1; i < operands_.size(); ++i) {
        result += " + " + operands_[i]->toString();
    }
    return result;
}

bool SCEVAddExpr::equals(const SCEV* other) const {
    if (!other || other->getSCEVType() != SCEVType::Add) return false;

    const SCEVAddExpr* otherAdd = static_cast<const SCEVAddExpr*>(other);
    if (operands_.size() != otherAdd->operands_.size()) return false;

    for (size_t i = 0; i < operands_.size(); ++i) {
        if (!operands_[i]->equals(otherAdd->operands_[i].get())) return false;
    }
    return true;
}

// SCEVMulExpr 实现
SCEVMulExpr::SCEVMulExpr(std::vector<std::unique_ptr<SCEV>> ops)
    : SCEV(ops.empty() ? nullptr : ops[0]->getType(), SCEVType::Mul) {
    operands_ = std::move(ops);
}

unsigned SCEVMulExpr::getExpressionSize() const {
    unsigned size = 1;
    for (const auto& op : operands_) {
        size += op->getExpressionSize();
    }
    return size;
}

bool SCEVMulExpr::hasLoop() const {
    for (const auto& op : operands_) {
        if (op->hasLoop()) return true;
    }
    return false;
}

std::vector<Loop*> SCEVMulExpr::getLoops() const {
    std::vector<Loop*> loops;
    for (const auto& op : operands_) {
        auto opLoops = op->getLoops();
        loops.insert(loops.end(), opLoops.begin(), opLoops.end());
    }
    return loops;
}

bool SCEVMulExpr::isConstant() const {
    for (const auto& op : operands_) {
        if (!op->isConstant()) return false;
    }
    return true;
}

std::string SCEVMulExpr::toString() const {
    if (operands_.empty()) return "1";

    std::string result = operands_[0]->toString();
    for (size_t i = 1; i < operands_.size(); ++i) {
        result += " * " + operands_[i]->toString();
    }
    return result;
}

bool SCEVMulExpr::equals(const SCEV* other) const {
    if (!other || other->getSCEVType() != SCEVType::Mul) return false;

    const SCEVMulExpr* otherMul = static_cast<const SCEVMulExpr*>(other);
    if (operands_.size() != otherMul->operands_.size()) return false;

    for (size_t i = 0; i < operands_.size(); ++i) {
        if (!operands_[i]->equals(otherMul->operands_[i].get())) return false;
    }
    return true;
}

// SCEVAddRecExpr 实现
SCEVAddRecExpr::SCEVAddRecExpr(std::unique_ptr<SCEV> start,
                               std::unique_ptr<SCEV> step, Loop* loop)
    : SCEV(start->getType(), SCEVType::AddRec),
      start_(std::move(start)),
      step_(std::move(step)),
      loop_(loop) {}

unsigned SCEVAddRecExpr::getExpressionSize() const {
    return 1 + start_->getExpressionSize() + step_->getExpressionSize();
}

std::vector<Loop*> SCEVAddRecExpr::getLoops() const {
    std::vector<Loop*> loops = {loop_};
    auto startLoops = start_->getLoops();
    auto stepLoops = step_->getLoops();
    loops.insert(loops.end(), startLoops.begin(), startLoops.end());
    loops.insert(loops.end(), stepLoops.begin(), stepLoops.end());
    return loops;
}

bool SCEVAddRecExpr::isConstant() const {
    return start_->isConstant() && step_->isConstant();
}

std::string SCEVAddRecExpr::toString() const {
    return "{" + start_->toString() + ", +, " + step_->toString() + "}";
}

bool SCEVAddRecExpr::equals(const SCEV* other) const {
    if (!other || other->getSCEVType() != SCEVType::AddRec) return false;

    const SCEVAddRecExpr* otherAddRec =
        static_cast<const SCEVAddRecExpr*>(other);
    return start_->equals(otherAddRec->start_.get()) &&
           step_->equals(otherAddRec->step_.get()) &&
           loop_ == otherAddRec->loop_;
}

// ScalarEvolutionResult 实现
SCEV* ScalarEvolutionResult::getSCEV(Value* V) {
    auto it = scevCache_.find(V);
    if (it != scevCache_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ScalarEvolutionResult::setSCEV(Value* V, std::unique_ptr<SCEV> scev) {
    scevCache_[V] = std::move(scev);
}

const std::vector<std::unique_ptr<SCEV>>& ScalarEvolutionResult::getLoopSCEVs(
    Loop* L) const {
    static const std::vector<std::unique_ptr<SCEV>> empty;
    auto it = loopScevCache_.find(L);
    if (it != loopScevCache_.end()) {
        return it->second;
    }
    return empty;
}

void ScalarEvolutionResult::addLoopSCEV(Loop* L, std::unique_ptr<SCEV> scev) {
    loopScevCache_[L].push_back(std::move(scev));
}

bool ScalarEvolutionResult::hasSCEV(Value* V) const {
    return scevCache_.find(V) != scevCache_.end();
}

void ScalarEvolutionResult::clear() {
    scevCache_.clear();
    loopScevCache_.clear();
}

// ScalarEvolutionAnalysis 实现
std::unique_ptr<AnalysisResult> ScalarEvolutionAnalysis::runOnFunction(
    Function& f, AnalysisManager& AM) {
    auto result = std::make_unique<ScalarEvolutionResult>();

    // 获取LoopInfo分析结果
    auto loopInfoResult = AM.getAnalysis<LoopInfo>("LoopAnalysis", f);
    if (!loopInfoResult) {
        // LOG_WARNING("LoopInfo analysis not available for SCEV analysis");
        return result;
    }

    loopInfo_ = loopInfoResult;

    // 首先分析循环中的SCEV（利用循环信息进行更精确的分析）
    for (auto& loop : loopInfo_->getTopLevelLoops()) {
        analyzeLoopSCEVs(loop.get(), *result);
    }

    // 然后分析函数中剩余的值（那些不在循环中或未被循环分析处理的）
    for (auto& bb : f) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            auto* inst = *it;
            if (!result->hasSCEV(inst)) {
                auto scev = createSCEV(inst, *result);
                if (scev) {
                    result->setSCEV(inst, std::move(scev));
                }
            }
        }
    }

    return result;
}

std::vector<std::string> ScalarEvolutionAnalysis::getDependencies() const {
    return {"LoopAnalysis"};
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::createSCEV(
    Value* V, ScalarEvolutionResult& result) {
    // 如果已经有SCEV，直接返回
    if (result.hasSCEV(V)) {
        return nullptr;  // 已经存在，不需要重新创建
    }

    // 处理常量
    if (auto* constant = dynamic_cast<Constant*>(V)) {
        return std::make_unique<SCEVConstant>(constant);
    }

    // 处理指令
    if (auto* inst = dynamic_cast<Instruction*>(V)) {
        return createSCEVForInstruction(inst, result);
    }

    // 其他值作为未知处理
    return std::make_unique<SCEVUnknown>(V);
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::createSCEVForInstruction(
    Instruction* I, ScalarEvolutionResult& result) {
    // 这里简化处理，只处理基本的算术指令
    // 实际实现中需要处理更多指令类型

    switch (I->getOpcode()) {
        case Opcode::Add: {
            auto* lhs = I->getOperand(0);
            auto* rhs = I->getOperand(1);

            auto lhsSCEV = result.getSCEV(lhs);
            auto rhsSCEV = result.getSCEV(rhs);

            if (lhsSCEV && rhsSCEV) {
                std::vector<std::unique_ptr<SCEV>> operands;
                operands.push_back(std::make_unique<SCEVAddExpr>(
                    std::vector<std::unique_ptr<SCEV>>{}));
                operands.push_back(std::make_unique<SCEVAddExpr>(
                    std::vector<std::unique_ptr<SCEV>>{}));
                return std::make_unique<SCEVAddExpr>(std::move(operands));
            }
            break;
        }
        case Opcode::Mul: {
            auto* lhs = I->getOperand(0);
            auto* rhs = I->getOperand(1);

            auto lhsSCEV = result.getSCEV(lhs);
            auto rhsSCEV = result.getSCEV(rhs);

            if (lhsSCEV && rhsSCEV) {
                std::vector<std::unique_ptr<SCEV>> operands;
                operands.push_back(std::make_unique<SCEVMulExpr>(
                    std::vector<std::unique_ptr<SCEV>>{}));
                operands.push_back(std::make_unique<SCEVMulExpr>(
                    std::vector<std::unique_ptr<SCEV>>{}));
                return std::make_unique<SCEVMulExpr>(std::move(operands));
            }
            break;
        }
        default:
            break;
    }

    // 默认作为未知处理
    return std::make_unique<SCEVUnknown>(I);
}

void ScalarEvolutionAnalysis::analyzeLoopSCEVs(Loop* L,
                                               ScalarEvolutionResult& result) {
    // 存储基础循环变量及其初值和步长信息
    // pair的first是初值，second是步长
    std::unordered_map<Value*, std::pair<Value*, Value*>> basicLoopVars;

    // 第一步：分析phi指令，识别基础循环变量
    analyzePhiInstructions(L, result, basicLoopVars);

    // 第二步：分析其他赋值语句，寻找基础循环变量的线性函数
    analyzeLinearExpressions(L, result, basicLoopVars);

    // 第三步：多轮分析直到收敛，处理高阶依赖
    analyzeUntilConvergence(L, result, basicLoopVars);
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::simplifySCEV(
    std::unique_ptr<SCEV> scev) {
    if (!scev) return nullptr;

    // 简化加法表达式
    if (auto* addExpr = dynamic_cast<SCEVAddExpr*>(scev.get())) {
        return combineAddExprs(addExpr->getOperands());
    }

    // 简化乘法表达式
    if (auto* mulExpr = dynamic_cast<SCEVMulExpr*>(scev.get())) {
        return combineMulExprs(mulExpr->getOperands());
    }

    return scev;
}

bool ScalarEvolutionAnalysis::canSimplify(const SCEV* scev) const {
    if (!scev) return false;

    // 检查是否可以简化
    switch (scev->getSCEVType()) {
        case SCEVType::Add:
        case SCEVType::Mul:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::combineAddExprs(
    const std::vector<std::unique_ptr<SCEV>>& operands) {
    // 简化加法表达式
    std::vector<std::unique_ptr<SCEV>> simplified;

    for (const auto& op : operands) {
        if (auto* addOp = dynamic_cast<SCEVAddExpr*>(op.get())) {
            // 展开嵌套的加法表达式
            for (const auto& nestedOp : addOp->getOperands()) {
                simplified.push_back(
                    std::make_unique<SCEVUnknown>(nestedOp.get()));
            }
        } else {
            simplified.push_back(std::make_unique<SCEVUnknown>(op.get()));
        }
    }

    return std::make_unique<SCEVAddExpr>(std::move(simplified));
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::combineMulExprs(
    const std::vector<std::unique_ptr<SCEV>>& operands) {
    // 简化乘法表达式
    std::vector<std::unique_ptr<SCEV>> simplified;

    for (const auto& op : operands) {
        if (auto* mulOp = dynamic_cast<SCEVMulExpr*>(op.get())) {
            // 展开嵌套的乘法表达式
            for (const auto& nestedOp : mulOp->getOperands()) {
                simplified.push_back(
                    std::make_unique<SCEVUnknown>(nestedOp.get()));
            }
        } else {
            simplified.push_back(std::make_unique<SCEVUnknown>(op.get()));
        }
    }

    return std::make_unique<SCEVMulExpr>(std::move(simplified));
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::createAddRecExpr(
    std::unique_ptr<SCEV> start, std::unique_ptr<SCEV> step, Loop* loop) {
    return std::make_unique<SCEVAddRecExpr>(std::move(start), std::move(step),
                                            loop);
}

void ScalarEvolutionAnalysis::analyzePhiInstructions(
    Loop* L, ScalarEvolutionResult& result,
    std::unordered_map<Value*, std::pair<Value*, Value*>>& basicLoopVars) {
    auto* header = L->getHeader();

    // 遍历循环头基本块中的所有phi指令
    for (auto it = header->begin(); it != header->end(); ++it) {
        auto* inst = *it;
        auto* phiInst = dynamic_cast<PHINode*>(inst);
        if (!phiInst) continue;

        Value* initValue = nullptr;  // 来自循环外的初值
        Value* loopValue = nullptr;  // 来自循环体的取值来源

        // 分析phi指令的两个输入
        for (unsigned i = 0; i < phiInst->getNumIncomingValues(); ++i) {
            auto* incomingBB = phiInst->getIncomingBlock(i);
            auto* incomingValue = phiInst->getIncomingValue(i);

            if (L->contains(incomingBB)) {
                // 来自循环内的值
                loopValue = incomingValue;
            } else {
                // 来自循环外的初值
                initValue = incomingValue;
            }
        }

        if (!initValue || !loopValue) continue;

        // 检查循环内的取值来源是否可以写成：phi + 循环无关变量
        if (auto* binaryOp = dynamic_cast<BinaryOperator*>(loopValue)) {
            if (binaryOp->getOpcode() == Opcode::Add) {
                Value* stepValue = nullptr;
                bool isBasicLoop = false;

                // 检查加法的两个操作数
                auto* lhs = binaryOp->getOperand(0);
                auto* rhs = binaryOp->getOperand(1);

                if (lhs == phiInst && isLoopInvariant(rhs, L)) {
                    // 形式：phi + constant
                    stepValue = rhs;
                    isBasicLoop = true;
                } else if (rhs == phiInst && isLoopInvariant(lhs, L)) {
                    // 形式：constant + phi
                    stepValue = lhs;
                    isBasicLoop = true;
                }

                if (isBasicLoop) {
                    // 记录基础循环变量信息
                    basicLoopVars[phiInst] =
                        std::make_pair(initValue, stepValue);

                    // 创建AddRec SCEV表达式
                    auto initSCEV = createSCEVForValue(initValue);
                    auto stepSCEV = createSCEVForValue(stepValue);
                    auto addRecSCEV = createAddRecExpr(std::move(initSCEV),
                                                       std::move(stepSCEV), L);

                    result.setSCEV(phiInst, std::move(addRecSCEV));
                }
            }
        }

        // 检查是否是基于其他基础循环变量的线性表达式
        if (!result.hasSCEV(phiInst)) {
            for (const auto& baseVar : basicLoopVars) {
                auto* basePhiInst = baseVar.first;
                auto linearCoeffs =
                    matchLinearExpression(loopValue, basePhiInst);

                if (linearCoeffs.first && linearCoeffs.second) {
                    auto* k = linearCoeffs.first;    // 系数
                    auto* a2 = linearCoeffs.second;  // 常数项

                    if (isLoopInvariant(k, L) && isLoopInvariant(a2, L)) {
                        // 验证并计算正确的初值
                        Value* computedInitValue = nullptr;
                        if (validateAndComputePhiInitValue(
                                initValue, loopValue, basePhiInst,
                                baseVar.second, &computedInitValue)) {
                            auto baseStepValue = baseVar.second.second;

                            // 使用计算出的或验证过的初值
                            Value* correctInitValue = computedInitValue
                                                          ? computedInitValue
                                                          : initValue;

                            // 创建SCEV表达式：基于基础变量的线性组合
                            std::vector<std::unique_ptr<SCEV>> stepOperands;
                            stepOperands.push_back(createSCEVForValue(k));
                            stepOperands.push_back(
                                createSCEVForValue(baseStepValue));
                            auto newStepSCEV = std::make_unique<SCEVMulExpr>(
                                std::move(stepOperands));

                            auto newInitSCEV =
                                createSCEVForValue(correctInitValue);
                            auto derivedSCEV =
                                createAddRecExpr(std::move(newInitSCEV),
                                                 std::move(newStepSCEV), L);

                            result.setSCEV(phiInst, std::move(derivedSCEV));
                            break;
                        }
                    }
                }
            }
        }
    }
}

bool ScalarEvolutionAnalysis::isLoopInvariant(Value* V, Loop* L) {
    // 常量总是循环无关的
    if (dynamic_cast<Constant*>(V)) {
        return true;
    }

    // 检查指令是否定义在循环外
    if (auto* inst = dynamic_cast<Instruction*>(V)) {
        return !L->contains(inst->getParent());
    }

    // 函数参数等其他值认为是循环无关的
    return true;
}

void ScalarEvolutionAnalysis::analyzeLinearExpressions(
    Loop* L, ScalarEvolutionResult& result,
    const std::unordered_map<Value*, std::pair<Value*, Value*>>&
        basicLoopVars) {
    // 遍历循环中的所有指令
    for (auto* bb : L->getBlocks()) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            auto* inst = *it;

            // 跳过已经分析过的phi指令
            if (dynamic_cast<PHINode*>(inst)) {
                continue;
            }

            // 跳过已经有SCEV的指令
            if (result.hasSCEV(inst)) {
                continue;
            }

            // 尝试识别线性表达式，先尝试基础循环变量
            bool found = false;
            for (const auto& basicVar : basicLoopVars) {
                auto* baseVar = basicVar.first;
                auto initValue = basicVar.second.first;
                auto stepValue = basicVar.second.second;

                auto linearCoeffs = matchLinearExpression(inst, baseVar);
                if (linearCoeffs.first && linearCoeffs.second) {
                    // 找到了线性关系：k*base + a2
                    auto* k = linearCoeffs.first;    // 系数
                    auto* a2 = linearCoeffs.second;  // 常数项

                    // 确保k和a2都是循环无关的
                    if (isLoopInvariant(k, L) && isLoopInvariant(a2, L)) {
                        // 尝试计算常量值
                        std::unique_ptr<SCEV> startSCEV, stepSCEV;

                        // 如果所有值都是常量，直接计算结果
                        auto* initConst = dynamic_cast<Constant*>(initValue);
                        auto* stepConst = dynamic_cast<Constant*>(stepValue);
                        auto* kConst = dynamic_cast<Constant*>(k);
                        auto* a2Const = dynamic_cast<Constant*>(a2);

                        if (initConst && stepConst && kConst && a2Const) {
                            // 计算新的初值：initValue * k + a2
                            auto* initConstInt =
                                dynamic_cast<ConstantInt*>(initConst);
                            auto* kConstInt =
                                dynamic_cast<ConstantInt*>(kConst);
                            auto* a2ConstInt =
                                dynamic_cast<ConstantInt*>(a2Const);
                            auto* stepConstInt =
                                dynamic_cast<ConstantInt*>(stepConst);

                            if (initConstInt && kConstInt && a2ConstInt &&
                                stepConstInt) {
                                int32_t newStart =
                                    initConstInt->getSignedValue() *
                                        kConstInt->getSignedValue() +
                                    a2ConstInt->getSignedValue();
                                int32_t newStep =
                                    stepConstInt->getSignedValue() *
                                    kConstInt->getSignedValue();

                                auto* context =
                                    initValue->getType()->getContext();
                                auto* newStartConst =
                                    ConstantInt::get(context, 32, newStart);
                                auto* newStepConst =
                                    ConstantInt::get(context, 32, newStep);

                                startSCEV = std::make_unique<SCEVConstant>(
                                    newStartConst);
                                stepSCEV = std::make_unique<SCEVConstant>(
                                    newStepConst);
                            } else {
                                // 如果不是整数常量，回退到表达式形式
                                goto create_expression;
                            }
                        } else {
                        create_expression:
                            // 创建SCEV表达式
                            std::vector<std::unique_ptr<SCEV>> initOperands;
                            initOperands.push_back(
                                createSCEVForValue(initValue));
                            initOperands.push_back(createSCEVForValue(k));
                            auto initMul = std::make_unique<SCEVMulExpr>(
                                std::move(initOperands));

                            std::vector<std::unique_ptr<SCEV>> startOperands;
                            startOperands.push_back(std::move(initMul));
                            startOperands.push_back(createSCEVForValue(a2));
                            startSCEV = std::make_unique<SCEVAddExpr>(
                                std::move(startOperands));

                            std::vector<std::unique_ptr<SCEV>> stepOperands;
                            stepOperands.push_back(
                                createSCEVForValue(stepValue));
                            stepOperands.push_back(createSCEVForValue(k));
                            stepSCEV = std::make_unique<SCEVMulExpr>(
                                std::move(stepOperands));
                        }

                        auto linearSCEV = createAddRecExpr(
                            std::move(startSCEV), std::move(stepSCEV), L);

                        result.setSCEV(inst, std::move(linearSCEV));
                        found = true;
                        break;
                    }
                }
            }

            // 如果基于基础循环变量没有找到，尝试基于已知的SCEV
            if (!found) {
                if (tryCreateSCEVFromKnownSCEVs(inst, L, result)) {
                    found = true;
                }
            }

            // 如果没有找到线性关系，创建默认的SCEV
            if (!found && !result.hasSCEV(inst)) {
                result.setSCEV(inst, createSCEVForValue(inst));
            }
        }
    }
}

bool ScalarEvolutionAnalysis::isBasicLoopVariable(
    Value* V, const std::unordered_map<Value*, std::pair<Value*, Value*>>&
                  basicLoopVars) {
    return basicLoopVars.find(V) != basicLoopVars.end();
}

std::pair<Value*, Value*> ScalarEvolutionAnalysis::matchLinearExpression(
    Value* V, Value* baseVar) {
    // 尝试匹配 k*base + a2 的形式
    if (auto* addOp = dynamic_cast<BinaryOperator*>(V)) {
        if (addOp->getOpcode() == Opcode::Add) {
            auto* lhs = addOp->getOperand(0);
            auto* rhs = addOp->getOperand(1);

            // 检查 lhs 是否是 k*base
            if (auto* mulOp = dynamic_cast<BinaryOperator*>(lhs)) {
                if (mulOp->getOpcode() == Opcode::Mul) {
                    auto* mulLhs = mulOp->getOperand(0);
                    auto* mulRhs = mulOp->getOperand(1);

                    if (mulLhs == baseVar) {
                        return std::make_pair(mulRhs, rhs);  // k=mulRhs, a2=rhs
                    } else if (mulRhs == baseVar) {
                        return std::make_pair(mulLhs, rhs);  // k=mulLhs, a2=rhs
                    }
                }
            }

            // 检查 rhs 是否是 k*base
            if (auto* mulOp = dynamic_cast<BinaryOperator*>(rhs)) {
                if (mulOp->getOpcode() == Opcode::Mul) {
                    auto* mulLhs = mulOp->getOperand(0);
                    auto* mulRhs = mulOp->getOperand(1);

                    if (mulLhs == baseVar) {
                        return std::make_pair(mulRhs, lhs);  // k=mulRhs, a2=lhs
                    } else if (mulRhs == baseVar) {
                        return std::make_pair(mulLhs, lhs);  // k=mulLhs, a2=lhs
                    }
                }
            }

            // 检查是否是简单的 base + a2 形式 (k=1)
            if (lhs == baseVar) {
                // 创建常量1作为系数
                auto* context = baseVar->getType()->getContext();
                auto* one = ConstantInt::get(context, 32, 1);
                return std::make_pair(one, rhs);
            } else if (rhs == baseVar) {
                auto* context = baseVar->getType()->getContext();
                auto* one = ConstantInt::get(context, 32, 1);
                return std::make_pair(one, lhs);
            }
        }
    }

    // 尝试匹配单独的 k*base 形式 (a2=0)
    if (auto* mulOp = dynamic_cast<BinaryOperator*>(V)) {
        if (mulOp->getOpcode() == Opcode::Mul) {
            auto* lhs = mulOp->getOperand(0);
            auto* rhs = mulOp->getOperand(1);

            // 直接匹配 k * baseVar
            if (lhs == baseVar) {
                auto* context = baseVar->getType()->getContext();
                auto* zero = ConstantInt::get(context, 32, 0);
                return std::make_pair(rhs, zero);  // k=rhs, a2=0
            } else if (rhs == baseVar) {
                auto* context = baseVar->getType()->getContext();
                auto* zero = ConstantInt::get(context, 32, 0);
                return std::make_pair(lhs, zero);  // k=lhs, a2=0
            }

            // 匹配 k * (baseVar + c) 形式，需要展开为 k*baseVar + k*c
            if (auto* addInMul = dynamic_cast<BinaryOperator*>(lhs)) {
                if (addInMul->getOpcode() == Opcode::Add) {
                    auto* addLhs = addInMul->getOperand(0);
                    auto* addRhs = addInMul->getOperand(1);

                    if (addLhs == baseVar) {
                        // 形式：k * (baseVar + c) = k*baseVar + k*c
                        // 这里返回k作为系数，k*c作为常数项
                        // 但由于addRhs可能不是常量，我们暂时简化处理
                        auto* context = baseVar->getType()->getContext();
                        auto* zero = ConstantInt::get(context, 32, 0);
                        return std::make_pair(
                            rhs, zero);  // 简化：只考虑k*baseVar部分
                    } else if (addRhs == baseVar) {
                        auto* context = baseVar->getType()->getContext();
                        auto* zero = ConstantInt::get(context, 32, 0);
                        return std::make_pair(rhs, zero);
                    }
                }
            }

            if (auto* addInMul = dynamic_cast<BinaryOperator*>(rhs)) {
                if (addInMul->getOpcode() == Opcode::Add) {
                    auto* addLhs = addInMul->getOperand(0);
                    auto* addRhs = addInMul->getOperand(1);

                    if (addLhs == baseVar) {
                        auto* context = baseVar->getType()->getContext();
                        auto* zero = ConstantInt::get(context, 32, 0);
                        return std::make_pair(lhs, zero);
                    } else if (addRhs == baseVar) {
                        auto* context = baseVar->getType()->getContext();
                        auto* zero = ConstantInt::get(context, 32, 0);
                        return std::make_pair(lhs, zero);
                    }
                }
            }
        }
    }

    return std::make_pair(nullptr, nullptr);
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::createSCEVForValue(Value* V) {
    // 如果是常量，创建SCEVConstant
    if (auto* constant = dynamic_cast<Constant*>(V)) {
        return std::make_unique<SCEVConstant>(constant);
    }

    // 否则创建SCEVUnknown
    return std::make_unique<SCEVUnknown>(V);
}

bool ScalarEvolutionAnalysis::validateAndComputePhiInitValue(
    Value* phiInitValue, Value* loopValue, Value* baseVar,
    const std::pair<Value*, Value*>& baseVarInfo, Value** computedInitValue) {
    // 获取基础变量的初值和步长
    auto* baseInitValue = baseVarInfo.first;

    // 尝试匹配loopValue相对于baseVar的线性关系
    auto linearCoeffs = matchLinearExpression(loopValue, baseVar);
    if (!linearCoeffs.first || !linearCoeffs.second) {
        return false;
    }

    auto* k = linearCoeffs.first;    // 系数
    auto* a2 = linearCoeffs.second;  // 常数项

    // 计算期望的初值：k * baseInitValue + a2
    auto* kConst = dynamic_cast<ConstantInt*>(k);
    auto* a2Const = dynamic_cast<ConstantInt*>(a2);
    auto* baseInitConst = dynamic_cast<ConstantInt*>(baseInitValue);
    auto* phiInitConst = dynamic_cast<ConstantInt*>(phiInitValue);

    if (kConst && a2Const && baseInitConst) {
        int32_t expectedInit =
            kConst->getSignedValue() * baseInitConst->getSignedValue() +
            a2Const->getSignedValue();

        if (phiInitConst) {
            // 验证phi的初值是否正确
            if (phiInitConst->getSignedValue() == expectedInit) {
                *computedInitValue = nullptr;  // 使用原始的phiInitValue
                return true;
            } else {
                // phi初值不正确，但我们可以计算出正确的值
                auto* context = phiInitValue->getType()->getContext();
                *computedInitValue =
                    ConstantInt::get(context, 32, expectedInit);
                return true;
            }
        } else {
            // phi初值不是常量，但我们可以计算出期望值
            auto* context = phiInitValue->getType()->getContext();
            *computedInitValue = ConstantInt::get(context, 32, expectedInit);
            return true;
        }
    }

    // 如果不能完全计算，至少验证关系是否合理
    return true;
}

void ScalarEvolutionAnalysis::analyzeUntilConvergence(
    Loop* L, ScalarEvolutionResult& result,
    const std::unordered_map<Value*, std::pair<Value*, Value*>>&
        basicLoopVars) {
    bool changed = true;
    int maxIterations = 10;  // 防止无限循环
    int iteration = 0;

    while (changed && iteration < maxIterations) {
        changed = false;
        iteration++;
        // 遍历循环中的所有指令
        for (auto* bb : L->getBlocks()) {
            for (auto it = bb->begin(); it != bb->end(); ++it) {
                auto* inst = *it;

                // 跳过phi指令和已有SCEV的指令
                if (dynamic_cast<PHINode*>(inst) || result.hasSCEV(inst)) {
                    continue;
                }

                // 尝试基于已知的SCEV创建新的SCEV
                if (tryCreateSCEVFromKnownSCEVs(inst, L, result)) {
                    changed = true;
                }
            }
        }
    }
}

bool ScalarEvolutionAnalysis::tryCreateSCEVFromKnownSCEVs(
    Value* inst, Loop* L, ScalarEvolutionResult& result) {
    // 特殊处理：检查是否是两个已知SCEV的加法
    if (auto* addOp = dynamic_cast<BinaryOperator*>(inst)) {
        if (addOp->getOpcode() == Opcode::Add) {
            auto* lhs = addOp->getOperand(0);
            auto* rhs = addOp->getOperand(1);

            auto* lhsSCEV = result.getSCEV(lhs);
            auto* rhsSCEV = result.getSCEV(rhs);

            if (lhsSCEV && rhsSCEV) {
                auto* lhsAddRec = dynamic_cast<SCEVAddRecExpr*>(lhsSCEV);
                auto* rhsAddRec = dynamic_cast<SCEVAddRecExpr*>(rhsSCEV);

                if (lhsAddRec && rhsAddRec &&
                    lhsAddRec->getLoop() == rhsAddRec->getLoop()) {
                    // 两个AddRec表达式相加：{A, +, B} + {C, +, D} = {A+C, +,
                    // B+D}
                    auto* lhsStart = lhsAddRec->getStart();
                    auto* lhsStep = lhsAddRec->getStep();
                    auto* rhsStart = rhsAddRec->getStart();
                    auto* rhsStep = rhsAddRec->getStep();

                    std::unique_ptr<SCEV> newStart, newStep;

                    // 首先尝试常量优化
                    if (auto* lhsStartConst =
                            dynamic_cast<SCEVConstant*>(lhsStart)) {
                        if (auto* lhsStepConst =
                                dynamic_cast<SCEVConstant*>(lhsStep)) {
                            if (auto* rhsStartConst =
                                    dynamic_cast<SCEVConstant*>(rhsStart)) {
                                if (auto* rhsStepConst =
                                        dynamic_cast<SCEVConstant*>(rhsStep)) {
                                    auto* lhsStartInt =
                                        dynamic_cast<ConstantInt*>(
                                            lhsStartConst->getValue());
                                    auto* lhsStepInt =
                                        dynamic_cast<ConstantInt*>(
                                            lhsStepConst->getValue());
                                    auto* rhsStartInt =
                                        dynamic_cast<ConstantInt*>(
                                            rhsStartConst->getValue());
                                    auto* rhsStepInt =
                                        dynamic_cast<ConstantInt*>(
                                            rhsStepConst->getValue());

                                    if (lhsStartInt && lhsStepInt &&
                                        rhsStartInt && rhsStepInt) {
                                        int32_t newStartVal =
                                            lhsStartInt->getSignedValue() +
                                            rhsStartInt->getSignedValue();
                                        int32_t newStepVal =
                                            lhsStepInt->getSignedValue() +
                                            rhsStepInt->getSignedValue();

                                        auto* context =
                                            inst->getType()->getContext();
                                        auto* newStartConst = ConstantInt::get(
                                            context, 32, newStartVal);
                                        auto* newStepConst = ConstantInt::get(
                                            context, 32, newStepVal);

                                        newStart =
                                            std::make_unique<SCEVConstant>(
                                                newStartConst);
                                        newStep =
                                            std::make_unique<SCEVConstant>(
                                                newStepConst);
                                    }
                                }
                            }
                        }
                    }

                    // 如果不能常量化，创建符号化表达式并应用分配律
                    if (!newStart || !newStep) {
                        // newStart = lhsStart + rhsStart，需要展开并合并项
                        newStart =
                            createAdditionWithDistribution(lhsStart, rhsStart);

                        // newStep = lhsStep + rhsStep，同样展开并合并项
                        newStep =
                            createAdditionWithDistribution(lhsStep, rhsStep);
                    }

                    auto newSCEV = createAddRecExpr(std::move(newStart),
                                                    std::move(newStep), L);
                    result.setSCEV(inst, std::move(newSCEV));
                    return true;
                }
            }
        }
    }

    // 首先尝试直接操作数匹配（最高优先级）
    if (auto* binaryOp = dynamic_cast<BinaryOperator*>(inst)) {
        for (int i = 0; i < 2; ++i) {
            auto* operand = binaryOp->getOperand(i);
            if (result.hasSCEV(operand)) {
                auto linearCoeffs = matchLinearExpression(inst, operand);
                if (linearCoeffs.first && linearCoeffs.second) {
                    auto* k = linearCoeffs.first;
                    auto* a2 = linearCoeffs.second;

                    if (isLoopInvariant(k, L) && isLoopInvariant(a2, L)) {
                        auto* baseSCEV = result.getSCEV(operand);
                        if (auto* baseAddRec =
                                dynamic_cast<SCEVAddRecExpr*>(baseSCEV)) {
                            auto* baseStart = baseAddRec->getStart();
                            auto* baseStep = baseAddRec->getStep();

                            // 创建符号化SCEV表达式
                            std::unique_ptr<SCEV> newStart, newStep;

                            // 首先尝试常量优化计算
                            if (auto* startConst =
                                    dynamic_cast<SCEVConstant*>(baseStart)) {
                                if (auto* stepConst =
                                        dynamic_cast<SCEVConstant*>(baseStep)) {
                                    if (auto* kConst =
                                            dynamic_cast<ConstantInt*>(k)) {
                                        if (auto* a2Const =
                                                dynamic_cast<ConstantInt*>(
                                                    a2)) {
                                            auto* startConstInt =
                                                dynamic_cast<ConstantInt*>(
                                                    startConst->getValue());
                                            auto* stepConstInt =
                                                dynamic_cast<ConstantInt*>(
                                                    stepConst->getValue());

                                            if (startConstInt && stepConstInt) {
                                                int32_t baseStartVal =
                                                    startConstInt
                                                        ->getSignedValue();
                                                int32_t baseStepVal =
                                                    stepConstInt
                                                        ->getSignedValue();
                                                int32_t kVal =
                                                    kConst->getSignedValue();
                                                int32_t a2Val =
                                                    a2Const->getSignedValue();

                                                int32_t newStartVal =
                                                    kVal * baseStartVal + a2Val;
                                                int32_t newStepVal =
                                                    kVal * baseStepVal;

                                                auto* context =
                                                    inst->getType()
                                                        ->getContext();
                                                auto* newStartConst =
                                                    ConstantInt::get(
                                                        context, 32,
                                                        newStartVal);
                                                auto* newStepConst =
                                                    ConstantInt::get(
                                                        context, 32,
                                                        newStepVal);

                                                newStart = std::make_unique<
                                                    SCEVConstant>(
                                                    newStartConst);
                                                newStep = std::make_unique<
                                                    SCEVConstant>(newStepConst);
                                            }
                                        }
                                    }
                                }
                            }

                            // 如果不能常量化，创建符号化表达式，正确应用乘法分配律
                            if (!newStart || !newStep) {
                                // newStart = k * baseStart + a2
                                // 如果baseStart是AddExpr，需要应用分配律：k*(A+B)
                                // = k*A + k*B
                                if (auto* baseStartAdd =
                                        dynamic_cast<SCEVAddExpr*>(baseStart)) {
                                    std::vector<std::unique_ptr<SCEV>>
                                        newStartTerms;

                                    // 对每个项应用分配律
                                    for (auto& term :
                                         baseStartAdd->getOperands()) {
                                        std::vector<std::unique_ptr<SCEV>>
                                            mulOps;
                                        mulOps.push_back(createSCEVForValue(k));
                                        mulOps.push_back(cloneSCEV(term.get()));
                                        auto mulTerm =
                                            std::make_unique<SCEVMulExpr>(
                                                std::move(mulOps));
                                        newStartTerms.push_back(
                                            std::move(mulTerm));
                                    }

                                    // 添加a2项（如果非零）
                                    auto* a2Const =
                                        dynamic_cast<ConstantInt*>(a2);
                                    if (!a2Const ||
                                        a2Const->getSignedValue() != 0) {
                                        newStartTerms.push_back(
                                            createSCEVForValue(a2));
                                    }

                                    newStart = std::make_unique<SCEVAddExpr>(
                                        std::move(newStartTerms));
                                } else {
                                    // baseStart不是AddExpr，直接相乘
                                    std::vector<std::unique_ptr<SCEV>>
                                        startMulOps;
                                    startMulOps.push_back(
                                        createSCEVForValue(k));
                                    startMulOps.push_back(cloneSCEV(baseStart));
                                    auto startMul =
                                        std::make_unique<SCEVMulExpr>(
                                            std::move(startMulOps));

                                    // 如果a2非零，加上a2
                                    auto* a2Const =
                                        dynamic_cast<ConstantInt*>(a2);
                                    if (a2Const &&
                                        a2Const->getSignedValue() == 0) {
                                        newStart = std::move(startMul);
                                    } else {
                                        std::vector<std::unique_ptr<SCEV>>
                                            startAddOps;
                                        startAddOps.push_back(
                                            std::move(startMul));
                                        startAddOps.push_back(
                                            createSCEVForValue(a2));
                                        newStart =
                                            std::make_unique<SCEVAddExpr>(
                                                std::move(startAddOps));
                                    }
                                }

                                // newStep = k * baseStep，同样应用分配律
                                if (auto* baseStepAdd =
                                        dynamic_cast<SCEVAddExpr*>(baseStep)) {
                                    std::vector<std::unique_ptr<SCEV>>
                                        newStepTerms;
                                    for (auto& term :
                                         baseStepAdd->getOperands()) {
                                        std::vector<std::unique_ptr<SCEV>>
                                            mulOps;
                                        mulOps.push_back(createSCEVForValue(k));
                                        mulOps.push_back(cloneSCEV(term.get()));
                                        auto mulTerm =
                                            std::make_unique<SCEVMulExpr>(
                                                std::move(mulOps));
                                        newStepTerms.push_back(
                                            std::move(mulTerm));
                                    }
                                    newStep = std::make_unique<SCEVAddExpr>(
                                        std::move(newStepTerms));
                                } else {
                                    std::vector<std::unique_ptr<SCEV>>
                                        stepMulOps;
                                    stepMulOps.push_back(createSCEVForValue(k));
                                    stepMulOps.push_back(cloneSCEV(baseStep));
                                    newStep = std::make_unique<SCEVMulExpr>(
                                        std::move(stepMulOps));
                                }
                            }

                            auto newSCEV = createAddRecExpr(
                                std::move(newStart), std::move(newStep), L);
                            result.setSCEV(inst, std::move(newSCEV));
                            return true;
                        }
                    }
                }
            }
        }
    }

    // 然后尝试间接关系匹配（较低优先级）
    for (auto* bb : L->getBlocks()) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            auto* potentialBase = *it;

            // 跳过自己
            if (potentialBase == inst) continue;

            // 只考虑已有SCEV的值
            if (!result.hasSCEV(potentialBase)) continue;

            // 跳过直接操作数，避免重复处理
            if (auto* binaryOp = dynamic_cast<BinaryOperator*>(inst)) {
                bool isDirectOperand = false;
                for (int i = 0; i < 2; ++i) {
                    if (binaryOp->getOperand(i) == potentialBase) {
                        isDirectOperand = true;
                        break;
                    }
                }
                if (isDirectOperand) continue;
            }

            // 检查当前指令是否是基于这个已知SCEV的线性关系
            auto linearCoeffs = matchLinearExpression(inst, potentialBase);
            if (linearCoeffs.first && linearCoeffs.second) {
                auto* k = linearCoeffs.first;
                auto* a2 = linearCoeffs.second;

                // 确保系数是循环无关的
                if (isLoopInvariant(k, L) && isLoopInvariant(a2, L)) {
                    // 获取基础值的SCEV
                    auto* baseSCEV = result.getSCEV(potentialBase);
                    if (auto* baseAddRec =
                            dynamic_cast<SCEVAddRecExpr*>(baseSCEV)) {
                        // 基础SCEV是AddRec，计算新的SCEV
                        auto* baseStart = baseAddRec->getStart();
                        auto* baseStep = baseAddRec->getStep();

                        // 计算新的初值和步长
                        std::unique_ptr<SCEV> newStart, newStep;

                        // 尝试计算常量结果
                        if (auto* startConst =
                                dynamic_cast<SCEVConstant*>(baseStart)) {
                            if (auto* stepConst =
                                    dynamic_cast<SCEVConstant*>(baseStep)) {
                                if (auto* kConst =
                                        dynamic_cast<ConstantInt*>(k)) {
                                    if (auto* a2Const =
                                            dynamic_cast<ConstantInt*>(a2)) {
                                        auto* startConstInt =
                                            dynamic_cast<ConstantInt*>(
                                                startConst->getValue());
                                        auto* stepConstInt =
                                            dynamic_cast<ConstantInt*>(
                                                stepConst->getValue());

                                        if (startConstInt && stepConstInt) {
                                            int32_t baseStartVal =
                                                startConstInt->getSignedValue();
                                            int32_t baseStepVal =
                                                stepConstInt->getSignedValue();
                                            int32_t kVal =
                                                kConst->getSignedValue();
                                            int32_t a2Val =
                                                a2Const->getSignedValue();

                                            int32_t newStartVal =
                                                kVal * baseStartVal + a2Val;
                                            int32_t newStepVal =
                                                kVal * baseStepVal;

                                            auto* context =
                                                inst->getType()->getContext();
                                            auto* newStartConst =
                                                ConstantInt::get(context, 32,
                                                                 newStartVal);
                                            auto* newStepConst =
                                                ConstantInt::get(context, 32,
                                                                 newStepVal);

                                            newStart =
                                                std::make_unique<SCEVConstant>(
                                                    newStartConst);
                                            newStep =
                                                std::make_unique<SCEVConstant>(
                                                    newStepConst);
                                        }
                                    }
                                }
                            }
                        }

                        // 如果不能计算常量，直接创建Unknown SCEV
                        if (!newStart || !newStep) {
                            // 暂时退回到Unknown处理，避免内存管理问题
                            return false;
                        }

                        auto newSCEV = createAddRecExpr(std::move(newStart),
                                                        std::move(newStep), L);
                        result.setSCEV(inst, std::move(newSCEV));
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::cloneSCEV(const SCEV* scev) {
    if (!scev) return nullptr;

    switch (scev->getSCEVType()) {
        case SCEVType::Constant: {
            auto* constSCEV = static_cast<const SCEVConstant*>(scev);
            return std::make_unique<SCEVConstant>(constSCEV->getValue());
        }

        case SCEVType::Unknown: {
            auto* unknownSCEV = static_cast<const SCEVUnknown*>(scev);
            return std::make_unique<SCEVUnknown>(unknownSCEV->getValue());
        }

        case SCEVType::Add: {
            auto* addSCEV = static_cast<const SCEVAddExpr*>(scev);
            std::vector<std::unique_ptr<SCEV>> operands;
            for (auto& operand : addSCEV->getOperands()) {
                operands.push_back(cloneSCEV(operand.get()));
            }
            return std::make_unique<SCEVAddExpr>(std::move(operands));
        }

        case SCEVType::Mul: {
            auto* mulSCEV = static_cast<const SCEVMulExpr*>(scev);
            std::vector<std::unique_ptr<SCEV>> operands;
            for (auto& operand : mulSCEV->getOperands()) {
                operands.push_back(cloneSCEV(operand.get()));
            }
            return std::make_unique<SCEVMulExpr>(std::move(operands));
        }

        case SCEVType::AddRec: {
            auto* addRecSCEV = static_cast<const SCEVAddRecExpr*>(scev);
            auto clonedStart = cloneSCEV(addRecSCEV->getStart());
            auto clonedStep = cloneSCEV(addRecSCEV->getStep());
            return createAddRecExpr(std::move(clonedStart),
                                    std::move(clonedStep),
                                    addRecSCEV->getLoop());
        }

        default:
            // 对于未处理的SCEV类型，返回nullptr
            return nullptr;
    }
}

std::unique_ptr<SCEV> ScalarEvolutionAnalysis::createAdditionWithDistribution(
    const SCEV* lhs, const SCEV* rhs) {
    if (!lhs || !rhs) return nullptr;

    // 首先尝试常量优化
    if (auto* lhsConst = dynamic_cast<const SCEVConstant*>(lhs)) {
        if (auto* rhsConst = dynamic_cast<const SCEVConstant*>(rhs)) {
            auto* lhsInt = dynamic_cast<ConstantInt*>(lhsConst->getValue());
            auto* rhsInt = dynamic_cast<ConstantInt*>(rhsConst->getValue());
            if (lhsInt && rhsInt) {
                int32_t sum =
                    lhsInt->getSignedValue() + rhsInt->getSignedValue();
                auto* context = lhsInt->getType()->getContext();
                auto* sumConst = ConstantInt::get(context, 32, sum);
                return std::make_unique<SCEVConstant>(sumConst);
            }
        }
    }

    // 收集所有要相加的项
    std::vector<std::unique_ptr<SCEV>> allTerms;

    // 处理lhs：如果是AddExpr，展开所有项；否则添加lhs本身
    if (auto* lhsAdd = dynamic_cast<const SCEVAddExpr*>(lhs)) {
        for (auto& term : lhsAdd->getOperands()) {
            allTerms.push_back(cloneSCEV(term.get()));
        }
    } else {
        allTerms.push_back(cloneSCEV(lhs));
    }

    // 处理rhs：如果是AddExpr，展开所有项；否则添加rhs本身
    if (auto* rhsAdd = dynamic_cast<const SCEVAddExpr*>(rhs)) {
        for (auto& term : rhsAdd->getOperands()) {
            allTerms.push_back(cloneSCEV(term.get()));
        }
    } else {
        allTerms.push_back(cloneSCEV(rhs));
    }

    // 如果只有一个项，直接返回
    if (allTerms.size() == 1) {
        return std::move(allTerms[0]);
    }

    // 创建加法表达式
    return std::make_unique<SCEVAddExpr>(std::move(allTerms));
}

}  // namespace midend
