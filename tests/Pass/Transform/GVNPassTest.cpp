#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"
#include "Pass/Transform/GVNPass.h"

using namespace midend;

class GVNPassTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();
        am->registerAnalysisType<DominanceAnalysis>();
        am->registerAnalysisType<CallGraphAnalysis>();
        am->registerAnalysisType<AliasAnalysis>();
    }

    void TearDown() override {
        // Destroy in correct order to avoid stale references
        // AnalysisManager should be destroyed before Module
        // to prevent accessing deleted objects
        am.reset();
        builder.reset();
        module.reset();
        ctx.reset();
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

// Test 1: 在同一基本块内多次出现相同算术表达式
TEST_F(GVNPassTest, SameBlockRedundantArithmetic) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto a = func->getArg(0);
    auto b = func->getArg(1);

    // Create redundant arithmetic expressions
    auto add1 = builder->createAdd(a, b, "add1");
    auto add2 = builder->createAdd(a, b, "add2");  // Redundant
    auto mul1 = builder->createMul(add1, add2, "mul1");
    builder->createRet(mul1);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %add1 = add i32 %arg0, %arg1
  %add2 = add i32 %arg0, %arg1
  %mul1 = mul i32 %add1, %add2
  ret i32 %mul1
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %add1 = add i32 %arg0, %arg1
  %mul1 = mul i32 %add1, %add1
  ret i32 %mul1
}
)");
}

// Test 2: 同一表达式使用交换律显示不同操作数顺序
TEST_F(GVNPassTest, CommutativeOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto a = func->getArg(0);
    auto b = func->getArg(1);

    // Create expressions with different operand order
    auto add1 = builder->createAdd(a, b, "add1");
    auto add2 =
        builder->createAdd(b, a, "add2");  // Should be recognized as same
    auto result = builder->createMul(add1, add2, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %add1 = add i32 %arg0, %arg1
  %add2 = add i32 %arg1, %arg0
  %result = mul i32 %add1, %add2
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %add1 = add i32 %arg0, %arg1
  %result = mul i32 %add1, %add1
  ret i32 %result
}
)");
}

// Test 3: 复制传播产生中间变量
TEST_F(GVNPassTest, CopyPropagation) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto a = func->getArg(0);
    auto one = builder->getInt32(1);

    // x = a (simulated by add 0)
    auto zero = builder->getInt32(0);
    auto x = builder->createAdd(a, zero, "x");

    // y = x + 1
    auto y = builder->createAdd(x, one, "y");

    // z = a + 1 (redundant with y)
    auto z = builder->createAdd(a, one, "z");

    auto result = builder->createAdd(y, z, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0) {
entry:
  %x = add i32 %arg0, 0
  %y = add i32 %x, 1
  %z = add i32 %arg0, 1
  %result = add i32 %y, %z
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // After GVN, z should be eliminated as it's redundant with y
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0) {
entry:
  %y = add i32 %arg0, 1
  %result = add i32 %y, %y
  ret i32 %result
}
)");
}

// Test 4: 跨基本块重复计算相同表达式
TEST_F(GVNPassTest, CrossBlockRedundancy) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto thenBB = BasicBlock::Create(ctx.get(), "then", func);
    auto elseBB = BasicBlock::Create(ctx.get(), "else", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto a = func->getArg(0);
    auto b = func->getArg(1);
    auto cond = func->getArg(2);

    // Entry block
    builder->setInsertPoint(entryBB);
    auto add1 = builder->createAdd(a, b, "add1");
    auto condBool = builder->createICmpNE(cond, builder->getInt32(0), "cond");
    builder->createCondBr(condBool, thenBB, elseBB);

    // Then block
    builder->setInsertPoint(thenBB);
    auto add2 = builder->createAdd(a, b, "add2");  // Redundant with add1
    builder->createBr(mergeBB);

    // Else block
    builder->setInsertPoint(elseBB);
    auto add3 = builder->createAdd(a, b, "add3");  // Redundant with add1
    builder->createBr(mergeBB);

    // Merge block
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(add2, thenBB);
    phi->addIncoming(add3, elseBB);
    auto result = builder->createAdd(add1, phi, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  %add1 = add i32 %arg0, %arg1
  %cond = icmp ne i32 %arg2, 0
  br i1 %cond, label %then, label %else
then:
  %add2 = add i32 %arg0, %arg1
  br label %merge
else:
  %add3 = add i32 %arg0, %arg1
  br label %merge
merge:
  %phi = phi i32 [ %add2, %then ], [ %add3, %else ]
  %result = add i32 %add1, %phi
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // After GVN, add2 and add3 should be eliminated
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  %add1 = add i32 %arg0, %arg1
  %cond = icmp ne i32 %arg2, 0
  br i1 %cond, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  %result = add i32 %add1, %add1
  ret i32 %result
}
)");
}

