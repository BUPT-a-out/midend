#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Pass/Pass.h"

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "IR/Constant.h"
#include "IR/Type.h"
#include "IR/Value.h"

namespace midend {

class Loop;
class Function;
class BasicBlock;
class Value;
class LoopInfo;
class Instruction;

/// SCEV表达式类型枚举
enum class SCEVType {
    Constant,    /// 常量表达式
    Unknown,     /// 未知表达式
    Add,         /// 加法表达式
    Mul,         /// 乘法表达式
    AddRec,      /// 循环递推表达式
    Truncate,    /// 截断表达式
    ZeroExtend,  /// 零扩展表达式
    SignExtend   /// 符号扩展表达式
};

/// SCEV表达式基类
class SCEV : public Value {
   protected:
    SCEVType scevType_;

   public:
    SCEV(Type* ty, SCEVType type)
        : Value(ty, ValueKind::SCEV), scevType_(type) {}
    virtual ~SCEV() = default;

    SCEVType getSCEVType() const { return scevType_; }

    /// 获取表达式的复杂度（用于排序）
    virtual unsigned getExpressionSize() const = 0;

    /// 检查表达式是否包含循环
    virtual bool hasLoop() const = 0;

    /// 获取表达式中的所有循环
    virtual std::vector<Loop*> getLoops() const = 0;

    /// 检查表达式是否为常量
    virtual bool isConstant() const = 0;

    /// 获取表达式的字符串表示
    virtual std::string toString() const override = 0;

    /// 检查两个SCEV是否相等
    virtual bool equals(const SCEV* other) const = 0;

    static bool classof(const Value* V) {
        return V->getValueKind() == ValueKind::SCEV;
    }
};

/// SCEV常量表达式
class SCEVConstant : public SCEV {
   private:
    Constant* value_;

   public:
    explicit SCEVConstant(Constant* val);
    ~SCEVConstant() override = default;

    Constant* getValue() const { return value_; }

    unsigned getExpressionSize() const override { return 1; }
    bool hasLoop() const override { return false; }
    std::vector<Loop*> getLoops() const override { return {}; }
    bool isConstant() const override { return true; }
    std::string toString() const override;
    bool equals(const SCEV* other) const override;

    static bool classof(const SCEV* S) {
        return S->getSCEVType() == SCEVType::Constant;
    }
};

/// SCEV未知表达式（对应IR中的值）
class SCEVUnknown : public SCEV {
   private:
    Value* value_;

   public:
    explicit SCEVUnknown(Value* val);
    ~SCEVUnknown() override = default;

    Value* getValue() const { return value_; }

    unsigned getExpressionSize() const override { return 1; }
    bool hasLoop() const override { return false; }
    std::vector<Loop*> getLoops() const override { return {}; }
    bool isConstant() const override { return false; }
    std::string toString() const override;
    bool equals(const SCEV* other) const override;

    static bool classof(const SCEV* S) {
        return S->getSCEVType() == SCEVType::Unknown;
    }
};

/// SCEV加法表达式
class SCEVAddExpr : public SCEV {
   private:
    std::vector<std::unique_ptr<SCEV>> operands_;

   public:
    explicit SCEVAddExpr(std::vector<std::unique_ptr<SCEV>> ops);
    ~SCEVAddExpr() override = default;

    const std::vector<std::unique_ptr<SCEV>>& getOperands() const {
        return operands_;
    }

    unsigned getExpressionSize() const override;
    bool hasLoop() const override;
    std::vector<Loop*> getLoops() const override;
    bool isConstant() const override;
    std::string toString() const override;
    bool equals(const SCEV* other) const override;

    static bool classof(const SCEV* S) {
        return S->getSCEVType() == SCEVType::Add;
    }
};

/// SCEV乘法表达式
class SCEVMulExpr : public SCEV {
   private:
    std::vector<std::unique_ptr<SCEV>> operands_;

   public:
    explicit SCEVMulExpr(std::vector<std::unique_ptr<SCEV>> ops);
    ~SCEVMulExpr() override = default;

    const std::vector<std::unique_ptr<SCEV>>& getOperands() const {
        return operands_;
    }

    unsigned getExpressionSize() const override;
    bool hasLoop() const override;
    std::vector<Loop*> getLoops() const override;
    bool isConstant() const override;
    std::string toString() const override;
    bool equals(const SCEV* other) const override;

    static bool classof(const SCEV* S) {
        return S->getSCEVType() == SCEVType::Mul;
    }
};

/// SCEV循环递推表达式 (a + b * n)
class SCEVAddRecExpr : public SCEV {
   private:
    std::unique_ptr<SCEV> start_;
    std::unique_ptr<SCEV> step_;
    Loop* loop_;

