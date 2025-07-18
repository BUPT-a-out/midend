#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Pass.h"
#include "Pass/Transform/TailRecursionOptimizationPass.h"

using namespace midend;

class TailRecursionOptimizationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

// Test Case 1: Basic tail recursion - factorial
TEST_F(TailRecursionOptimizationTest, BasicTailRecursion_Factorial) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "factorial", module.get());

    auto n = func->getArg(0);
    auto acc = func->getArg(1);
    n->setName("n");
    acc->setName("acc");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, baseBB, recursiveBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(acc);

    builder->setInsertPoint(recursiveBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto acc1 = builder->createMul(acc, n);
    auto callInst = builder->createCall(func, {n1, acc1});
    builder->createRet(callInst);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @factorial(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %recursive
base:
  ret i32 %acc
recursive:
  %1 = sub i32 %n, 1
  %2 = mul i32 %acc, %n
  %3 = call i32 @factorial(i32 %1, i32 %2)
  ret i32 %3
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @factorial(i32 %n, i32 %acc) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %n.phi = phi i32 [ %n, %entry ], [ %0, %recursive ]
  %acc.phi = phi i32 [ %acc, %entry ], [ %1, %recursive ]
  %2 = icmp sle i32 %n.phi, 1
  br i1 %2, label %base, label %recursive
base:
  ret i32 %acc.phi
recursive:
  %0 = sub i32 %n.phi, 1
  %1 = mul i32 %acc.phi, %n.phi
  br label %tail_recursion_loop
}
)");
}

// Test Case 2: Void tail recursion - countdown
TEST_F(TailRecursionOptimizationTest, VoidTailRecursion_Countdown) {
    auto voidType = ctx->getVoidType();
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(voidType, {intType});
    auto func = Function::Create(funcType, "countdown", module.get());

    auto n = func->getArg(0);
    n->setName("n");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(0));
    builder->createCondBr(cond, baseBB, recursiveBB);

    builder->setInsertPoint(baseBB);
    builder->createRetVoid();

    builder->setInsertPoint(recursiveBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    builder->createCall(func, {n1});
    builder->createRetVoid();

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define void @countdown(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 0
  br i1 %0, label %base, label %recursive
base:
  ret void
recursive:
  %1 = sub i32 %n, 1
  call void @countdown(i32 %1)
  ret void
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define void @countdown(i32 %n) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %n.phi = phi i32 [ %n, %entry ], [ %0, %recursive ]
  %1 = icmp sle i32 %n.phi, 0
  br i1 %1, label %base, label %recursive
base:
  ret void
recursive:
  %0 = sub i32 %n.phi, 1
  br label %tail_recursion_loop
}
)");
}

// Test Case 3: Single parameter tail recursion - should NOT be optimized
TEST_F(TailRecursionOptimizationTest, SingleParameterTailRecursion_NotTail) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "sum", module.get());

    auto n = func->getArg(0);
    n->setName("n");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, baseBB, recursiveBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(n);

    builder->setInsertPoint(recursiveBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto callInst = builder->createCall(func, {n1});
    auto result = builder->createAdd(n, callInst);
    builder->createRet(result);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @sum(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %recursive
base:
  ret i32 %n
recursive:
  %1 = sub i32 %n, 1
  %2 = call i32 @sum(i32 %1)
  %3 = add i32 %n, %2
  ret i32 %3
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(
        changed);  // This is NOT tail recursion (result is used in addition)

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @sum(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %recursive
base:
  ret i32 %n
recursive:
  %1 = sub i32 %n, 1
  %2 = call i32 @sum(i32 %1)
  %3 = add i32 %n, %2
  ret i32 %3
}
)");
}