// Test 5: 不同分支汇合处Phi节点输入相同值
TEST_F(GVNPassTest, RedundantPHINode) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto thenBB = BasicBlock::Create(ctx.get(), "then", func);
    auto elseBB = BasicBlock::Create(ctx.get(), "else", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto a = func->getArg(0);
    auto cond = func->getArg(1);

    // Entry block
    builder->setInsertPoint(entryBB);
    auto value = builder->createAdd(a, builder->getInt32(10), "value");
    auto condBool = builder->createICmpNE(cond, builder->getInt32(0), "cond");
    builder->createCondBr(condBool, thenBB, elseBB);

    // Then block
    builder->setInsertPoint(thenBB);
    builder->createBr(mergeBB);

    // Else block
    builder->setInsertPoint(elseBB);
    builder->createBr(mergeBB);

    // Merge block - PHI with same value from both paths
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(value, thenBB);
    phi->addIncoming(value, elseBB);
    builder->createRet(phi);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %value = add i32 %arg0, 10
  %cond = icmp ne i32 %arg1, 0
  br i1 %cond, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  %phi = phi i32 [ %value, %then ], [ %value, %else ]
  ret i32 %phi
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    // After GVN, PHI should be eliminated
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %value = add i32 %arg0, 10
  %cond = icmp ne i32 %arg1, 0
  br i1 %cond, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  ret i32 %value
}
)");
}

// Test 6: 多分支合流中部分冗余表达式
TEST_F(GVNPassTest, PartialRedundancy) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto path1BB = BasicBlock::Create(ctx.get(), "path1", func);
    auto path2BB = BasicBlock::Create(ctx.get(), "path2", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto a = func->getArg(0);
    auto b = func->getArg(1);
    auto cond = func->getArg(2);

    // Entry block
    builder->setInsertPoint(entryBB);
    auto condBool = builder->createICmpNE(cond, builder->getInt32(0), "cond");
    builder->createCondBr(condBool, path1BB, path2BB);

    // Path1: compute a + b
    builder->setInsertPoint(path1BB);
    auto add1 = builder->createAdd(a, b, "add1");
    builder->createBr(mergeBB);

    // Path2: no computation
    builder->setInsertPoint(path2BB);
    builder->createBr(mergeBB);

    // Merge: always compute a + b (partially redundant)
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(add1, path1BB);
    phi->addIncoming(builder->getInt32(0), path2BB);
    auto add2 = builder->createAdd(a, b, "add2");  // Partially redundant
    auto result = builder->createAdd(phi, add2, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  %cond = icmp ne i32 %arg2, 0
  br i1 %cond, label %path1, label %path2
path1:
  %add1 = add i32 %arg0, %arg1
  br label %merge
path2:
  br label %merge
merge:
  %phi = phi i32 [ %add1, %path1 ], [ 0, %path2 ]
  %add2 = add i32 %arg0, %arg1
  %result = add i32 %phi, %add2
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);
}

