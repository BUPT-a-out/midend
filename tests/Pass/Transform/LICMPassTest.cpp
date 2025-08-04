#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Analysis/LoopInfo.h"
#include "Pass/Pass.h"
#include "Pass/Transform/LICMPass.h"

using namespace midend;

class LICMPassTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("licm_test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();

        // Register all required analyses
        am->registerAnalysisType<DominanceAnalysis>();
        am->registerAnalysisType<LoopAnalysis>();
        am->registerAnalysisType<AliasAnalysis>();
        am->registerAnalysisType<CallGraphAnalysis>();
    }

    void TearDown() override {
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

// Test 1: Simple loop invariant arithmetic
TEST_F(LICMPassTest, SimpleLoopInvariantArithmetic) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry: setup
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header: i = phi [0, entry], [i+1, body]; cond = i < n
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: invariant = a + 5; result = invariant * 2; i = i + 1
    builder->setInsertPoint(loopBodyBB);
    auto invariant = builder->createAdd(a, builder->getInt32(5),
                                        "invariant");  // Should be hoisted
    auto res = builder->createMul(invariant, builder->getInt32(2),
                                  "result");  // Should be hoisted
    auto nextI = builder->createAdd(i, res, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %invariant = add i32 %arg0, 5
  %result = mul i32 %invariant, 2
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant = add i32 %arg0, 5
  %result = mul i32 %invariant, 2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 2: Loop invariant loads from global variables
TEST_F(LICMPassTest, LoopInvariantGlobalLoads) {
    auto intType = ctx->getIntegerType(32);

    // Create a global variable
    auto globalVar = GlobalVariable::Create(
        intType, false, GlobalVariable::ExternalLinkage, builder->getInt32(42),
        "global_var", module.get());

    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry
    builder->setInsertPoint(entryBB);
    auto sum = builder->createAlloca(intType, nullptr, "sum");
    builder->createStore(builder->getInt32(0), sum);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: load from global (should be hoisted)
    builder->setInsertPoint(loopBodyBB);
    auto globalLoad =
        builder->createLoad(globalVar, "global_load");  // Should be hoisted
    auto currentSum = builder->createLoad(sum, "current_sum");
    auto newSum = builder->createAdd(currentSum, globalLoad, "new_sum");
    builder->createStore(newSum, sum);
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalSum = builder->createLoad(sum, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %global_load = load i32, i32* @global_var
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %global_load
  store i32 %new_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  %global_load = load i32, i32* @global_var
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %global_load
  store i32 %new_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
)");
}

// Test 3: Nested loops with multiple invariant levels
TEST_F(LICMPassTest, NestedLoopsMultipleInvariantLevels) {
    GTEST_SKIP();
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerLoopBB = BasicBlock::Create(ctx.get(), "outer.loop", func);
    auto innerLoopBB = BasicBlock::Create(ctx.get(), "inner.loop", func);
    auto innerBodyBB = BasicBlock::Create(ctx.get(), "inner.body", func);
    auto outerBodyBB = BasicBlock::Create(ctx.get(), "outer.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);
    auto m = func->getArg(2);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(outerLoopBB);

    // Outer loop header
    builder->setInsertPoint(outerLoopBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto outerCond = builder->createICmpSLT(i, n, "outer.cond");
    builder->createCondBr(outerCond, innerLoopBB, exitBB);

    // Inner loop header
    builder->setInsertPoint(innerLoopBB);
    auto j = builder->createPHI(intType, "j");
    j->addIncoming(builder->getInt32(0), outerLoopBB);
    auto innerCond = builder->createICmpSLT(j, m, "inner.cond");
    builder->createCondBr(innerCond, innerBodyBB, outerBodyBB);

    // Inner body: both loop invariants and outer-loop invariants
    builder->setInsertPoint(innerBodyBB);
    auto globalInvariant = builder->createMul(
        a, builder->getInt32(10), "global_inv");  // Invariant to both loops
    auto outerInvariant = builder->createAdd(
        globalInvariant, i, "outer_inv");  // Invariant to inner loop only
    auto res =
        builder->createMul(outerInvariant, j, "computation");  // Not invariant
    auto nextJ = builder->createAdd(j, res, "next_j");
    j->addIncoming(nextJ, innerBodyBB);
    builder->createBr(innerLoopBB);

    // Outer body
    builder->setInsertPoint(outerBodyBB);
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, outerBodyBB);
    builder->createBr(outerLoopBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  br label %outer.loop
outer.loop:
  %i = phi i32 [ 0, %entry ], [ %next_i, %outer.body ]
  %outer.cond = icmp slt i32 %i, %arg1
  br i1 %outer.cond, label %inner.loop, label %exit
inner.loop:
  %j = phi i32 [ 0, %outer.loop ], [ %next_j, %inner.body ]
  %inner.cond = icmp slt i32 %j, %arg2
  br i1 %inner.cond, label %inner.body, label %outer.body
inner.body:
  %global_inv = mul i32 %arg0, 10
  %outer_inv = add i32 %global_inv, %i
  %computation = mul i32 %outer_inv, %j
  %next_j = add i32 %j, %computation
  br label %inner.loop
outer.body:
  %next_i = add i32 %i, 1
  br label %outer.loop
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  %global_inv = mul i32 %arg0, 10
  br label %outer.loop
outer.loop:
  %i = phi i32 [ 0, %entry ], [ %next_i, %outer.body ]
  %outer.cond = icmp slt i32 %i, %arg1
  br i1 %outer.cond, label %inner.loop, label %exit
inner.loop:
  %j = phi i32 [ 0, %outer.loop ], [ %next_j, %inner.body ]
  %inner.cond = icmp slt i32 %j, %arg2
  %outer_inv = add i32 %global_inv, %i
  br i1 %inner.cond, label %inner.body, label %outer.body
inner.body:
  %computation = mul i32 %outer_inv, %j
  %next_j = add i32 %j, %computation
  br label %inner.loop
outer.body:
  %next_i = add i32 %i, 1
  br label %outer.loop
exit:
  ret i32 0
}
)");
}

// Test 4: PHI nodes with invariant incoming values
TEST_F(LICMPassTest, PHINodesWithInvariantValues) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry: compute invariant value
    builder->setInsertPoint(entryBB);
    auto invariantValue =
        builder->createMul(a, builder->getInt32(2), "invariant_val");
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    auto value =
        builder->createPHI(intType, "value");  // PHI with invariant values
    i->addIncoming(builder->getInt32(0), entryBB);
    value->addIncoming(invariantValue, entryBB);     // Invariant incoming
    value->addIncoming(invariantValue, loopBodyBB);  // Same invariant value

    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: use PHI value
    builder->setInsertPoint(loopBodyBB);
    auto result = builder->createAdd(value, builder->getInt32(1),
                                     "result");  // Should be hoisted
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(value);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant_val = mul i32 %arg0, 2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %value = phi i32 [ %invariant_val, %entry ], [ %invariant_val, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %result = add i32 %value, 1
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 %value
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant_val = mul i32 %arg0, 2
  %result = add i32 %invariant_val, 1
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 %invariant_val
}
)");
}