// Test Case 4: No tail recursion - fibonacci
TEST_F(TailRecursionOptimizationTest, NoTailRecursion_Fibonacci) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "fibonacci", module.get());

    auto n = func->getArg(0);
    n->setName("n");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, baseBB, recursiveBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(n);

    builder->setInsertPoint(recursiveBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto n2 = builder->createSub(n, builder->getInt32(2));
    auto call1 = builder->createCall(func, {n1});
    auto call2 = builder->createCall(func, {n2});
    auto result = builder->createAdd(call1, call2);
    builder->createRet(result);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @fibonacci(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %recursive
base:
  ret i32 %n
recursive:
  %1 = sub i32 %n, 1
  %2 = sub i32 %n, 2
  %3 = call i32 @fibonacci(i32 %1)
  %4 = call i32 @fibonacci(i32 %2)
  %5 = add i32 %3, %4
  ret i32 %5
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);  // Not tail recursion - multiple calls

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @fibonacci(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %recursive
base:
  ret i32 %n
recursive:
  %1 = sub i32 %n, 1
  %2 = sub i32 %n, 2
  %3 = call i32 @fibonacci(i32 %1)
  %4 = call i32 @fibonacci(i32 %2)
  %5 = add i32 %3, %4
  ret i32 %5
}
)");
}

// Test Case 5: Non-recursive function
TEST_F(TailRecursionOptimizationTest, NonRecursiveFunction) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "add_one", module.get());

    auto n = func->getArg(0);
    n->setName("n");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);

    builder->setInsertPoint(entryBB);
    auto result = builder->createAdd(n, builder->getInt32(1));
    builder->createRet(result);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @add_one(i32 %n) {
entry:
  %0 = add i32 %n, 1
  ret i32 %0
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);  // No recursion at all

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @add_one(i32 %n) {
entry:
  %0 = add i32 %n, 1
  ret i32 %0
}
)");
}

// Test Case 6: Indirect recursion (should not be optimized)
TEST_F(TailRecursionOptimizationTest, IndirectRecursion) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func1 = Function::Create(funcType, "func1", module.get());
    auto func2 = Function::Create(funcType, "func2", module.get());

    auto n1 = func1->getArg(0);
    n1->setName("n");

    auto entryBB1 = BasicBlock::Create(ctx.get(), "entry", func1);
    auto baseBB1 = BasicBlock::Create(ctx.get(), "base", func1);
    auto recursiveBB1 = BasicBlock::Create(ctx.get(), "recursive", func1);

    builder->setInsertPoint(entryBB1);
    auto cond1 = builder->createICmpSLE(n1, builder->getInt32(0));
    builder->createCondBr(cond1, baseBB1, recursiveBB1);

    builder->setInsertPoint(baseBB1);
    builder->createRet(builder->getInt32(0));

    builder->setInsertPoint(recursiveBB1);
    auto n1_dec = builder->createSub(n1, builder->getInt32(1));
    auto callInst1 = builder->createCall(func2, {n1_dec});
    builder->createRet(callInst1);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func1), R"(define i32 @func1(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 0
  br i1 %0, label %base, label %recursive
base:
  ret i32 0
recursive:
  %1 = sub i32 %n, 1
  %2 = call i32 @func2(i32 %1)
  ret i32 %2
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func1, *am);

    EXPECT_FALSE(changed);  // Calls different function, not tail recursion

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func1), R"(define i32 @func1(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 0
  br i1 %0, label %base, label %recursive
base:
  ret i32 0
recursive:
  %1 = sub i32 %n, 1
  %2 = call i32 @func2(i32 %1)
  ret i32 %2
}
)");
}

// Test Case 7: Three parameter tail recursion
TEST_F(TailRecursionOptimizationTest, ThreeParameterTailRecursion) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType, intType});
    auto func = Function::Create(funcType, "three_param", module.get());

    auto a = func->getArg(0);
    auto b = func->getArg(1);
    auto c = func->getArg(2);
    a->setName("a");
    b->setName("b");
    c->setName("c");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(a, builder->getInt32(0));
    builder->createCondBr(cond, baseBB, recursiveBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(b);

    builder->setInsertPoint(recursiveBB);
    auto a1 = builder->createSub(a, builder->getInt32(1));
    auto b1 = builder->createAdd(b, c);
    auto c1 = builder->createMul(c, builder->getInt32(2));
    auto callInst = builder->createCall(func, {a1, b1, c1});
    builder->createRet(callInst);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @three_param(i32 %a, i32 %b, i32 %c) {
entry:
  %0 = icmp sle i32 %a, 0
  br i1 %0, label %base, label %recursive
base:
  ret i32 %b
recursive:
  %1 = sub i32 %a, 1
  %2 = add i32 %b, %c
  %3 = mul i32 %c, 2
  %4 = call i32 @three_param(i32 %1, i32 %2, i32 %3)
  ret i32 %4
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @three_param(i32 %a, i32 %b, i32 %c) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %a.phi = phi i32 [ %a, %entry ], [ %0, %recursive ]
  %b.phi = phi i32 [ %b, %entry ], [ %1, %recursive ]
  %c.phi = phi i32 [ %c, %entry ], [ %2, %recursive ]
  %3 = icmp sle i32 %a.phi, 0
  br i1 %3, label %base, label %recursive
base:
  ret i32 %b.phi
recursive:
  %0 = sub i32 %a.phi, 1
  %1 = add i32 %b.phi, %c.phi
  %2 = mul i32 %c.phi, 2
  br label %tail_recursion_loop
}
)");
}