// Test 8: 嵌套循环
TEST_F(GVNPassTest, NestedLoopInvariants) {
    auto intType = ctx->getIntegerType(32);
    auto funcType =
        FunctionType::get(intType, {intType, intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerLoopBB = BasicBlock::Create(ctx.get(), "outer_loop", func);
    auto innerLoopBB = BasicBlock::Create(ctx.get(), "inner_loop", func);
    auto innerExitBB = BasicBlock::Create(ctx.get(), "inner_exit", func);
    auto outerExitBB = BasicBlock::Create(ctx.get(), "outer_exit", func);

    auto a = func->getArg(0);
    auto b = func->getArg(1);
    auto n = func->getArg(2);
    auto m = func->getArg(3);

    // Entry
    builder->setInsertPoint(entryBB);
    auto zero = builder->getInt32(0);
    builder->createBr(outerLoopBB);

    // Outer loop
    builder->setInsertPoint(outerLoopBB);
    auto i = builder->createPHI(intType, "i");
    auto sumOuter = builder->createPHI(intType, "sum_outer");
    i->addIncoming(zero, entryBB);
    sumOuter->addIncoming(zero, entryBB);

    // Outer loop invariant
    auto outerInv = builder->createAdd(a, b, "outer_inv");
    builder->createBr(innerLoopBB);

    // Inner loop
    builder->setInsertPoint(innerLoopBB);
    auto j = builder->createPHI(intType, "j");
    auto sumInner = builder->createPHI(intType, "sum_inner");
    j->addIncoming(zero, outerLoopBB);
    sumInner->addIncoming(sumOuter, outerLoopBB);

    // Same expression as outer (should be recognized as redundant)
    auto innerInv1 = builder->createAdd(a, b, "inner_inv1");
    // Inner loop invariant depending on outer loop variable
    auto innerInv2 = builder->createMul(outerInv, i, "inner_inv2");

    auto temp = builder->createAdd(innerInv1, innerInv2, "temp");
    auto newSumInner = builder->createAdd(sumInner, temp, "new_sum_inner");

    auto one = builder->getInt32(1);
    auto nextJ = builder->createAdd(j, one, "next_j");
    auto innerCond = builder->createICmpSLT(nextJ, m, "inner_cond");

    j->addIncoming(nextJ, innerLoopBB);
    sumInner->addIncoming(newSumInner, innerLoopBB);

    builder->createCondBr(innerCond, innerLoopBB, innerExitBB);

    // Inner exit
    builder->setInsertPoint(innerExitBB);
    auto nextI = builder->createAdd(i, one, "next_i");
    auto outerCond = builder->createICmpSLT(nextI, n, "outer_cond");

    i->addIncoming(nextI, innerExitBB);
    sumOuter->addIncoming(sumInner, innerExitBB);

    builder->createCondBr(outerCond, outerLoopBB, outerExitBB);

    // Outer exit
    builder->setInsertPoint(outerExitBB);
    builder->createRet(sumOuter);

    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  br label %outer_loop
outer_loop:
  %i = phi i32 [ 0, %entry ], [ %next_i, %inner_exit ]
  %sum_outer = phi i32 [ 0, %entry ], [ %sum_inner, %inner_exit ]
  %outer_inv = add i32 %arg0, %arg1
  br label %inner_loop
inner_loop:
  %j = phi i32 [ 0, %outer_loop ], [ %next_j, %inner_loop ]
  %sum_inner = phi i32 [ %sum_outer, %outer_loop ], [ %new_sum_inner, %inner_loop ]
  %inner_inv1 = add i32 %arg0, %arg1
  %inner_inv2 = mul i32 %outer_inv, %i
  %temp = add i32 %inner_inv1, %inner_inv2
  %new_sum_inner = add i32 %sum_inner, %temp
  %next_j = add i32 %j, 1
  %inner_cond = icmp slt i32 %next_j, %arg3
  br i1 %inner_cond, label %inner_loop, label %inner_exit
inner_exit:
  %next_i = add i32 %i, 1
  %outer_cond = icmp slt i32 %next_i, %arg2
  br i1 %outer_cond, label %outer_loop, label %outer_exit
outer_exit:
  ret i32 %sum_outer
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  br label %outer_loop
outer_loop:
  %i = phi i32 [ 0, %entry ], [ %next_i, %inner_exit ]
  %sum_outer = phi i32 [ 0, %entry ], [ %sum_inner, %inner_exit ]
  %outer_inv = add i32 %arg0, %arg1
  br label %inner_loop
inner_loop:
  %j = phi i32 [ 0, %outer_loop ], [ %next_j, %inner_loop ]
  %sum_inner = phi i32 [ %sum_outer, %outer_loop ], [ %new_sum_inner, %inner_loop ]
  %inner_inv2 = mul i32 %outer_inv, %i
  %temp = add i32 %outer_inv, %inner_inv2
  %new_sum_inner = add i32 %sum_inner, %temp
  %next_j = add i32 %j, 1
  %inner_cond = icmp slt i32 %next_j, %arg3
  br i1 %inner_cond, label %inner_loop, label %inner_exit
inner_exit:
  %next_i = add i32 %i, 1
  %outer_cond = icmp slt i32 %next_i, %arg2
  br i1 %outer_cond, label %outer_loop, label %outer_exit
outer_exit:
  ret i32 %sum_outer
}
)");
}