   public:
    SCEVAddRecExpr(std::unique_ptr<SCEV> start, std::unique_ptr<SCEV> step,
                   Loop* loop);
    ~SCEVAddRecExpr() override = default;

    SCEV* getStart() const { return start_.get(); }
    SCEV* getStep() const { return step_.get(); }
    Loop* getLoop() const { return loop_; }

    unsigned getExpressionSize() const override;
    bool hasLoop() const override { return true; }
    std::vector<Loop*> getLoops() const override;
    bool isConstant() const override;
    std::string toString() const override;
    bool equals(const SCEV* other) const override;

    static bool classof(const SCEV* S) {
        return S->getSCEVType() == SCEVType::AddRec;
    }
};

namespace SCEVUtils {

/// 检查值是否在循环中
bool isInLoop(Value* V, Loop* L);

/// 检查指令是否为循环不变量
bool isLoopInvariant(Instruction* I, Loop* L);

/// 检查值是否为循环不变量
bool isLoopInvariant(Value* V, Loop* L);

/// 获取循环的迭代次数（如果可计算）
std::unique_ptr<SCEV> getLoopTripCount(Loop* L);

/// 检查循环是否可数
bool isCountableLoop(Loop* L);

/// 获取循环的步长
std::unique_ptr<SCEV> getLoopStep(Loop* L);

/// 获取循环的起始值
std::unique_ptr<SCEV> getLoopStart(Loop* L);

/// 检查SCEV表达式是否为线性
bool isLinear(const SCEV* scev);

/// 检查SCEV表达式是否为多项式
bool isPolynomial(const SCEV* scev);

/// 获取SCEV表达式的度数
unsigned getPolynomialDegree(const SCEV* scev);

/// 检查两个SCEV是否相等
bool areEqual(const SCEV* A, const SCEV* B);

/// 检查SCEV是否为零
bool isZero(const SCEV* scev);

/// 检查SCEV是否为一
bool isOne(const SCEV* scev);

/// 检查SCEV是否为常量
bool isConstant(const SCEV* scev);

/// 获取SCEV的常量值（如果为常量）
int64_t getConstantValue(const SCEV* scev);

/// 创建SCEV加法表达式
std::unique_ptr<SCEV> createAddExpr(
    std::vector<std::unique_ptr<SCEV>> operands);

/// 创建SCEV乘法表达式
std::unique_ptr<SCEV> createMulExpr(
    std::vector<std::unique_ptr<SCEV>> operands);

/// 创建SCEV常量表达式
std::unique_ptr<SCEV> createConstantExpr(int64_t value, Type* type);

/// 创建SCEV未知表达式
std::unique_ptr<SCEV> createUnknownExpr(Value* value);

/// 创建SCEV循环递推表达式
std::unique_ptr<SCEV> createAddRecExpr(std::unique_ptr<SCEV> start,
                                       std::unique_ptr<SCEV> step, Loop* loop);

/// 简化SCEV表达式
std::unique_ptr<SCEV> simplifySCEV(std::unique_ptr<SCEV> scev);

/// 规范化SCEV表达式
std::unique_ptr<SCEV> canonicalizeSCEV(std::unique_ptr<SCEV> scev);

/// 检查SCEV表达式是否包含循环
bool containsLoop(const SCEV* scev, Loop* L);

/// 获取SCEV表达式中包含的所有循环
std::vector<Loop*> getLoopsInSCEV(const SCEV* scev);

/// 替换SCEV表达式中的循环
std::unique_ptr<SCEV> replaceLoopInSCEV(const SCEV* scev, Loop* oldLoop,
                                        Loop* newLoop);

/// 计算SCEV表达式在给定迭代次数下的值
std::unique_ptr<SCEV> evaluateAtIteration(const SCEV* scev, unsigned iteration);

/// 检查SCEV表达式是否单调递增
bool isMonotonicIncreasing(const SCEV* scev, Loop* L);

/// 检查SCEV表达式是否单调递减
bool isMonotonicDecreasing(const SCEV* scev, Loop* L);

/// 获取SCEV表达式的范围
std::pair<std::unique_ptr<SCEV>, std::unique_ptr<SCEV>> getSCEVRange(
    const SCEV* scev, Loop* L);

}  // namespace SCEVUtils

/// SCEV分析结果类
class ScalarEvolutionResult : public AnalysisResult {
   private:
    std::unordered_map<Value*, std::unique_ptr<SCEV>> scevCache_;
    std::unordered_map<Loop*, std::vector<std::unique_ptr<SCEV>>>
        loopScevCache_;

   public:
    ScalarEvolutionResult() = default;
    ~ScalarEvolutionResult() override = default;

    /// 获取值的SCEV表达式
    SCEV* getSCEV(Value* V);