// Test Case 8: Function with no parameters
TEST_F(TailRecursionOptimizationTest, NoParameterTailRecursion) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "no_param", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);

    builder->setInsertPoint(entryBB);
    auto cond =
        builder->createICmpSLE(builder->getInt32(0), builder->getInt32(0));
    builder->createCondBr(cond, baseBB, recursiveBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(builder->getInt32(42));

    builder->setInsertPoint(recursiveBB);
    auto callInst = builder->createCall(func, {});
    builder->createRet(callInst);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @no_param() {
entry:
  %0 = icmp sle i32 0, 0
  br i1 %0, label %base, label %recursive
base:
  ret i32 42
recursive:
  %1 = call i32 @no_param()
  ret i32 %1
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @no_param() {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %0 = icmp sle i32 0, 0
  br i1 %0, label %base, label %recursive
base:
  ret i32 42
recursive:
  br label %tail_recursion_loop
}
)");
}

// Test Case 9: Function declaration (should not be optimized)
TEST_F(TailRecursionOptimizationTest, FunctionDeclaration) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "declaration", module.get());
    // No basic blocks - just a declaration

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), "define i32 @declaration(i32 %arg0)\n");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);  // No definition to optimize

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), "define i32 @declaration(i32 %arg0)\n");
}

// Test Case 10: Empty function (edge case)
TEST_F(TailRecursionOptimizationTest, EmptyFunction) {
    auto voidType = ctx->getVoidType();
    auto funcType = FunctionType::get(voidType, {});
    auto func = Function::Create(funcType, "empty", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);
    builder->createRetVoid();

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define void @empty() {
entry:
  ret void
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);  // No recursion

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), R"(define void @empty() {
entry:
  ret void
}
)");
}

// Test Case 11: Multi-branch tail recursion (multiple paths with tail calls)
TEST_F(TailRecursionOptimizationTest, MultiBranchTailRecursion) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "multi_branch", module.get());

    auto n = func->getArg(0);
    auto acc = func->getArg(1);
    n->setName("n");
    acc->setName("acc");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto evenBB = BasicBlock::Create(ctx.get(), "even", func);
    auto oddBB = BasicBlock::Create(ctx.get(), "odd", func);

    auto dispatchBB = BasicBlock::Create(ctx.get(), "dispatch", func);
    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, baseBB, dispatchBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(acc);

    builder->setInsertPoint(dispatchBB);
    auto mod = builder->createRem(n, builder->getInt32(2));
    auto isEven = builder->createICmpEQ(mod, builder->getInt32(0));
    builder->createCondBr(isEven, evenBB, oddBB);

    builder->setInsertPoint(evenBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto acc1 = builder->createAdd(acc, n);
    auto c1 = builder->createCall(func, {n1, acc1});
    builder->createRet(c1);

    builder->setInsertPoint(oddBB);
    auto n2 = builder->createSub(n, builder->getInt32(2));
    auto acc2 = builder->createMul(acc, builder->getInt32(2));
    auto c2 = builder->createCall(func, {n2, acc2});
    builder->createRet(c2);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_branch(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %dispatch
base:
  ret i32 %acc
even:
  %1 = sub i32 %n, 1
  %2 = add i32 %acc, %n
  %3 = call i32 @multi_branch(i32 %1, i32 %2)
  ret i32 %3
odd:
  %4 = sub i32 %n, 2
  %5 = mul i32 %acc, 2
  %6 = call i32 @multi_branch(i32 %4, i32 %5)
  ret i32 %6
dispatch:
  %7 = srem i32 %n, 2
  %8 = icmp eq i32 %7, 0
  br i1 %8, label %even, label %odd
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_branch(i32 %n, i32 %acc) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %n.phi = phi i32 [ %n, %entry ], [ %0, %even ], [ %1, %odd ]
  %acc.phi = phi i32 [ %acc, %entry ], [ %2, %even ], [ %3, %odd ]
  %4 = icmp sle i32 %n.phi, 1
  br i1 %4, label %base, label %dispatch
base:
  ret i32 %acc.phi
even:
  %0 = sub i32 %n.phi, 1
  %2 = add i32 %acc.phi, %n.phi
  br label %tail_recursion_loop
odd:
  %1 = sub i32 %n.phi, 2
  %3 = mul i32 %acc.phi, 2
  br label %tail_recursion_loop
dispatch:
  %5 = srem i32 %n.phi, 2
  %6 = icmp eq i32 %5, 0
  br i1 %6, label %even, label %odd
}
)");
}