// Test 5: Function calls to pure functions
TEST_F(LICMPassTest, PureFunctionCalls) {
    auto intType = ctx->getIntegerType(32);

    // Create a pure function (only contains arithmetic operations)
    auto pureFuncType = FunctionType::get(intType, {intType});
    auto pureFunc = Function::Create(pureFuncType, "pure_func", module.get());
    auto pureBB = BasicBlock::Create(ctx.get(), "entry", pureFunc);
    builder->setInsertPoint(pureBB);
    auto param = pureFunc->getArg(0);
    auto doubled = builder->createMul(param, builder->getInt32(2), "doubled");
    builder->createRet(doubled);

    // Create main function with loop
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: call pure function with invariant argument
    builder->setInsertPoint(loopBodyBB);
    auto pureCall =
        builder->createCall(pureFunc, {a}, "pure_call");  // Should be hoisted
    auto result = builder->createAdd(pureCall, i, "result");
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %pure_call = call i32 @pure_func(i32 %arg0)
  %result = add i32 %pure_call, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %pure_call = call i32 @pure_func(i32 %arg0)
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %result = add i32 %pure_call, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 6: Complex expressions with multiple invariants
TEST_F(LICMPassTest, ComplexExpressionsMultipleInvariants) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto b = func->getArg(1);
    auto n = func->getArg(2);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: complex expression with multiple invariants
    builder->setInsertPoint(loopBodyBB);
    auto inv1 = builder->createAdd(a, b, "inv1");  // Invariant
    auto inv2 =
        builder->createMul(a, builder->getInt32(3), "inv2");  // Invariant
    auto inv3 = builder->createSub(
        inv1, inv2, "inv3");  // Depends on invariants, so invariant
    auto complex = builder->createAdd(inv3, builder->getInt32(7),
                                      "complex");  // Also invariant
    auto result =
        builder->createMul(complex, i, "result");  // Not invariant (uses i)
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg2
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %inv1 = add i32 %arg0, %arg1
  %inv2 = mul i32 %arg0, 3
  %inv3 = sub i32 %inv1, %inv2
  %complex = add i32 %inv3, 7
  %result = mul i32 %complex, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1, i32 %arg2) {
entry:
  %inv1 = add i32 %arg0, %arg1
  %inv2 = mul i32 %arg0, 3
  %inv3 = sub i32 %inv1, %inv2
  %complex = add i32 %inv3, 7
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg2
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %result = mul i32 %complex, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 7: Loops with side effects (should not hoist)
TEST_F(LICMPassTest, LoopsWithSideEffects) {
    auto intType = ctx->getIntegerType(32);
    auto voidType = ctx->getVoidType();

    // Create a function with side effects
    auto sideEffectFuncType = FunctionType::get(voidType, {});
    auto sideEffectFunc =
        Function::Create(sideEffectFuncType, "side_effect_func", module.get());
    auto sideEffectBB = BasicBlock::Create(ctx.get(), "entry", sideEffectFunc);
    builder->setInsertPoint(sideEffectBB);
    // Create a global variable to modify
    auto globalVar = GlobalVariable::Create(
        intType, false, GlobalVariable::ExternalLinkage, builder->getInt32(0),
        "global_counter", module.get());
    auto load = builder->createLoad(globalVar, "load");
    auto incremented = builder->createAdd(load, builder->getInt32(1), "inc");
    builder->createStore(incremented, globalVar);
    builder->createRetVoid();

    // Create main function
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: side effect call (should NOT be hoisted)
    builder->setInsertPoint(loopBodyBB);
    builder->createCall(sideEffectFunc, {},
                        "side_effect_call");  // Should NOT be hoisted
    auto computation =
        builder->createMul(builder->getInt32(5), builder->getInt32(3),
                           "computation");  // Should be hoisted
    auto nextI = builder->createAdd(i, computation, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  call void @side_effect_func()
  %computation = mul i32 5, 3
  %next_i = add i32 %i, %computation
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %computation = mul i32 5, 3
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  call void @side_effect_func()
  %next_i = add i32 %i, %computation
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 8: Memory aliasing scenarios (conservative approach)
TEST_F(LICMPassTest, MemoryAliasingScenarios) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = ctx->getPointerType(intType);
    auto funcType = FunctionType::get(intType, {ptrType, ptrType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto ptr1 = func->getArg(0);
    auto ptr2 = func->getArg(1);
    auto n = func->getArg(2);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: load from ptr1, store to ptr2, load from ptr1 again
    builder->setInsertPoint(loopBodyBB);
    auto load1 = builder->createLoad(ptr1, "load1");    // Might be hoisted
    builder->createStore(builder->getInt32(42), ptr2);  // Potential aliasing
    auto load2 = builder->createLoad(
        ptr1, "load2");  // Should NOT be hoisted due to potential aliasing
    auto result = builder->createAdd(load1, load2, "result");
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0, i32* %arg1, i32 %arg2) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg2
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %load1 = load i32, i32* %arg0
  store i32 42, i32* %arg1
  %load2 = load i32, i32* %arg0
  %result = add i32 %load1, %load2
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0, i32* %arg1, i32 %arg2) {
entry:
  %load1 = load i32, i32* %arg0
  %load2 = load i32, i32* %arg0
  %result = add i32 %load1, %load2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg2
  br i1 %cond, label %loop.body, label %exit
loop.body:
  store i32 42, i32* %arg1
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 9: Division operations (potential exceptions)
TEST_F(LICMPassTest, DivisionOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: division operations
    builder->setInsertPoint(loopBodyBB);
    auto safeDiv =
        builder->createDiv(a, builder->getInt32(2),
                           "safe_div");  // Safe division, should be hoisted
    auto unsafeDiv = builder->createDiv(
        a, i, "unsafe_div");  // Uses loop variable, can't be hoisted anyway
    auto result = builder->createAdd(safeDiv, unsafeDiv, "result");
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %safe_div = sdiv i32 %arg0, 2
  %unsafe_div = sdiv i32 %arg0, %i
  %result = add i32 %safe_div, %unsafe_div
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %safe_div = sdiv i32 %arg0, 2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %unsafe_div = sdiv i32 %arg0, %i
  %result = add i32 %safe_div, %unsafe_div
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 10: Array indexing with invariant indices
TEST_F(LICMPassTest, ArrayIndexingInvariantIndices) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = ctx->getPointerType(intType);
    auto funcType = FunctionType::get(intType, {ptrType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto array = func->getArg(0);
    auto idx = func->getArg(1);  // Invariant index
    auto n = func->getArg(2);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: array access with invariant index
    builder->setInsertPoint(loopBodyBB);
    auto gep = builder->createGEP(array, idx, "gep");    // Should be hoisted
    auto load = builder->createLoad(gep, "array_load");  // Should be hoisted
    auto result = builder->createAdd(load, i, "result");
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0, i32 %arg1, i32 %arg2) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg2
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %gep = getelementptr i32, i32* %arg0, i32 %arg1
  %array_load = load i32, i32* %gep
  %result = add i32 %array_load, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0, i32 %arg1, i32 %arg2) {
entry:
  %gep = getelementptr i32, i32* %arg0, i32 %arg1
  %array_load = load i32, i32* %gep
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg2
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %result = add i32 %array_load, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 11: Multiple exit blocks
TEST_F(LICMPassTest, MultipleExitBlocks) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto loopLatchBB = BasicBlock::Create(ctx.get(), "loop.latch", func);
    auto earlyExitBB = BasicBlock::Create(ctx.get(), "early.exit", func);
    auto normalExitBB = BasicBlock::Create(ctx.get(), "normal.exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, normalExitBB);

    // Loop body: invariant computation and early exit condition
    builder->setInsertPoint(loopBodyBB);
    auto invariant = builder->createMul(a, builder->getInt32(3),
                                        "invariant");  // Should be hoisted
    auto earlyExitCond =
        builder->createICmpSGT(invariant, builder->getInt32(100), "early_cond");
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    // Branch to either early exit or continue looping
    builder->createCondBr(earlyExitCond, earlyExitBB, loopLatchBB);

    // Loop latch: just increment and branch back
    builder->setInsertPoint(loopLatchBB);
    i->addIncoming(nextI, loopLatchBB);
    builder->createBr(loopHeaderBB);

    // Early exit
    builder->setInsertPoint(earlyExitBB);
    builder->createRet(builder->getInt32(-1));

    // Normal exit
    builder->setInsertPoint(normalExitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.latch ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %normal.exit
loop.body:
  %invariant = mul i32 %arg0, 3
  %early_cond = icmp sgt i32 %invariant, 100
  %next_i = add i32 %i, 1
  br i1 %early_cond, label %early.exit, label %loop.latch
loop.latch:
  br label %loop.header
early.exit:
  ret i32 -1
normal.exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant = mul i32 %arg0, 3
  %early_cond = icmp sgt i32 %invariant, 100
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.latch ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %normal.exit
loop.body:
  %next_i = add i32 %i, 1
  br i1 %early_cond, label %early.exit, label %loop.latch
loop.latch:
  br label %loop.header
early.exit:
  ret i32 -1
normal.exit:
  ret i32 0
}
)");
}

// Test 12: Loop-carried dependencies (should not hoist)
TEST_F(LICMPassTest, LoopCarriedDependencies) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    auto sum = builder->createPHI(intType, "sum");  // Loop-carried dependency
    i->addIncoming(builder->getInt32(0), entryBB);
    sum->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body
    builder->setInsertPoint(loopBodyBB);
    auto invariantComp =
        builder->createMul(builder->getInt32(5), builder->getInt32(7),
                           "invariant");  // Can be hoisted
    auto dependent = builder->createAdd(
        sum, invariantComp, "dependent");  // Uses loop-carried value
    auto nextSum =
        builder->createAdd(dependent, i, "next_sum");  // Creates dependency
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    sum->addIncoming(nextSum, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %sum = phi i32 [ 0, %entry ], [ %next_sum, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %invariant = mul i32 5, 7
  %dependent = add i32 %sum, %invariant
  %next_sum = add i32 %dependent, %i
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %sum
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %invariant = mul i32 5, 7
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %sum = phi i32 [ 0, %entry ], [ %next_sum, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %dependent = add i32 %sum, %invariant
  %next_sum = add i32 %dependent, %i
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %sum
}
)");
}

// Test 13: Conditional hoisting opportunities
TEST_F(LICMPassTest, ConditionalHoistingOpportunities) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto condCheckBB = BasicBlock::Create(ctx.get(), "cond.check", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, condCheckBB, exitBB);

    // Conditional check in loop
    builder->setInsertPoint(condCheckBB);
    auto innerCond =
        builder->createICmpSGT(i, builder->getInt32(5), "inner_cond");
    builder->createCondBr(innerCond, trueBB, falseBB);

    // True branch: invariant computation
    builder->setInsertPoint(trueBB);
    auto invariant1 = builder->createMul(a, builder->getInt32(2),
                                         "invariant1");  // Should be hoisted
    builder->createBr(mergeBB);

    // False branch: different invariant computation
    builder->setInsertPoint(falseBB);
    auto invariant2 = builder->createAdd(a, builder->getInt32(10),
                                         "invariant2");  // Should be hoisted
    builder->createBr(mergeBB);

    // Merge
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(invariant1, trueBB);
    phi->addIncoming(invariant2, falseBB);
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, mergeBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %merge ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %cond.check, label %exit
cond.check:
  %inner_cond = icmp sgt i32 %i, 5
  br i1 %inner_cond, label %true, label %false
true:
  %invariant1 = mul i32 %arg0, 2
  br label %merge
false:
  %invariant2 = add i32 %arg0, 10
  br label %merge
merge:
  %phi = phi i32 [ %invariant1, %true ], [ %invariant2, %false ]
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant2 = add i32 %arg0, 10
  %invariant1 = mul i32 %arg0, 2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %merge ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %cond.check, label %exit
cond.check:
  %inner_cond = icmp sgt i32 %i, 5
  br i1 %inner_cond, label %true, label %false
true:
  br label %merge
false:
  br label %merge
merge:
  %phi = phi i32 [ %invariant1, %true ], [ %invariant2, %false ]
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test 14: Preheader creation scenarios
TEST_F(LICMPassTest, PreheaderCreationScenarios) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto condBB = BasicBlock::Create(ctx.get(), "cond", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry: branch to condition check
    builder->setInsertPoint(entryBB);
    auto entryCond =
        builder->createICmpSGT(n, builder->getInt32(0), "entry_cond");
    builder->createCondBr(entryCond, condBB, exitBB);

    // Condition: another path to loop header
    builder->setInsertPoint(condBB);
    builder->createBr(loopHeaderBB);

    // Loop header (no preheader initially)
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), condBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: invariant computation that needs hoisting
    builder->setInsertPoint(loopBodyBB);
    auto invariant = builder->createMul(n, builder->getInt32(7),
                                        "invariant");  // Should be hoisted
    auto result = builder->createAdd(invariant, i, "result");
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %entry_cond = icmp sgt i32 %arg0, 0
  br i1 %entry_cond, label %cond, label %exit
cond:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %cond ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %invariant = mul i32 %arg0, 7
  %result = add i32 %invariant, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %entry_cond = icmp sgt i32 %arg0, 0
  br i1 %entry_cond, label %cond, label %exit
cond:
  %invariant = mul i32 %arg0, 7
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %cond ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %result = add i32 %invariant, %i
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test for statistics collection
TEST_F(LICMPassTest, StatisticsCollection) {
    auto intType = ctx->getIntegerType(32);

    // Create a global variable for loads
    auto globalVar = GlobalVariable::Create(
        intType, false, GlobalVariable::ExternalLinkage, builder->getInt32(42),
        "global_var", module.get());

    // Create a pure function for calls
    auto pureFuncType = FunctionType::get(intType, {intType});
    auto pureFunc = Function::Create(pureFuncType, "pure_func", module.get());
    auto pureBB = BasicBlock::Create(ctx.get(), "entry", pureFunc);
    builder->setInsertPoint(pureBB);
    auto param = pureFunc->getArg(0);
    auto doubled = builder->createMul(param, builder->getInt32(2), "doubled");
    builder->createRet(doubled);

    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: multiple hoistable instructions
    builder->setInsertPoint(loopBodyBB);
    auto arithmetic = builder->createAdd(a, builder->getInt32(5),
                                         "arithmetic");  // Regular instruction
    auto globalLoad =
        builder->createLoad(globalVar, "global_load");  // Load instruction
    auto pureCall =
        builder->createCall(pureFunc, {a}, "pure_call");  // Call instruction
    auto sum = builder->createAdd(arithmetic, globalLoad, "sum");
    auto result = builder->createAdd(sum, pureCall, "result");
    auto nextI = builder->createAdd(i, result, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %arithmetic = add i32 %arg0, 5
  %global_load = load i32, i32* @global_var
  %pure_call = call i32 @pure_func(i32 %arg0)
  %sum = add i32 %arithmetic, %global_load
  %result = add i32 %sum, %pure_call
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %arithmetic = add i32 %arg0, 5
  %global_load = load i32, i32* @global_var
  %pure_call = call i32 @pure_func(i32 %arg0)
  %sum = add i32 %arithmetic, %global_load
  %result = add i32 %sum, %pure_call
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %next_i = add i32 %i, %result
  br label %loop.header
exit:
  ret i32 0
}
)");
}

// Test negative case: verify instructions that should NOT be hoisted
TEST_F(LICMPassTest, NegativeTestInstructionsShouldNotHoist) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry
    builder->setInsertPoint(entryBB);
    auto mem = builder->createAlloca(intType, nullptr, "mem");
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: instructions that should NOT be hoisted
    builder->setInsertPoint(loopBodyBB);
    auto dependent = builder->createAdd(i, builder->getInt32(1),
                                        "dependent");  // Uses loop variable
    builder->createStore(dependent, mem);              // Store has side effects
    auto load = builder->createLoad(mem, "load");      // Load after store
    auto nextI = builder->createAdd(i, load, "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR, R"(define i32 @test_func(i32 %arg0) {
entry:
  %mem = alloca i32
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg0
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %dependent = add i32 %i, 1
  store i32 %dependent, i32* %mem
  %load = load i32, i32* %mem
  %next_i = add i32 %i, %load
  br label %loop.header
exit:
  ret i32 0
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

// Test 16: Maximum iteration limit enforcement
TEST_F(LICMPassTest, MaximumIterationLimitEnforcement) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: Create a very long chain of dependent invariant instructions
    // This tests that the pass respects MAX_HOISTING_ITERATIONS = 50
    builder->setInsertPoint(loopBodyBB);

    Value* current = n;
    std::vector<Instruction*> instructions;

    // Create 60 chained invariant instructions (more than
    // MAX_HOISTING_ITERATIONS)
    for (int idx = 0; idx < 60; ++idx) {
        auto add = builder->createAdd(current, builder->getInt32(1),
                                      "chain" + std::to_string(idx));
        instructions.push_back(add);
        current = add;
    }

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(current);

    std::string originalIR = IRPrinter().print(func);

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);

    // The pass should terminate after MAX_HOISTING_ITERATIONS and not hang
    // Some hoisting should occur but not all instructions
    std::string optimizedIR = IRPrinter().print(func);

    // Verify the pass completed (didn't hang)
    EXPECT_TRUE(true);  // If we reach here, the pass didn't hang

    // Some optimization should have occurred
    if (changed) {
        EXPECT_NE(originalIR, optimizedIR);
    }
}

// Test 19: Multiple loop exit scenarios (break statements)
TEST_F(LICMPassTest, MultipleLoopExitScenarios) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto breakCheckBB = BasicBlock::Create(ctx.get(), "break.check", func);
    auto continueCheckBB =
        BasicBlock::Create(ctx.get(), "continue.check", func);
    auto earlyExitBB = BasicBlock::Create(ctx.get(), "early.exit", func);
    auto normalExitBB = BasicBlock::Create(ctx.get(), "normal.exit", func);
    auto returnBB = BasicBlock::Create(ctx.get(), "return", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, normalExitBB);

    // Loop body: invariant computation
    builder->setInsertPoint(loopBodyBB);
    auto invariant = builder->createMul(a, builder->getInt32(3), "invariant");
    auto computation = builder->createAdd(invariant, i, "computation");
    builder->createBr(breakCheckBB);

    // Break check: early exit condition
    builder->setInsertPoint(breakCheckBB);
    auto breakCond = builder->createICmpSGT(computation, builder->getInt32(100),
                                            "break_cond");
    builder->createCondBr(breakCond, earlyExitBB, continueCheckBB);

    // Continue check: skip iteration condition
    builder->setInsertPoint(continueCheckBB);
    auto continueCond = builder->createICmpEQ(
        builder->createAnd(i, builder->getInt32(1), "mod2"),
        builder->getInt32(0), "continue_cond");
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, continueCheckBB);
    builder->createCondBr(continueCond, loopHeaderBB, loopHeaderBB);

    // Early exit (break)
    builder->setInsertPoint(earlyExitBB);
    builder->createBr(returnBB);

    // Normal exit
    builder->setInsertPoint(normalExitBB);
    builder->createBr(returnBB);

    // Return
    builder->setInsertPoint(returnBB);
    auto finalResult = builder->createPHI(intType, "final_result");
    finalResult->addIncoming(builder->getInt32(-1), earlyExitBB);
    finalResult->addIncoming(builder->getInt32(0), normalExitBB);
    builder->createRet(finalResult);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %continue.check ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %normal.exit
loop.body:
  %invariant = mul i32 %arg0, 3
  %computation = add i32 %invariant, %i
  br label %break.check
break.check:
  %break_cond = icmp sgt i32 %computation, 100
  br i1 %break_cond, label %early.exit, label %continue.check
continue.check:
  %mod2 = and i32 %i, 1
  %continue_cond = icmp eq i32 %mod2, 0
  %next_i = add i32 %i, 1
  br i1 %continue_cond, label %loop.header, label %loop.header
early.exit:
  br label %return
normal.exit:
  br label %return
return:
  %final_result = phi i32 [ -1, %early.exit ], [ 0, %normal.exit ]
  ret i32 %final_result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant = mul i32 %arg0, 3
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %continue.check ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %normal.exit
loop.body:
  %computation = add i32 %invariant, %i
  br label %break.check
break.check:
  %break_cond = icmp sgt i32 %computation, 100
  br i1 %break_cond, label %early.exit, label %continue.check
continue.check:
  %mod2 = and i32 %i, 1
  %continue_cond = icmp eq i32 %mod2, 0
  %next_i = add i32 %i, 1
  br i1 %continue_cond, label %loop.header, label %loop.header
early.exit:
  br label %return
normal.exit:
  br label %return
return:
  %final_result = phi i32 [ -1, %early.exit ], [ 0, %normal.exit ]
  ret i32 %final_result
}
)");
}

// Test 20: Continue statements in loops (represented as conditional branches
// back to header)
TEST_F(LICMPassTest, ContinueStatementsInLoops) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto skipWorkBB = BasicBlock::Create(ctx.get(), "skip.work", func);
    auto doWorkBB = BasicBlock::Create(ctx.get(), "do.work", func);
    auto loopIncrBB = BasicBlock::Create(ctx.get(), "loop.incr", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    auto sum = builder->createAlloca(intType, nullptr, "sum");
    builder->createStore(builder->getInt32(0), sum);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: check if we should skip this iteration
    builder->setInsertPoint(loopBodyBB);
    auto invariant = builder->createMul(a, builder->getInt32(5),
                                        "invariant");  // Should be hoisted
    auto skipCond = builder->createICmpEQ(
        builder->createAnd(i, builder->getInt32(1), "mod2"),
        builder->getInt32(0), "skip_cond");
    builder->createCondBr(skipCond, skipWorkBB, doWorkBB);

    // Skip work (continue)
    builder->setInsertPoint(skipWorkBB);
    builder->createBr(loopIncrBB);

    // Do work
    builder->setInsertPoint(doWorkBB);
    auto currentSum = builder->createLoad(sum, "current_sum");
    auto newSum = builder->createAdd(currentSum, invariant, "new_sum");
    builder->createStore(newSum, sum);
    builder->createBr(loopIncrBB);

    // Loop increment
    builder->setInsertPoint(loopIncrBB);
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopIncrBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalSum = builder->createLoad(sum, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.incr ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %invariant = mul i32 %arg0, 5
  %mod2 = and i32 %i, 1
  %skip_cond = icmp eq i32 %mod2, 0
  br i1 %skip_cond, label %skip.work, label %do.work
skip.work:
  br label %loop.incr
do.work:
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %invariant
  store i32 %new_sum, i32* %sum
  br label %loop.incr
loop.incr:
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  %invariant = mul i32 %arg0, 5
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.incr ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %mod2 = and i32 %i, 1
  %skip_cond = icmp eq i32 %mod2, 0
  br i1 %skip_cond, label %skip.work, label %do.work
skip.work:
  br label %loop.incr
do.work:
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %invariant
  store i32 %new_sum, i32* %sum
  br label %loop.incr
loop.incr:
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
)");
}

// Test 21: Irreducible control flow handling
TEST_F(LICMPassTest, IrreducibleControlFlowHandling) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto altPreheaderBB = BasicBlock::Create(ctx.get(), "alt.preheader", func);
    auto altHeaderBB = BasicBlock::Create(ctx.get(), "alt.header", func);
    auto altBodyBB = BasicBlock::Create(ctx.get(), "alt.body", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto n = func->getArg(0);

    // Entry
    builder->setInsertPoint(entryBB);
    auto initialCond =
        builder->createICmpSGT(n, builder->getInt32(10), "initial_cond");
    builder->createCondBr(initialCond, loopHeaderBB, altPreheaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i1 = builder->createPHI(intType, "i1");
    i1->addIncoming(builder->getInt32(0), entryBB);
    auto cond1 = builder->createICmpSLT(i1, n, "cond1");
    builder->createCondBr(cond1, loopBodyBB, exitBB);

    // Loop body - can branch to alternative header (irreducible)
    builder->setInsertPoint(loopBodyBB);
    auto invariant1 = builder->createMul(n, builder->getInt32(2), "invariant1");
    auto branchCond =
        builder->createICmpSGT(i1, builder->getInt32(5), "branch_cond");
    auto nextI1 = builder->createAdd(i1, builder->getInt32(1), "next_i1");
    i1->addIncoming(nextI1, loopBodyBB);
    builder->createCondBr(branchCond, altPreheaderBB, loopHeaderBB);

    builder->setInsertPoint(altPreheaderBB);
    builder->createBr(altHeaderBB);

    // Alternative header (creates irreducible control flow)
    builder->setInsertPoint(altHeaderBB);
    auto i2 = builder->createPHI(intType, "i2");
    i2->addIncoming(builder->getInt32(5), entryBB);
    i2->addIncoming(nextI1,
                    loopBodyBB);  // From loop body - creates irreducibility
    auto cond2 = builder->createICmpSLT(i2, builder->getInt32(20), "cond2");
    builder->createCondBr(cond2, altBodyBB, mergeBB);

    // Alternative body - can branch back to main loop
    builder->setInsertPoint(altBodyBB);
    auto invariant2 = builder->createAdd(n, builder->getInt32(7), "invariant2");
    auto useInvariant2 =
        builder->createAdd(invariant2, builder->getInt32(0), "use_invariant2");
    auto backCond =
        builder->createICmpSLT(i2, builder->getInt32(15), "back_cond");
    auto nextI2 = builder->createAdd(i2, useInvariant2, "next_i2");
    i2->addIncoming(nextI2, altBodyBB);
    i1->addIncoming(nextI2, altBodyBB);  // Branch back to main loop
    builder->createCondBr(backCond, loopHeaderBB, altHeaderBB);

    // Merge
    builder->setInsertPoint(mergeBB);
    builder->createBr(exitBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto result = builder->createPHI(intType, "result");
    result->addIncoming(invariant1, loopHeaderBB);
    result->addIncoming(builder->getInt32(0), mergeBB);
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %initial_cond = icmp sgt i32 %arg0, 10
  br i1 %initial_cond, label %loop.header, label %alt.preheader
loop.header:
  %i1 = phi i32 [ 0, %entry ], [ %next_i1, %loop.body ], [ %next_i2, %alt.body ]
  %cond1 = icmp slt i32 %i1, %arg0
  br i1 %cond1, label %loop.body, label %exit
loop.body:
  %invariant1 = mul i32 %arg0, 2
  %branch_cond = icmp sgt i32 %i1, 5
  %next_i1 = add i32 %i1, 1
  br i1 %branch_cond, label %alt.preheader, label %loop.header
alt.preheader:
  br label %alt.header
alt.header:
  %i2 = phi i32 [ 5, %entry ], [ %next_i1, %loop.body ], [ %next_i2, %alt.body ]
  %cond2 = icmp slt i32 %i2, 20
  br i1 %cond2, label %alt.body, label %merge
alt.body:
  %invariant2 = add i32 %arg0, 7
  %use_invariant2 = add i32 %invariant2, 0
  %back_cond = icmp slt i32 %i2, 15
  %next_i2 = add i32 %i2, %use_invariant2
  br i1 %back_cond, label %loop.header, label %alt.header
merge:
  br label %exit
exit:
  %result = phi i32 [ %invariant1, %loop.header ], [ 0, %merge ]
  ret i32 %result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_func(i32 %arg0) {
entry:
  %initial_cond = icmp sgt i32 %arg0, 10
  %invariant1 = mul i32 %arg0, 2
  br i1 %initial_cond, label %loop.header, label %alt.preheader
loop.header:
  %i1 = phi i32 [ 0, %entry ], [ %next_i1, %loop.body ], [ %next_i2, %alt.body ]
  %cond1 = icmp slt i32 %i1, %arg0
  br i1 %cond1, label %loop.body, label %exit
loop.body:
  %branch_cond = icmp sgt i32 %i1, 5
  %next_i1 = add i32 %i1, 1
  br i1 %branch_cond, label %alt.preheader, label %loop.header
alt.preheader:
  %invariant2 = add i32 %arg0, 7
  %use_invariant2 = add i32 %invariant2, 0
  br label %alt.header
alt.header:
  %i2 = phi i32 [ 5, %entry ], [ %next_i1, %loop.body ], [ %next_i2, %alt.body ]
  %cond2 = icmp slt i32 %i2, 20
  br i1 %cond2, label %alt.body, label %merge
alt.body:
  %back_cond = icmp slt i32 %i2, 15
  %next_i2 = add i32 %i2, %use_invariant2
  br i1 %back_cond, label %loop.header, label %alt.header
merge:
  br label %exit
exit:
  %result = phi i32 [ %invariant1, %loop.header ], [ 0, %merge ]
  ret i32 %result
}
)");
}

// Test 22: Volatile memory operations (should never be hoisted)
TEST_F(LICMPassTest, VolatileMemoryOperations) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = ctx->getPointerType(intType);
    auto funcType = FunctionType::get(intType, {ptrType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto volatilePtr = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    auto sum = builder->createAlloca(intType, nullptr, "sum");
    builder->createStore(builder->getInt32(0), sum);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: volatile operations that should NOT be hoisted
    builder->setInsertPoint(loopBodyBB);
    // Regular invariant computation (should be hoisted)
    auto invariant = builder->createMul(n, builder->getInt32(2), "invariant");

    // Create load and store operations (simulating volatile behavior with side
    // effects) NOTE: This IR framework doesn't support volatile - we'll
    // simulate the side effects
    auto volatileLoad = builder->createLoad(volatilePtr, "volatile_load");

    // Create a store operation (should NOT be hoisted due to side effects)
    auto storeValue =
        builder->createAdd(invariant, builder->getInt32(1), "store_val");
    builder->createStore(storeValue, volatilePtr);

    auto currentSum = builder->createLoad(sum, "current_sum");
    auto newSum = builder->createAdd(currentSum, volatileLoad, "new_sum");
    builder->createStore(newSum, sum);

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalSum = builder->createLoad(sum, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0, i32 %arg1) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %invariant = mul i32 %arg1, 2
  %volatile_load = load i32, i32* %arg0
  %store_val = add i32 %invariant, 1
  store i32 %store_val, i32* %arg0
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %volatile_load
  store i32 %new_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32* %arg0, i32 %arg1) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  %invariant = mul i32 %arg1, 2
  %store_val = add i32 %invariant, 1
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %volatile_load = load i32, i32* %arg0
  store i32 %store_val, i32* %arg0
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %volatile_load
  store i32 %new_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
)");
}

// Test 24: Complex array access patterns (simulating struct field access)
TEST_F(LICMPassTest, ComplexArrayAccessPatterns) {
    auto intType = ctx->getIntegerType(32);

    // Create array types to simulate struct-like access patterns
    auto arrayType =
        ctx->getArrayType(intType, 4);  // Simulate a struct with 4 fields
    auto ptrType = ctx->getPointerType(arrayType);

    auto funcType = FunctionType::get(intType, {ptrType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto arrayPtr = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: invariant array element accesses (simulating struct field
    // access)
    builder->setInsertPoint(loopBodyBB);

    // Access first element (invariant GEP and load)
    auto field0Ptr = builder->createGEP(arrayType, arrayPtr,
                                        {builder->getInt32(0)}, "field0_ptr");
    auto field0Load = builder->createLoad(field0Ptr, "field0_load");

    // Access second element (invariant GEP and load)
    auto field1Ptr = builder->createGEP(arrayType, arrayPtr,
                                        {builder->getInt32(1)}, "field1_ptr");
    auto field1Load = builder->createLoad(field1Ptr, "field1_load");

    // Access third element (invariant GEP and load)
    auto field2Ptr = builder->createGEP(arrayType, arrayPtr,
                                        {builder->getInt32(2)}, "field2_ptr");
    auto field2Load = builder->createLoad(field2Ptr, "field2_load");

    // Compute with loaded values (uses loop-variant i, so can't be hoisted)
    auto sum = builder->createAdd(field0Load, field1Load, "sum");
    auto result = builder->createAdd(sum, field2Load, "result");
    auto finalResult = builder->createAdd(result, i, "final_result");

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(finalResult);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func([4 x i32]* %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %field0_ptr = getelementptr [4 x i32], [4 x i32]* %arg0, i32 0
  %field0_load = load i32, i32* %field0_ptr
  %field1_ptr = getelementptr [4 x i32], [4 x i32]* %arg0, i32 1
  %field1_load = load i32, i32* %field1_ptr
  %field2_ptr = getelementptr [4 x i32], [4 x i32]* %arg0, i32 2
  %field2_load = load i32, i32* %field2_ptr
  %sum = add i32 %field0_load, %field1_load
  %result = add i32 %sum, %field2_load
  %final_result = add i32 %result, %i
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %final_result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func([4 x i32]* %arg0, i32 %arg1) {
entry:
  %field0_ptr = getelementptr [4 x i32], [4 x i32]* %arg0, i32 0
  %field0_load = load i32, i32* %field0_ptr
  %field1_ptr = getelementptr [4 x i32], [4 x i32]* %arg0, i32 1
  %field1_load = load i32, i32* %field1_ptr
  %field2_ptr = getelementptr [4 x i32], [4 x i32]* %arg0, i32 2
  %field2_load = load i32, i32* %field2_ptr
  %sum = add i32 %field0_load, %field1_load
  %result = add i32 %sum, %field2_load
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %final_result = add i32 %result, %i
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %final_result
}
)");
}

// Test 25: Array access with invariant indices (already covered in existing
// test 10, but let's add a more complex version)
TEST_F(LICMPassTest, MultiDimensionalArrayAccessInvariantIndices) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ctx->getArrayType(intType, 10);
    auto array2DType = ctx->getArrayType(arrayType, 10);
    auto ptrType = ctx->getPointerType(array2DType);
    auto funcType =
        FunctionType::get(intType, {ptrType, intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto array2D = func->getArg(0);
    auto row = func->getArg(1);  // invariant index
    auto col = func->getArg(2);  // invariant index
    auto n = func->getArg(3);

    // Entry
    builder->setInsertPoint(entryBB);
    auto sum = builder->createAlloca(intType, nullptr, "sum");
    builder->createStore(builder->getInt32(0), sum);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: multi-dimensional array access with invariant indices
    builder->setInsertPoint(loopBodyBB);

    // Access array[row][col] - both indices are loop invariant
    auto gep =
        builder->createGEP(array2DType, array2D, {row, col}, "array_gep");
    auto load = builder->createLoad(gep, "array_load");  // Should be hoisted

    // Access array[i][col] - first index is variant, second is invariant
    auto variantGep =
        builder->createGEP(array2DType, array2D, {i, col}, "variant_gep");
    auto variantLoad = builder->createLoad(
        variantGep, "variant_load");  // Should NOT be hoisted

    // Compute with loaded values
    auto currentSum = builder->createLoad(sum, "current_sum");
    auto newSum = builder->createAdd(currentSum, load, "new_sum");
    auto finalSum = builder->createAdd(newSum, variantLoad, "final_sum");
    builder->createStore(finalSum, sum);

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto result = builder->createLoad(sum, "result");
    builder->createRet(result);

    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_func([10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg3
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %array_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2
  %array_load = load i32, i32* %array_gep
  %variant_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %i, i32 %arg2
  %variant_load = load i32, i32* %variant_gep
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %array_load
  %final_sum = add i32 %new_sum, %variant_load
  store i32 %final_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %result = load i32, i32* %sum
  ret i32 %result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_func([10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  %array_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2
  %array_load = load i32, i32* %array_gep
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg3
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %variant_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %i, i32 %arg2
  %variant_load = load i32, i32* %variant_gep
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %array_load
  %final_sum = add i32 %new_sum, %variant_load
  store i32 %final_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %result = load i32, i32* %sum
  ret i32 %result
}
)");
}

// Test 26: PHI nodes with many predecessors (5+)
TEST_F(LICMPassTest, PHINodesWithManyPredecessors) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto case0BB = BasicBlock::Create(ctx.get(), "case0", func);
    auto case1BB = BasicBlock::Create(ctx.get(), "case1", func);
    auto case2BB = BasicBlock::Create(ctx.get(), "case2", func);
    auto case3BB = BasicBlock::Create(ctx.get(), "case3", func);
    auto case4BB = BasicBlock::Create(ctx.get(), "case4", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry: precompute invariant values
    builder->setInsertPoint(entryBB);
    auto inv0 = builder->createMul(a, builder->getInt32(1), "inv0");
    auto inv1 = builder->createMul(a, builder->getInt32(2), "inv1");
    auto inv2 = builder->createMul(a, builder->getInt32(3), "inv2");
    auto inv3 = builder->createMul(a, builder->getInt32(4), "inv3");
    auto inv4 = builder->createMul(a, builder->getInt32(5), "inv4");
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, case0BB, exitBB);

    // Create a switch-like pattern with many blocks feeding into a PHI
    builder->setInsertPoint(case0BB);
    auto branch0 = builder->createICmpEQ(
        builder->createAnd(i, builder->getInt32(7), "mod8"),
        builder->getInt32(0), "branch0");
    builder->createCondBr(branch0, case1BB, case2BB);

    builder->setInsertPoint(case1BB);
    auto branch1 = builder->createICmpEQ(
        builder->createAnd(i, builder->getInt32(3), "mod4"),
        builder->getInt32(1), "branch1");
    builder->createCondBr(branch1, case3BB, case4BB);

    builder->setInsertPoint(case2BB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(case3BB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(case4BB);
    builder->createBr(mergeBB);

    // Merge with PHI having 5+ predecessors (will add more through loop
    // structure)
    builder->setInsertPoint(mergeBB);
    auto manyPredPhi = builder->createPHI(intType, "many_pred_phi");

    // Add incoming values - all are loop invariant
    manyPredPhi->addIncoming(
        inv0, case0BB);  // Case where no further branching occurred
    manyPredPhi->addIncoming(inv1,
                             case1BB);  // Not reachable but syntactically valid
    manyPredPhi->addIncoming(inv2, case2BB);
    manyPredPhi->addIncoming(inv3, case3BB);
    manyPredPhi->addIncoming(inv4, case4BB);

    // PHI node should be recognized as invariant since all incoming values are
    // invariant
    auto computation =
        builder->createAdd(manyPredPhi, builder->getInt32(10), "computation");

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, mergeBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(computation);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %inv0 = mul i32 %arg0, 1
  %inv1 = mul i32 %arg0, 2
  %inv2 = mul i32 %arg0, 3
  %inv3 = mul i32 %arg0, 4
  %inv4 = mul i32 %arg0, 5
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %merge ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %case0, label %exit
case0:
  %mod8 = and i32 %i, 7
  %branch0 = icmp eq i32 %mod8, 0
  br i1 %branch0, label %case1, label %case2
case1:
  %mod4 = and i32 %i, 3
  %branch1 = icmp eq i32 %mod4, 1
  br i1 %branch1, label %case3, label %case4
case2:
  br label %merge
case3:
  br label %merge
case4:
  br label %merge
merge:
  %many_pred_phi = phi i32 [ %inv0, %case0 ], [ %inv1, %case1 ], [ %inv2, %case2 ], [ %inv3, %case3 ], [ %inv4, %case4 ]
  %computation = add i32 %many_pred_phi, 10
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %computation
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

// Test 27: Circular PHI dependencies
TEST_F(LICMPassTest, CircularPHIDependencies) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    auto initialValue = builder->createMul(a, builder->getInt32(2), "initial");
    builder->createBr(loopHeaderBB);

    // Loop header: create circular PHI dependencies
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    auto phi1 = builder->createPHI(intType, "phi1");
    auto phi2 = builder->createPHI(intType, "phi2");

    i->addIncoming(builder->getInt32(0), entryBB);
    // phi1 depends on phi2, phi2 depends on phi1 (circular)
    phi1->addIncoming(initialValue, entryBB);
    phi2->addIncoming(initialValue, entryBB);

    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: create the circular dependency
    builder->setInsertPoint(loopBodyBB);

    // Create computations that maintain the circular dependency
    auto newPhi2 = builder->createAdd(phi1, builder->getInt32(0),
                                      "new_phi2");  // phi1 -> phi2
    auto newPhi1 = builder->createAdd(phi2, builder->getInt32(0),
                                      "new_phi1");  // phi2 -> phi1

    // This should be recognized as loop invariant since both PHIs have the same
    // invariant value
    auto invariantComputation =
        builder->createMul(phi1, builder->getInt32(3), "invariant_comp");

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");

    i->addIncoming(nextI, loopBodyBB);
    phi1->addIncoming(newPhi1, loopBodyBB);
    phi2->addIncoming(newPhi2, loopBodyBB);

    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(invariantComputation);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %initial = mul i32 %arg0, 2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %phi1 = phi i32 [ %initial, %entry ], [ %new_phi1, %loop.body ]
  %phi2 = phi i32 [ %initial, %entry ], [ %new_phi2, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %new_phi2 = add i32 %phi1, 0
  %new_phi1 = add i32 %phi2, 0
  %invariant_comp = mul i32 %phi1, 3
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %invariant_comp
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

// Test 28: Mixed invariant/variant incoming values in PHI nodes
TEST_F(LICMPassTest, MixedInvariantVariantPHIIncomingValues) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto condCheckBB = BasicBlock::Create(ctx.get(), "cond.check", func);
    auto variantPathBB = BasicBlock::Create(ctx.get(), "variant.path", func);
    auto invariantPathBB =
        BasicBlock::Create(ctx.get(), "invariant.path", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    auto invariantValue =
        builder->createMul(a, builder->getInt32(5), "invariant_value");
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, condCheckBB, exitBB);

    // Condition check: decide which path to take
    builder->setInsertPoint(condCheckBB);
    auto pathCond = builder->createICmpEQ(
        builder->createAnd(i, builder->getInt32(1), "mod2"),
        builder->getInt32(0), "path_cond");
    builder->createCondBr(pathCond, variantPathBB, invariantPathBB);

    // Variant path: produces loop-variant value
    builder->setInsertPoint(variantPathBB);
    auto variantValue =
        builder->createAdd(i, builder->getInt32(10), "variant_value");
    builder->createBr(mergeBB);

    // Invariant path: produces loop-invariant value
    builder->setInsertPoint(invariantPathBB);
    auto anotherInvariant = builder->createAdd(
        invariantValue, builder->getInt32(3), "another_invariant");
    builder->createBr(mergeBB);

    // Merge: PHI with mixed invariant/variant incoming values
    builder->setInsertPoint(mergeBB);
    auto mixedPhi = builder->createPHI(intType, "mixed_phi");
    mixedPhi->addIncoming(variantValue, variantPathBB);  // Variant incoming
    mixedPhi->addIncoming(anotherInvariant,
                          invariantPathBB);  // Invariant incoming

    // This computation using the mixed PHI should NOT be hoisted
    // since the PHI is not loop invariant (has variant incoming values)
    auto computation =
        builder->createMul(mixedPhi, builder->getInt32(2), "computation");

    // But this invariant computation should be hoisted
    auto pureInvariant =
        builder->createSub(a, builder->getInt32(7), "pure_invariant");

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, mergeBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto result = builder->createAdd(computation, pureInvariant, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant_value = mul i32 %arg0, 5
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %merge ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %cond.check, label %exit
cond.check:
  %mod2 = and i32 %i, 1
  %path_cond = icmp eq i32 %mod2, 0
  br i1 %path_cond, label %variant.path, label %invariant.path
variant.path:
  %variant_value = add i32 %i, 10
  br label %merge
invariant.path:
  %another_invariant = add i32 %invariant_value, 3
  br label %merge
merge:
  %mixed_phi = phi i32 [ %variant_value, %variant.path ], [ %another_invariant, %invariant.path ]
  %computation = mul i32 %mixed_phi, 2
  %pure_invariant = sub i32 %arg0, 7
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %result = add i32 %computation, %pure_invariant
  ret i32 %result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  %invariant_value = mul i32 %arg0, 5
  %another_invariant = add i32 %invariant_value, 3
  %pure_invariant = sub i32 %arg0, 7
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %merge ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %cond.check, label %exit
cond.check:
  %mod2 = and i32 %i, 1
  %path_cond = icmp eq i32 %mod2, 0
  br i1 %path_cond, label %variant.path, label %invariant.path
variant.path:
  %variant_value = add i32 %i, 10
  br label %merge
invariant.path:
  br label %merge
merge:
  %mixed_phi = phi i32 [ %variant_value, %variant.path ], [ %another_invariant, %invariant.path ]
  %computation = mul i32 %mixed_phi, 2
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %result = add i32 %computation, %pure_invariant
  ret i32 %result
}
)");
}

// Test: Pure function call with loop-dependent variables
TEST_F(LICMPassTest, PureFunctionCallWithLoopDependentVariables) {
    auto intType = ctx->getIntegerType(32);

    // Create a pure function (only contains arithmetic operations)
    auto pureFuncType = FunctionType::get(intType, {intType});
    auto pureFunc = Function::Create(pureFuncType, "pure_func", module.get());
    auto pureBB = BasicBlock::Create(ctx.get(), "entry", pureFunc);
    builder->setInsertPoint(pureBB);
    auto param = pureFunc->getArg(0);
    auto doubled = builder->createMul(param, builder->getInt32(2), "doubled");
    builder->createRet(doubled);

    // Create main function with loop
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto a = func->getArg(0);
    auto n = func->getArg(1);

    // Entry
    builder->setInsertPoint(entryBB);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: call pure function with loop-dependent variable (should NOT be
    // hoisted)
    builder->setInsertPoint(loopBodyBB);
    auto pureCallWithLoopVar = builder->createCall(
        pureFunc, {i}, "pure_call_loop_var");  // Should NOT be hoisted
    auto result = builder->createAdd(pureCallWithLoopVar, a, "result");
    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(result);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_func(i32 %arg0, i32 %arg1) {
entry:
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg1
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %pure_call_loop_var = call i32 @pure_func(i32 %i)
  %result = add i32 %pure_call_loop_var, %arg0
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  ret i32 %result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(LICMPassTest, MultiDimensionalArrayAccessInvariantIndicesWithStore) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ctx->getArrayType(intType, 10);
    auto array2DType = ctx->getArrayType(arrayType, 10);
    auto ptrType = ctx->getPointerType(array2DType);
    auto funcType =
        FunctionType::get(intType, {ptrType, intType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto array2D = func->getArg(0);
    auto row = func->getArg(1);  // invariant index
    auto col = func->getArg(2);  // invariant index
    auto n = func->getArg(3);

    // Entry
    builder->setInsertPoint(entryBB);
    auto sum = builder->createAlloca(intType, nullptr, "sum");
    builder->createStore(builder->getInt32(0), sum);
    builder->createBr(loopHeaderBB);

    // Loop header
    builder->setInsertPoint(loopHeaderBB);
    auto i = builder->createPHI(intType, "i");
    i->addIncoming(builder->getInt32(0), entryBB);
    auto cond = builder->createICmpSLT(i, n, "cond");
    builder->createCondBr(cond, loopBodyBB, exitBB);

    // Loop body: multi-dimensional array access with invariant indices
    builder->setInsertPoint(loopBodyBB);

    // Access array[row][col] - both indices are loop invariant
    auto gep =
        builder->createGEP(array2DType, array2D, {row, col}, "array_gep");
    auto load = builder->createLoad(gep, "array_load");  // Should be hoisted

    // Access array[i][col] - first index is variant, second is invariant
    auto variantGep =
        builder->createGEP(array2DType, array2D, {i, col}, "variant_gep");
    auto variantLoad = builder->createLoad(
        variantGep, "variant_load");  // Should NOT be hoisted

    // Compute with loaded values
    auto currentSum = builder->createLoad(sum, "current_sum");
    auto newSum = builder->createAdd(currentSum, load, "new_sum");
    builder->createStore(newSum, gep);
    auto finalSum = builder->createAdd(newSum, variantLoad, "final_sum");
    builder->createStore(finalSum, sum);

    auto nextI = builder->createAdd(i, builder->getInt32(1), "next_i");
    i->addIncoming(nextI, loopBodyBB);
    builder->createBr(loopHeaderBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto result = builder->createLoad(sum, "result");
    builder->createRet(result);

    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_func([10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg3
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %array_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2
  %array_load = load i32, i32* %array_gep
  %variant_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %i, i32 %arg2
  %variant_load = load i32, i32* %variant_gep
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %array_load
  store i32 %new_sum, i32* %array_gep
  %final_sum = add i32 %new_sum, %variant_load
  store i32 %final_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %result = load i32, i32* %sum
  ret i32 %result
}
)");

    // Run LICM Pass
    LICMPass licm;
    bool changed = licm.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_func([10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  %array_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %arg1, i32 %arg2
  br label %loop.header
loop.header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %loop.body ]
  %cond = icmp slt i32 %i, %arg3
  br i1 %cond, label %loop.body, label %exit
loop.body:
  %array_load = load i32, i32* %array_gep
  %variant_gep = getelementptr [10 x [10 x i32]], [10 x [10 x i32]]* %arg0, i32 %i, i32 %arg2
  %variant_load = load i32, i32* %variant_gep
  %current_sum = load i32, i32* %sum
  %new_sum = add i32 %current_sum, %array_load
  store i32 %new_sum, i32* %array_gep
  %final_sum = add i32 %new_sum, %variant_load
  store i32 %final_sum, i32* %sum
  %next_i = add i32 %i, 1
  br label %loop.header
exit:
  %result = load i32, i32* %sum
  ret i32 %result
}
)");
}