    /// 设置值的SCEV表达式
    void setSCEV(Value* V, std::unique_ptr<SCEV> scev);

    /// 获取循环中所有SCEV表达式
    const std::vector<std::unique_ptr<SCEV>>& getLoopSCEVs(Loop* L) const;

    /// 添加循环的SCEV表达式
    void addLoopSCEV(Loop* L, std::unique_ptr<SCEV> scev);

    /// 检查是否已有值的SCEV
    bool hasSCEV(Value* V) const;

    /// 清除缓存
    void clear();
};

/// SCEV分析Pass
class ScalarEvolutionAnalysis : public Analysis {
   private:
    LoopInfo* loopInfo_;

   public:
    ScalarEvolutionAnalysis() : loopInfo_(nullptr) {}
    ~ScalarEvolutionAnalysis() override = default;

    /// 运行SCEV分析
    std::unique_ptr<AnalysisResult> runOnFunction(Function& f,
                                                  AnalysisManager& AM) override;

    /// 支持函数级分析
    bool supportsFunction() const override { return true; }

    /// 获取依赖的分析
    std::vector<std::string> getDependencies() const override;

    /// 获取分析名称
    static std::string getName() { return "scalar-evolution"; }

   private:
    /// 为值创建SCEV表达式
    std::unique_ptr<SCEV> createSCEV(Value* V, ScalarEvolutionResult& result);

    /// 为指令创建SCEV表达式
    std::unique_ptr<SCEV> createSCEVForInstruction(
        Instruction* I, ScalarEvolutionResult& result);

    /// 为循环创建SCEV表达式
    std::unique_ptr<SCEV> createSCEVForLoop(Loop* L,
                                            ScalarEvolutionResult& result);

    /// 分析循环中的标量演化
    void analyzeLoopSCEVs(Loop* L, ScalarEvolutionResult& result);

    /// 简化SCEV表达式
    std::unique_ptr<SCEV> simplifySCEV(std::unique_ptr<SCEV> scev);

    /// 检查表达式是否可以简化
    bool canSimplify(const SCEV* scev) const;

    /// 合并加法表达式
    std::unique_ptr<SCEV> combineAddExprs(
        const std::vector<std::unique_ptr<SCEV>>& operands);

    /// 合并乘法表达式
    std::unique_ptr<SCEV> combineMulExprs(
        const std::vector<std::unique_ptr<SCEV>>& operands);

    /// 创建循环递推表达式
    std::unique_ptr<SCEV> createAddRecExpr(std::unique_ptr<SCEV> start,
                                           std::unique_ptr<SCEV> step,
                                           Loop* loop);

    /// 分析phi指令，识别基础循环变量
    void analyzePhiInstructions(
        Loop* L, ScalarEvolutionResult& result,
        std::unordered_map<Value*, std::pair<Value*, Value*>>& basicLoopVars);

    /// 判断一个值是否是循环无关变量
    bool isLoopInvariant(Value* V, Loop* L);

    /// 分析线性表达式
    void analyzeLinearExpressions(
        Loop* L, ScalarEvolutionResult& result,
        const std::unordered_map<Value*, std::pair<Value*, Value*>>&
            basicLoopVars);

    /// 检查是否为基础循环变量
    bool isBasicLoopVariable(
        Value* V, const std::unordered_map<Value*, std::pair<Value*, Value*>>&
                      basicLoopVars);

    /// 尝试匹配线性表达式 k*base + a2
    std::pair<Value*, Value*> matchLinearExpression(Value* V, Value* baseVar);

    /// 为值创建正确的SCEV（常量或Unknown）
    std::unique_ptr<SCEV> createSCEVForValue(Value* V);

    /// 多轮分析直到收敛
    void analyzeUntilConvergence(
        Loop* L, ScalarEvolutionResult& result,
        const std::unordered_map<Value*, std::pair<Value*, Value*>>&
            basicLoopVars);

    /// 验证并计算依赖型phi的正确初值
    bool validateAndComputePhiInitValue(
        Value* phiInitValue, Value* loopValue, Value* baseVar,
        const std::pair<Value*, Value*>& baseVarInfo,
        Value** computedInitValue);

    /// 尝试基于已知的SCEV创建新SCEV
    bool tryCreateSCEVFromKnownSCEVs(Value* inst, Loop* L,
                                     ScalarEvolutionResult& result);

    /// 克隆SCEV表达式
    std::unique_ptr<SCEV> cloneSCEV(const SCEV* scev);

    /// 创建SCEV加法并应用分配律
    std::unique_ptr<SCEV> createAdditionWithDistribution(const SCEV* lhs,
                                                         const SCEV* rhs);
};

}  // namespace midend