// Test Case 12: Mixed tail and non-tail recursion
TEST_F(TailRecursionOptimizationTest, MixedTailAndNonTailRecursion) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "mixed", module.get());

    auto n = func->getArg(0);
    auto acc = func->getArg(1);
    n->setName("n");
    acc->setName("acc");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto tailBB = BasicBlock::Create(ctx.get(), "tail", func);
    auto nonTailBB = BasicBlock::Create(ctx.get(), "non_tail", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, baseBB, tailBB);

    builder->setInsertPoint(baseBB);
    auto mod = builder->createRem(n, builder->getInt32(3));
    auto isDiv3 = builder->createICmpEQ(mod, builder->getInt32(0));
    builder->createCondBr(isDiv3, tailBB, nonTailBB);

    builder->setInsertPoint(tailBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto acc1 = builder->createAdd(acc, n);
    auto tailCall = builder->createCall(func, {n1, acc1});
    builder->createRet(tailCall);

    builder->setInsertPoint(nonTailBB);
    auto n2 = builder->createSub(n, builder->getInt32(2));
    auto nonTailCall = builder->createCall(func, {n2, acc});
    auto result = builder->createAdd(nonTailCall, builder->getInt32(1));
    builder->createRet(result);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @mixed(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %tail
base:
  %1 = srem i32 %n, 3
  %2 = icmp eq i32 %1, 0
  br i1 %2, label %tail, label %non_tail
tail:
  %3 = sub i32 %n, 1
  %4 = add i32 %acc, %n
  %5 = call i32 @mixed(i32 %3, i32 %4)
  ret i32 %5
non_tail:
  %6 = sub i32 %n, 2
  %7 = call i32 @mixed(i32 %6, i32 %acc)
  %8 = add i32 %7, 1
  ret i32 %8
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should NOT optimize because there are non-tail recursive calls
    EXPECT_FALSE(changed);

    // Check IR after optimization - should be unchanged
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @mixed(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %tail
base:
  %1 = srem i32 %n, 3
  %2 = icmp eq i32 %1, 0
  br i1 %2, label %tail, label %non_tail
tail:
  %3 = sub i32 %n, 1
  %4 = add i32 %acc, %n
  %5 = call i32 @mixed(i32 %3, i32 %4)
  ret i32 %5
non_tail:
  %6 = sub i32 %n, 2
  %7 = call i32 @mixed(i32 %6, i32 %acc)
  %8 = add i32 %7, 1
  ret i32 %8
}
)");
}