// Test 9: 纯函数调用多次出现且参数相同
TEST_F(GVNPassTest, PureFunctionCalls) {
    auto intType = ctx->getIntegerType(32);

    // Create a pure function
    auto pureFuncType = FunctionType::get(intType, {intType});
    auto pureFunc = Function::Create(pureFuncType, "pure_func", module.get());
    auto pureBB = BasicBlock::Create(ctx.get(), "entry", pureFunc);
    builder->setInsertPoint(pureBB);
    auto param = pureFunc->getArg(0);
    auto doubled = builder->createMul(param, builder->getInt32(2), "doubled");
    builder->createRet(doubled);

    // Create main function
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto a = func->getArg(0);

    // Multiple calls to pure function with same argument
    auto call1 = builder->createCall(pureFunc, {a}, "call1");
    auto call2 = builder->createCall(pureFunc, {a}, "call2");  // Redundant
    auto call3 = builder->createCall(pureFunc, {a}, "call3");  // Redundant

    auto sum1 = builder->createAdd(call1, call2, "sum1");
    auto sum2 = builder->createAdd(sum1, call3, "sum2");
    builder->createRet(sum2);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0) {
entry:
  %call1 = call i32 @pure_func(i32 %arg0)
  %call2 = call i32 @pure_func(i32 %arg0)
  %call3 = call i32 @pure_func(i32 %arg0)
  %sum1 = add i32 %call1, %call2
  %sum2 = add i32 %sum1, %call3
  ret i32 %sum2
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %call1 = call i32 @pure_func(i32 %arg0)
  %sum1 = add i32 %call1, %call1
  %sum2 = add i32 %sum1, %call1
  ret i32 %sum2
}
)");
}

// Test 10: 带副作用的函数调用
TEST_F(GVNPassTest, TestSideEffectCalls) {
    auto intType = ctx->getIntegerType(32);
    auto voidType = ctx->getVoidType();

    auto gv = GlobalVariable::Create(
        intType, true, midend::GlobalVariable::InternalLinkage,
        builder->getInt32(0), "global_var", module.get());

    // Create a function with side effects
    auto sideEffectFuncType = FunctionType::get(voidType, {intType});
    auto sideEffectFunc =
        Function::Create(sideEffectFuncType, "side_effect_func", module.get());

    auto sideEffectBB = BasicBlock::Create(ctx.get(), "entry", sideEffectFunc);
    builder->setInsertPoint(sideEffectBB);
    builder->createStore(builder->getInt32(42), gv);
    builder->createRetVoid();

    // Create main function
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto a = func->getArg(0);

    // Multiple calls to function with side effects
    builder->createCall(sideEffectFunc, {a});
    builder->createCall(sideEffectFunc, {a});  // Should NOT be eliminated
    builder->createCall(sideEffectFunc, {a});  // Should NOT be eliminated

    builder->createRet(a);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0) {
entry:
  call void @side_effect_func(i32 %arg0)
  call void @side_effect_func(i32 %arg0)
  call void @side_effect_func(i32 %arg0)
  ret i32 %arg0
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    // Side effect calls should not be eliminated
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0) {
entry:
  call void @side_effect_func(i32 %arg0)
  call void @side_effect_func(i32 %arg0)
  call void @side_effect_func(i32 %arg0)
  ret i32 %arg0
}
)");
}