// Test Case 13: Multiple tail calls in different branches
TEST_F(TailRecursionOptimizationTest, MultipleTailCallsInDifferentBranches) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "multi_tail", module.get());

    auto n = func->getArg(0);
    auto acc = func->getArg(1);
    n->setName("n");
    acc->setName("acc");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto baseBB = BasicBlock::Create(ctx.get(), "base", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);
    auto leftBB = BasicBlock::Create(ctx.get(), "left", func);
    auto rightBB = BasicBlock::Create(ctx.get(), "right", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, baseBB, exitBB);

    builder->setInsertPoint(baseBB);
    auto mod = builder->createRem(n, builder->getInt32(2));
    auto isEven = builder->createICmpEQ(mod, builder->getInt32(0));
    builder->createCondBr(isEven, leftBB, rightBB);

    builder->setInsertPoint(exitBB);
    builder->createRet(acc);

    builder->setInsertPoint(leftBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto acc1 = builder->createAdd(acc, n);
    auto leftCall = builder->createCall(func, {n1, acc1});
    builder->createRet(leftCall);

    builder->setInsertPoint(rightBB);
    auto n2 = builder->createSub(n, builder->getInt32(2));
    auto acc2 = builder->createMul(acc, builder->getInt32(2));
    auto rightCall = builder->createCall(func, {n2, acc2});
    builder->createRet(rightCall);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_tail(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %base, label %exit
base:
  %1 = srem i32 %n, 2
  %2 = icmp eq i32 %1, 0
  br i1 %2, label %left, label %right
exit:
  ret i32 %acc
left:
  %3 = sub i32 %n, 1
  %4 = add i32 %acc, %n
  %5 = call i32 @multi_tail(i32 %3, i32 %4)
  ret i32 %5
right:
  %6 = sub i32 %n, 2
  %7 = mul i32 %acc, 2
  %8 = call i32 @multi_tail(i32 %6, i32 %7)
  ret i32 %8
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization - both tail calls optimized into single loop
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @multi_tail(i32 %n, i32 %acc) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %n.phi = phi i32 [ %n, %entry ], [ %0, %left ], [ %1, %right ]
  %acc.phi = phi i32 [ %acc, %entry ], [ %2, %left ], [ %3, %right ]
  %4 = icmp sle i32 %n.phi, 1
  br i1 %4, label %base, label %exit
base:
  %5 = srem i32 %n.phi, 2
  %6 = icmp eq i32 %5, 0
  br i1 %6, label %left, label %right
exit:
  ret i32 %acc.phi
left:
  %0 = sub i32 %n.phi, 1
  %2 = add i32 %acc.phi, %n.phi
  br label %tail_recursion_loop
right:
  %1 = sub i32 %n.phi, 2
  %3 = mul i32 %acc.phi, 2
  br label %tail_recursion_loop
}
)");
}

// Test Case 14: Tail recursion with loop structure
TEST_F(TailRecursionOptimizationTest, TailRecursionWithLoop) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "loop_tail", module.get());

    auto n = func->getArg(0);
    auto acc = func->getArg(1);
    n->setName("n");
    acc->setName("acc");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop_body", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, exitBB, loopBB);

    builder->setInsertPoint(loopBB);
    auto i = builder->createPHI(intType);
    i->addIncoming(builder->getInt32(0), entryBB);
    auto loopCond = builder->createICmpSLT(i, builder->getInt32(3));
    builder->createCondBr(loopCond, loopBodyBB, recursiveBB);

    builder->setInsertPoint(loopBodyBB);
    auto iNext = builder->createAdd(i, builder->getInt32(1));
    i->addIncoming(iNext, loopBodyBB);
    builder->createBr(loopBB);

    builder->setInsertPoint(recursiveBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto acc1 = builder->createAdd(acc, i);
    auto callInst = builder->createCall(func, {n1, acc1});
    builder->createRet(callInst);

    builder->setInsertPoint(exitBB);
    builder->createRet(acc);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @loop_tail(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %exit, label %loop
loop:
  %1 = phi i32 [ 0, %entry ], [ %2, %loop_body ]
  %3 = icmp slt i32 %1, 3
  br i1 %3, label %loop_body, label %recursive
loop_body:
  %2 = add i32 %1, 1
  br label %loop
recursive:
  %4 = sub i32 %n, 1
  %5 = add i32 %acc, %1
  %6 = call i32 @loop_tail(i32 %4, i32 %5)
  ret i32 %6
exit:
  ret i32 %acc
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @loop_tail(i32 %n, i32 %acc) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %n.phi = phi i32 [ %n, %entry ], [ %0, %recursive ]
  %acc.phi = phi i32 [ %acc, %entry ], [ %1, %recursive ]
  %2 = icmp sle i32 %n.phi, 1
  br i1 %2, label %exit, label %loop
loop:
  %3 = phi i32 [ 0, %tail_recursion_loop ], [ %4, %loop_body ]
  %5 = icmp slt i32 %3, 3
  br i1 %5, label %loop_body, label %recursive
loop_body:
  %4 = add i32 %3, 1
  br label %loop
recursive:
  %0 = sub i32 %n.phi, 1
  %1 = add i32 %acc.phi, %3
  br label %tail_recursion_loop
exit:
  ret i32 %acc.phi
}
)");
}

// Test Case 15: Complex multi-return environment
TEST_F(TailRecursionOptimizationTest, ComplexMultiReturnEnvironment) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "complex_ret", module.get());

    auto n = func->getArg(0);
    auto acc = func->getArg(1);
    n->setName("n");
    acc->setName("acc");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto ret1BB = BasicBlock::Create(ctx.get(), "ret1", func);
    auto ret2BB = BasicBlock::Create(ctx.get(), "ret2", func);
    auto ret3BB = BasicBlock::Create(ctx.get(), "ret3", func);
    auto tailBB = BasicBlock::Create(ctx.get(), "tail", func);

    builder->setInsertPoint(entryBB);
    auto cond1 = builder->createICmpSLE(n, builder->getInt32(0));
    builder->createCondBr(cond1, ret1BB, ret2BB);

    builder->setInsertPoint(ret1BB);
    builder->createRet(acc);

    builder->setInsertPoint(ret2BB);
    auto cond2 = builder->createICmpEQ(n, builder->getInt32(1));
    builder->createCondBr(cond2, ret3BB, tailBB);

    builder->setInsertPoint(ret3BB);
    auto result = builder->createAdd(acc, builder->getInt32(1));
    builder->createRet(result);

    builder->setInsertPoint(tailBB);
    auto mod = builder->createRem(n, builder->getInt32(2));
    auto isEven = builder->createICmpEQ(mod, builder->getInt32(0));
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto acc1 = builder->createSelect(
        isEven, builder->createMul(acc, builder->getInt32(2)),
        builder->createAdd(acc, n));
    auto callInst = builder->createCall(func, {n1, acc1});
    builder->createRet(callInst);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @complex_ret(i32 %n, i32 %acc) {
entry:
  %0 = icmp sle i32 %n, 0
  br i1 %0, label %ret1, label %ret2
ret1:
  ret i32 %acc
ret2:
  %1 = icmp eq i32 %n, 1
  br i1 %1, label %ret3, label %tail
ret3:
  %2 = add i32 %acc, 1
  ret i32 %2
tail:
  %3 = srem i32 %n, 2
  %4 = icmp eq i32 %3, 0
  %5 = sub i32 %n, 1
  %6 = mul i32 %acc, 2
  %7 = add i32 %acc, %n
  %8 = select i1 %4, i32 %6, i32 %7
  %9 = call i32 @complex_ret(i32 %5, i32 %8)
  ret i32 %9
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Check IR after optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @complex_ret(i32 %n, i32 %acc) {
entry:
  br label %tail_recursion_loop
tail_recursion_loop:
  %n.phi = phi i32 [ %n, %entry ], [ %0, %tail ]
  %acc.phi = phi i32 [ %acc, %entry ], [ %1, %tail ]
  %2 = icmp sle i32 %n.phi, 0
  br i1 %2, label %ret1, label %ret2
ret1:
  ret i32 %acc.phi
ret2:
  %3 = icmp eq i32 %n.phi, 1
  br i1 %3, label %ret3, label %tail
ret3:
  %4 = add i32 %acc.phi, 1
  ret i32 %4
tail:
  %5 = srem i32 %n.phi, 2
  %6 = icmp eq i32 %5, 0
  %0 = sub i32 %n.phi, 1
  %7 = mul i32 %acc.phi, 2
  %8 = add i32 %acc.phi, %n.phi
  %1 = select i1 %6, i32 %7, i32 %8
  br label %tail_recursion_loop
}
)");
}