// Test 11: 对同一指针地址在不同位置加载多次
TEST_F(GVNPassTest, RedundantLoads) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = ctx->getPointerType(intType);
    auto funcType = FunctionType::get(intType, {ptrType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto ptr = func->getArg(0);

    // Multiple loads from same address
    auto load1 = builder->createLoad(ptr, "load1");
    auto load2 = builder->createLoad(ptr, "load2");  // Redundant
    auto load3 = builder->createLoad(ptr, "load3");  // Redundant

    auto sum1 = builder->createAdd(load1, load2, "sum1");
    auto sum2 = builder->createAdd(sum1, load3, "sum2");
    builder->createRet(sum2);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0) {
entry:
  %load1 = load i32, i32* %arg0
  %load2 = load i32, i32* %arg0
  %load3 = load i32, i32* %arg0
  %sum1 = add i32 %load1, %load2
  %sum2 = add i32 %sum1, %load3
  ret i32 %sum2
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0) {
entry:
  %load1 = load i32, i32* %arg0
  %sum1 = add i32 %load1, %load1
  %sum2 = add i32 %sum1, %load1
  ret i32 %sum2
}
)");
}

// Test 12: 浮点运算因舍入或 NaN/Inf 风险不得随意交换
TEST_F(GVNPassTest, FloatingPointConservatism) {
    auto floatType = ctx->getFloatType();
    auto funcType = FunctionType::get(floatType, {floatType, floatType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto a = func->getArg(0);
    auto b = func->getArg(1);

    // Floating point operations
    auto fadd1 = builder->createFAdd(a, b, "fadd1");
    auto fadd2 = builder->createFAdd(
        a, b, "fadd2");  // May not be eliminated due to FP semantics
    auto fadd3 = builder->createFAdd(
        b, a, "fadd3");  // Different order, may not be considered same

    auto sum1 = builder->createFAdd(fadd1, fadd2, "sum1");
    auto sum2 = builder->createFAdd(sum1, fadd3, "sum2");
    builder->createRet(sum2);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define float @test_func(float %arg0, float %arg1) {
entry:
  %fadd1 = fadd float %arg0, %arg1
  %fadd2 = fadd float %arg0, %arg1
  %fadd3 = fadd float %arg1, %arg0
  %sum1 = fadd float %fadd1, %fadd2
  %sum2 = fadd float %sum1, %fadd3
  ret float %sum2
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define float @test_func(float %arg0, float %arg1) {
entry:
  %fadd1 = fadd float %arg0, %arg1
  %fadd3 = fadd float %arg1, %arg0
  %sum1 = fadd float %fadd1, %fadd1
  %sum2 = fadd float %sum1, %fadd3
  ret float %sum2
}
)");
}

// Test 13: 不同数据类型对同一地址加载测试
TEST_F(GVNPassTest, TypeSafetyLoads) {
    auto intType = ctx->getIntegerType(32);
    auto floatType = ctx->getFloatType();
    auto intPtrType = ctx->getPointerType(intType);
    auto floatPtrType = ctx->getPointerType(floatType);
    auto funcType = FunctionType::get(intType, {intPtrType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto intPtr = func->getArg(0);

    // Cast to float pointer
    auto floatPtr =
        builder->createCast(CastInst::SIToFP, intPtr, floatPtrType, "floatPtr");

    // Load as different types
    auto loadInt = builder->createLoad(intPtr, "loadInt");
    auto loadFloat = builder->createLoad(floatPtr, "loadFloat");

    // Cast float to int for addition
    auto floatAsInt =
        builder->createCast(CastInst::FPToSI, loadFloat, intType, "floatAsInt");
    auto result = builder->createAdd(loadInt, floatAsInt, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32* %arg0) {
entry:
  %floatPtr = sitofp i32* %arg0 to float*
  %loadInt = load i32, i32* %arg0
  %loadFloat = load float, float* %floatPtr
  %floatAsInt = fptosi float %loadFloat to i32
  %result = add i32 %loadInt, %floatAsInt
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    // GVN should NOT merge loads of different types
    EXPECT_FALSE(changed);
}

// Test 17: 支配树不覆盖的路径上冗余识别
TEST_F(GVNPassTest, NonDominatingPaths) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto path1BB = BasicBlock::Create(ctx.get(), "path1", func);
    auto path2BB = BasicBlock::Create(ctx.get(), "path2", func);
    auto path3BB = BasicBlock::Create(ctx.get(), "path3", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto a = func->getArg(0);
    auto b = func->getArg(1);
    auto selector = func->getArg(2);

    // Entry - three-way branch using conditionals
    builder->setInsertPoint(entryBB);
    auto check1BB = BasicBlock::Create(ctx.get(), "check1", func);
    auto check2BB = BasicBlock::Create(ctx.get(), "check2", func);

    // Check if selector == 1
    auto cmp1 = builder->createICmpEQ(selector, builder->getInt32(1), "cmp1");
    builder->createCondBr(cmp1, path1BB, check1BB);

    // Check if selector == 2
    builder->setInsertPoint(check1BB);
    auto cmp2 = builder->createICmpEQ(selector, builder->getInt32(2), "cmp2");
    builder->createCondBr(cmp2, path2BB, check2BB);

    // Check if selector == 3
    builder->setInsertPoint(check2BB);
    auto cmp3 = builder->createICmpEQ(selector, builder->getInt32(3), "cmp3");
    builder->createCondBr(cmp3, path3BB, mergeBB);

    // Path1: compute a + b
    builder->setInsertPoint(path1BB);
    auto add1 = builder->createAdd(a, b, "add1");
    builder->createBr(mergeBB);

    // Path2: compute a + b
    builder->setInsertPoint(path2BB);
    auto add2 = builder->createAdd(a, b, "add2");
    builder->createBr(mergeBB);

    // Path3: no computation
    builder->setInsertPoint(path3BB);
    builder->createBr(mergeBB);

    // Merge: expression is available on some paths but not all
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(add1, path1BB);
    phi->addIncoming(add2, path2BB);
    phi->addIncoming(builder->getInt32(0), path3BB);
    phi->addIncoming(builder->getInt32(0), entryBB);  // Default case

    // This should NOT be eliminated because not available on all paths
    auto add3 = builder->createAdd(a, b, "add3");
    auto result = builder->createAdd(phi, add3, "result");
    builder->createRet(result);

    auto beforeIR = IRPrinter().print(func);

    EXPECT_EQ(beforeIR,
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  %cmp1 = icmp eq i32 %arg2, 1
  br i1 %cmp1, label %path1, label %check1
path1:
  %add1 = add i32 %arg0, %arg1
  br label %merge
path2:
  %add2 = add i32 %arg0, %arg1
  br label %merge
path3:
  br label %merge
merge:
  %phi = phi i32 [ %add1, %path1 ], [ %add2, %path2 ], [ 0, %path3 ], [ 0, %entry ]
  %add3 = add i32 %arg0, %arg1
  %result = add i32 %phi, %add3
  ret i32 %result
check1:
  %cmp2 = icmp eq i32 %arg2, 2
  br i1 %cmp2, label %path2, label %check2
check2:
  %cmp3 = icmp eq i32 %arg2, 3
  br i1 %cmp3, label %path3, label %merge
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

// Test 18: Load after store with aliasing
TEST_F(GVNPassTest, LoadStoreAliasing) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = ctx->getPointerType(intType);
    auto funcType = FunctionType::get(intType, {ptrType, ptrType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto ptr1 = func->getArg(0);
    auto ptr2 = func->getArg(1);

    // Load from ptr1
    auto load1 = builder->createLoad(ptr1, "load1");

    // Store to ptr2 (may alias with ptr1)
    builder->createStore(builder->getInt32(42), ptr2);

    // Load from ptr1 again (may not be redundant if ptr1 aliases ptr2)
    auto load2 = builder->createLoad(ptr1, "load2");

    auto result = builder->createAdd(load1, load2, "result");
    builder->createRet(result);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_func(i32* %arg0, i32* %arg1) {
entry:
  %load1 = load i32, i32* %arg0
  store i32 42, i32* %arg1
  %load2 = load i32, i32* %arg0
  %result = add i32 %load1, %load2
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;

    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(GVNPassTest, LoadStoreAliasing2) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = ctx->getPointerType(intType);
    auto funcType = FunctionType::get(intType, {ptrType, ptrType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto ptr1 = func->getArg(0);

    // Load from ptr1
    auto load1 = builder->createLoad(ptr1, "load1");

    // Store to ptr2 (may alias with ptr1)
    builder->createStore(builder->getInt32(42), ptr1);

    // Load from ptr1 again (may not be redundant if ptr1 aliases ptr2)
    auto load2 = builder->createLoad(ptr1, "load2");

    auto result = builder->createAdd(load1, load2, "result");
    builder->createRet(result);

    auto beforeIR = IRPrinter().print(func);

    EXPECT_EQ(beforeIR,
              R"(define i32 @test_func(i32* %arg0, i32* %arg1) {
entry:
  %load1 = load i32, i32* %arg0
  store i32 42, i32* %arg0
  %load2 = load i32, i32* %arg0
  %result = add i32 %load1, %load2
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(GVNPassTest, LoadStoreAliasing3) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto ptr1 = builder->createAlloca(intType, nullptr, "alloca1");
    auto ptr2 = builder->createAlloca(intType, nullptr, "alloca2");

    // Load from ptr1
    auto load1 = builder->createLoad(ptr1, "load1");

    // Store to ptr2 (may alias with ptr1)
    builder->createStore(builder->getInt32(42), ptr1);

    // Load from ptr1 again (may not be redundant if ptr1 aliases ptr2)
    auto load2 = builder->createLoad(ptr1, "load2");

    auto result = builder->createAdd(load1, load2, "result");
    builder->createRet(result);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR, R"(define i32 @test_func() {
entry:
  %alloca1 = alloca i32
  %alloca2 = alloca i32
  %load1 = load i32, i32* %alloca1
  store i32 42, i32* %alloca1
  %load2 = load i32, i32* %alloca1
  %result = add i32 %load1, %load2
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(GVNPassTest, LoadStoreAliasing4) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto ptr1 = builder->createAlloca(intType, nullptr, "alloca1");
    auto ptr2 = builder->createAlloca(intType, nullptr, "alloca2");

    // Load from ptr1
    auto load1 = builder->createLoad(ptr1, "load1");

    // Store to ptr2 (may alias with ptr1)
    builder->createStore(builder->getInt32(42), ptr2);

    // Load from ptr1 again (may not be redundant if ptr1 aliases ptr2)
    auto load2 = builder->createLoad(ptr1, "load2");

    auto result = builder->createAdd(load1, load2, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func() {
entry:
  %alloca1 = alloca i32
  %alloca2 = alloca i32
  %load1 = load i32, i32* %alloca1
  store i32 42, i32* %alloca2
  %load2 = load i32, i32* %alloca1
  %result = add i32 %load1, %load2
  ret i32 %result
}
)");

    // Run GVN Pass
    GVNPass gvn;
    bool changed = gvn.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func() {
entry:
  %alloca1 = alloca i32
  %alloca2 = alloca i32
  %load1 = load i32, i32* %alloca1
  store i32 42, i32* %alloca2
  %result = add i32 %load1, %load1
  ret i32 %result
}
)");
}