// Test Case 16: Conservative behavior - function with one tail call and one
// non-tail call
TEST_F(TailRecursionOptimizationTest, ConservativeBehavior_OneNonTailCall) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "conservative", module.get());

    auto n = func->getArg(0);
    n->setName("n");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto tailBB = BasicBlock::Create(ctx.get(), "tail", func);
    auto nonTailBB = BasicBlock::Create(ctx.get(), "non_tail", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, exitBB, tailBB);

    builder->setInsertPoint(tailBB);
    auto cond2 = builder->createICmpEQ(n, builder->getInt32(2));
    builder->createCondBr(cond2, nonTailBB, exitBB);

    builder->setInsertPoint(nonTailBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto nonTailCall = builder->createCall(func, {n1});
    auto result = builder->createAdd(nonTailCall, builder->getInt32(1));
    builder->createRet(result);

    builder->setInsertPoint(exitBB);
    auto n2 = builder->createSub(n, builder->getInt32(2));
    auto tailCall = builder->createCall(func, {n2});
    builder->createRet(tailCall);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @conservative(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %exit, label %tail
tail:
  %1 = icmp eq i32 %n, 2
  br i1 %1, label %non_tail, label %exit
non_tail:
  %2 = sub i32 %n, 1
  %3 = call i32 @conservative(i32 %2)
  %4 = add i32 %3, 1
  ret i32 %4
exit:
  %5 = sub i32 %n, 2
  %6 = call i32 @conservative(i32 %5)
  ret i32 %6
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should NOT optimize because there is a non-tail recursive call
    EXPECT_FALSE(changed);

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @conservative(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %exit, label %tail
tail:
  %1 = icmp eq i32 %n, 2
  br i1 %1, label %non_tail, label %exit
non_tail:
  %2 = sub i32 %n, 1
  %3 = call i32 @conservative(i32 %2)
  %4 = add i32 %3, 1
  ret i32 %4
exit:
  %5 = sub i32 %n, 2
  %6 = call i32 @conservative(i32 %5)
  ret i32 %6
}
)");
}

// Test Case 17: All-or-nothing behavior - multiple non-tail calls
TEST_F(TailRecursionOptimizationTest,
       AllOrNothingBehavior_MultipleNonTailCalls) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "all_or_nothing", module.get());

    auto n = func->getArg(0);
    n->setName("n");

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto recursiveBB = BasicBlock::Create(ctx.get(), "recursive", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSLE(n, builder->getInt32(1));
    builder->createCondBr(cond, exitBB, recursiveBB);

    builder->setInsertPoint(recursiveBB);
    auto n1 = builder->createSub(n, builder->getInt32(1));
    auto n2 = builder->createSub(n, builder->getInt32(2));
    auto call1 = builder->createCall(func, {n1});
    auto call2 = builder->createCall(func, {n2});
    auto result = builder->createAdd(call1, call2);
    builder->createRet(result);

    builder->setInsertPoint(exitBB);
    builder->createRet(n);

    // Check IR before optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @all_or_nothing(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %exit, label %recursive
recursive:
  %1 = sub i32 %n, 1
  %2 = sub i32 %n, 2
  %3 = call i32 @all_or_nothing(i32 %1)
  %4 = call i32 @all_or_nothing(i32 %2)
  %5 = add i32 %3, %4
  ret i32 %5
exit:
  ret i32 %n
}
)");

    TailRecursionOptimizationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should NOT optimize because both calls are non-tail
    EXPECT_FALSE(changed);

    // Check IR after optimization (should be unchanged)
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @all_or_nothing(i32 %n) {
entry:
  %0 = icmp sle i32 %n, 1
  br i1 %0, label %exit, label %recursive
recursive:
  %1 = sub i32 %n, 1
  %2 = sub i32 %n, 2
  %3 = call i32 @all_or_nothing(i32 %1)
  %4 = call i32 @all_or_nothing(i32 %2)
  %5 = add i32 %3, %4
  ret i32 %5
exit:
  ret i32 %n
}
)");
}