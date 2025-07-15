#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "Pass/Pass.h"
#include "Pass/Transform/Mem2RegPass.h"

using namespace midend;

class Mem2RegTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();
        am->registerAnalysisType<DominanceAnalysis>();
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

TEST_F(Mem2RegTest, SimpleAllocaPromoted) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create alloca
    auto alloca = builder->createAlloca(intType, nullptr, "x");

    // Store 42 to the alloca
    auto val = builder->getInt32(42);
    builder->createStore(val, alloca);

    // Load from the alloca
    auto load = builder->createLoad(alloca, "load_x");

    // Return the loaded value
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %x = alloca i32\n"
              "  store i32 42, i32* %x\n"
              "  %load_x = load i32, i32* %x\n"
              "  ret i32 %load_x\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 42\n"
              "}\n");
}

// Basic edge case tests
TEST_F(Mem2RegTest, AllocaWithMultipleStores) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "x");

    // Multiple stores to same alloca
    builder->createStore(builder->getInt32(10), alloca);
    builder->createStore(builder->getInt32(20), alloca);
    builder->createStore(builder->getInt32(30), alloca);

    // Load final value
    auto load = builder->createLoad(alloca, "final_x");
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %x = alloca i32\n"
              "  store i32 10, i32* %x\n"
              "  store i32 20, i32* %x\n"
              "  store i32 30, i32* %x\n"
              "  %final_x = load i32, i32* %x\n"
              "  ret i32 %final_x\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 30\n"
              "}\n");
}

TEST_F(Mem2RegTest, AllocaWithUnusedLoad) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "x");
    builder->createStore(builder->getInt32(42), alloca);

    // Unused load
    auto unusedLoad = builder->createLoad(alloca, "unused");
    (void)unusedLoad;

    // Return constant instead of loaded value
    builder->createRet(builder->getInt32(100));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %x = alloca i32\n"
              "  store i32 42, i32* %x\n"
              "  %unused = load i32, i32* %x\n"
              "  ret i32 100\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 100\n"
              "}\n");
}

TEST_F(Mem2RegTest, MultipleAllocasInSameFunction) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create multiple allocas
    auto alloca1 = builder->createAlloca(intType, nullptr, "x");
    auto alloca2 = builder->createAlloca(intType, nullptr, "y");
    auto alloca3 = builder->createAlloca(intType, nullptr, "z");

    // Store and load from each
    builder->createStore(builder->getInt32(10), alloca1);
    builder->createStore(builder->getInt32(20), alloca2);
    builder->createStore(builder->getInt32(30), alloca3);

    auto load1 = builder->createLoad(alloca1, "x_val");
    auto load2 = builder->createLoad(alloca2, "y_val");
    auto load3 = builder->createLoad(alloca3, "z_val");

    // Add all values and return
    auto add1 = builder->createAdd(load1, load2, "tmp1");
    auto add2 = builder->createAdd(add1, load3, "result");
    builder->createRet(add2);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %x = alloca i32\n"
              "  %y = alloca i32\n"
              "  %z = alloca i32\n"
              "  store i32 10, i32* %x\n"
              "  store i32 20, i32* %y\n"
              "  store i32 30, i32* %z\n"
              "  %x_val = load i32, i32* %x\n"
              "  %y_val = load i32, i32* %y\n"
              "  %z_val = load i32, i32* %z\n"
              "  %tmp1 = add i32 %x_val, %y_val\n"
              "  %result = add i32 %tmp1, %z_val\n"
              "  ret i32 %result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %tmp1 = add i32 10, 20\n"
              "  %result = add i32 %tmp1, 30\n"
              "  ret i32 %result\n"
              "}\n");
}

// Simple if-else tests
TEST_F(Mem2RegTest, SimpleIfElseWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "result");

    // Condition: n > 0
    auto condition =
        builder->createICmpSGT(func->getArg(0), builder->getInt32(0), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    // True branch
    builder->setInsertPoint(trueBB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(mergeBB);

    // False branch
    builder->setInsertPoint(falseBB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(mergeBB);

    // Merge block
    builder->setInsertPoint(mergeBB);
    auto load = builder->createLoad(alloca, "final_result");
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %result = alloca i32\n"
              "  %cond = icmp sgt i32 %arg0, 0\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  store i32 100, i32* %result\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  store i32 200, i32* %result\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %final_result = load i32, i32* %result\n"
              "  ret i32 %final_result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        "define i32 @test_func(i32 %arg0) {\n"
        "entry:\n"
        "  %cond = icmp sgt i32 %arg0, 0\n"
        "  br i1 %cond, label %if.true, label %if.false\n"
        "if.true:\n"
        "  br label %if.merge\n"
        "if.false:\n"
        "  br label %if.merge\n"
        "if.merge:\n"
        "  %result.phi.1 = phi i32 [ 100, %if.true ], [ 200, %if.false ]\n"
        "  ret i32 %result.phi.1\n"
        "}\n");
}

TEST_F(Mem2RegTest, IfElseWithMultipleAllocas) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca1 = builder->createAlloca(intType, nullptr, "x");
    auto alloca2 = builder->createAlloca(intType, nullptr, "y");

    // Initial stores
    builder->createStore(builder->getInt32(10), alloca1);
    builder->createStore(builder->getInt32(20), alloca2);

    auto condition =
        builder->createICmpEQ(func->getArg(0), builder->getInt32(1), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    // True branch - modify x
    builder->setInsertPoint(trueBB);
    builder->createStore(builder->getInt32(100), alloca1);
    builder->createBr(mergeBB);

    // False branch - modify y
    builder->setInsertPoint(falseBB);
    builder->createStore(builder->getInt32(200), alloca2);
    builder->createBr(mergeBB);

    // Merge block - use both values
    builder->setInsertPoint(mergeBB);
    auto load1 = builder->createLoad(alloca1, "x_val");
    auto load2 = builder->createLoad(alloca2, "y_val");
    auto result = builder->createAdd(load1, load2, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %x = alloca i32\n"
              "  %y = alloca i32\n"
              "  store i32 10, i32* %x\n"
              "  store i32 20, i32* %y\n"
              "  %cond = icmp eq i32 %arg0, 1\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  store i32 100, i32* %x\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  store i32 200, i32* %y\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %x_val = load i32, i32* %x\n"
              "  %y_val = load i32, i32* %y\n"
              "  %result = add i32 %x_val, %y_val\n"
              "  ret i32 %result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %cond = icmp eq i32 %arg0, 1\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %y.phi.1 = phi i32 [ 20, %if.true ], [ 200, %if.false ]\n"
              "  %x.phi.2 = phi i32 [ 100, %if.true ], [ 10, %if.false ]\n"
              "  %result = add i32 %x.phi.2, %y.phi.1\n"
              "  ret i32 %result\n"
              "}\n");
}

// Loop tests
TEST_F(Mem2RegTest, SimpleLoopWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop.header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto loopExitBB = BasicBlock::Create(ctx.get(), "loop.exit", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "sum");
    auto allocaI = builder->createAlloca(intType, nullptr, "i");

    // Initialize sum = 0, i = 0
    builder->createStore(builder->getInt32(0), alloca);
    builder->createStore(builder->getInt32(0), allocaI);
    builder->createBr(loopHeaderBB);

    // Loop header: check i < n
    builder->setInsertPoint(loopHeaderBB);
    auto loadI = builder->createLoad(allocaI, "i_val");
    auto condition = builder->createICmpSLT(loadI, func->getArg(0), "cond");
    builder->createCondBr(condition, loopBodyBB, loopExitBB);

    // Loop body: sum += i; i++
    builder->setInsertPoint(loopBodyBB);
    auto currentSum = builder->createLoad(alloca, "sum_val");
    auto currentI = builder->createLoad(allocaI, "i_val2");
    auto newSum = builder->createAdd(currentSum, currentI, "new_sum");
    auto newI = builder->createAdd(currentI, builder->getInt32(1), "new_i");
    builder->createStore(newSum, alloca);
    builder->createStore(newI, allocaI);
    builder->createBr(loopHeaderBB);

    // Loop exit: return sum
    builder->setInsertPoint(loopExitBB);
    auto finalSum = builder->createLoad(alloca, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %sum = alloca i32\n"
              "  %i = alloca i32\n"
              "  store i32 0, i32* %sum\n"
              "  store i32 0, i32* %i\n"
              "  br label %loop.header\n"
              "loop.header:\n"
              "  %i_val = load i32, i32* %i\n"
              "  %cond = icmp slt i32 %i_val, %arg0\n"
              "  br i1 %cond, label %loop.body, label %loop.exit\n"
              "loop.body:\n"
              "  %sum_val = load i32, i32* %sum\n"
              "  %i_val2 = load i32, i32* %i\n"
              "  %new_sum = add i32 %sum_val, %i_val2\n"
              "  %new_i = add i32 %i_val2, 1\n"
              "  store i32 %new_sum, i32* %sum\n"
              "  store i32 %new_i, i32* %i\n"
              "  br label %loop.header\n"
              "loop.exit:\n"
              "  %final_sum = load i32, i32* %sum\n"
              "  ret i32 %final_sum\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  br label %loop.header\n"
              "loop.header:\n"
              "  %i.phi.1 = phi i32 [ 0, %entry ], [ %new_i, %loop.body ]\n"
              "  %sum.phi.2 = phi i32 [ 0, %entry ], [ %new_sum, %loop.body ]\n"
              "  %cond = icmp slt i32 %i.phi.1, %arg0\n"
              "  br i1 %cond, label %loop.body, label %loop.exit\n"
              "loop.body:\n"
              "  %new_sum = add i32 %sum.phi.2, %i.phi.1\n"
              "  %new_i = add i32 %i.phi.1, 1\n"
              "  br label %loop.header\n"
              "loop.exit:\n"
              "  ret i32 %sum.phi.2\n"
              "}\n");
}

// Nested if-else tests
TEST_F(Mem2RegTest, NestedIfElseWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "result");

    // Simple nested test: if (x > 10) result = 100; else result = 200;
    auto condition =
        builder->createICmpSGT(func->getArg(0), builder->getInt32(10), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(mergeBB);

    builder->setInsertPoint(falseBB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    auto load = builder->createLoad(alloca, "final_result");
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %result = alloca i32\n"
              "  %cond = icmp sgt i32 %arg0, 10\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  store i32 100, i32* %result\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  store i32 200, i32* %result\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %final_result = load i32, i32* %result\n"
              "  ret i32 %final_result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        "define i32 @test_func(i32 %arg0) {\n"
        "entry:\n"
        "  %cond = icmp sgt i32 %arg0, 10\n"
        "  br i1 %cond, label %if.true, label %if.false\n"
        "if.true:\n"
        "  br label %if.merge\n"
        "if.false:\n"
        "  br label %if.merge\n"
        "if.merge:\n"
        "  %result.phi.1 = phi i32 [ 100, %if.true ], [ 200, %if.false ]\n"
        "  ret i32 %result.phi.1\n"
        "}\n");
}

// Simple loop with conditional test
TEST_F(Mem2RegTest, LoopWithConditional) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "counter");
    builder->createStore(builder->getInt32(0), alloca);
    builder->createBr(loopBB);

    builder->setInsertPoint(loopBB);
    auto load = builder->createLoad(alloca, "current");
    auto incremented = builder->createAdd(load, builder->getInt32(1), "inc");
    builder->createStore(incremented, alloca);
    auto condition =
        builder->createICmpSLT(incremented, builder->getInt32(5), "cond");
    builder->createCondBr(condition, loopBB, exitBB);

    builder->setInsertPoint(exitBB);
    auto finalLoad = builder->createLoad(alloca, "final");
    builder->createRet(finalLoad);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %counter = alloca i32\n"
              "  store i32 0, i32* %counter\n"
              "  br label %loop\n"
              "loop:\n"
              "  %current = load i32, i32* %counter\n"
              "  %inc = add i32 %current, 1\n"
              "  store i32 %inc, i32* %counter\n"
              "  %cond = icmp slt i32 %inc, 5\n"
              "  br i1 %cond, label %loop, label %exit\n"
              "exit:\n"
              "  %final = load i32, i32* %counter\n"
              "  ret i32 %final\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  br label %loop\n"
              "loop:\n"
              "  %counter.phi.1 = phi i32 [ 0, %entry ], [ %inc, %loop ]\n"
              "  %inc = add i32 %counter.phi.1, 1\n"
              "  %cond = icmp slt i32 %inc, 5\n"
              "  br i1 %cond, label %loop, label %exit\n"
              "exit:\n"
              "  ret i32 %inc\n"
              "}\n");
}

TEST_F(Mem2RegTest, AllocaWithNoLoads) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "write_only");
    builder->createStore(builder->getInt32(42), alloca);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createRet(builder->getInt32(999));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %write_only = alloca i32\n"
              "  store i32 42, i32* %write_only\n"
              "  store i32 100, i32* %write_only\n"
              "  ret i32 999\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 999\n"
              "}\n");
}

TEST_F(Mem2RegTest, AllocaWithSelfReference) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "accumulator");
    builder->createStore(func->getArg(0), alloca);

    auto load1 = builder->createLoad(alloca, "val1");
    auto doubled = builder->createMul(load1, builder->getInt32(2), "doubled");
    builder->createStore(doubled, alloca);

    auto load2 = builder->createLoad(alloca, "val2");
    builder->createRet(load2);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %accumulator = alloca i32\n"
              "  store i32 %arg0, i32* %accumulator\n"
              "  %val1 = load i32, i32* %accumulator\n"
              "  %doubled = mul i32 %val1, 2\n"
              "  store i32 %doubled, i32* %accumulator\n"
              "  %val2 = load i32, i32* %accumulator\n"
              "  ret i32 %val2\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %doubled = mul i32 %arg0, 2\n"
              "  ret i32 %doubled\n"
              "}\n");
}

TEST_F(Mem2RegTest, AllocaWithDifferentTypes) {
    auto intType = ctx->getIntegerType(32);
    auto floatType = ctx->getFloatType();
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto intAlloca = builder->createAlloca(intType, nullptr, "int_var");
    auto floatAlloca = builder->createAlloca(floatType, nullptr, "float_var");

    builder->createStore(builder->getInt32(42), intAlloca);
    builder->createStore(builder->getFloat(3.14f), floatAlloca);

    auto intLoad = builder->createLoad(intAlloca, "int_load");
    auto floatLoad = builder->createLoad(floatAlloca, "float_load");
    auto floatToInt = builder->createFPToSI(floatLoad, intType, "float_to_int");
    auto result = builder->createAdd(intLoad, floatToInt, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %int_var = alloca i32\n"
              "  %float_var = alloca float\n"
              "  store i32 42, i32* %int_var\n"
              "  store float 3.140000e+00, float* %float_var\n"
              "  %int_load = load i32, i32* %int_var\n"
              "  %float_load = load float, float* %float_var\n"
              "  %float_to_int = fptosi float %float_load to i32\n"
              "  %result = add i32 %int_load, %float_to_int\n"
              "  ret i32 %result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %float_to_int = fptosi float 3.140000e+00 to i32\n"
              "  %result = add i32 42, %float_to_int\n"
              "  ret i32 %result\n"
              "}\n");
}

// Complex control flow tests
TEST_F(Mem2RegTest, ComplexControlFlowWithChainedIfs) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto case1BB = BasicBlock::Create(ctx.get(), "case1", func);
    auto case2BB = BasicBlock::Create(ctx.get(), "case2", func);
    auto case3BB = BasicBlock::Create(ctx.get(), "case3", func);
    auto defaultBB = BasicBlock::Create(ctx.get(), "default", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "result");

    // Chain of if-else statements simulating a switch
    auto cond1 =
        builder->createICmpEQ(func->getArg(0), builder->getInt32(1), "cond1");
    builder->createCondBr(cond1, case1BB, case2BB);

    builder->setInsertPoint(case1BB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(exitBB);

    builder->setInsertPoint(case2BB);
    auto cond2 =
        builder->createICmpEQ(func->getArg(0), builder->getInt32(2), "cond2");
    builder->createCondBr(cond2, case3BB, defaultBB);

    builder->setInsertPoint(case3BB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(exitBB);

    builder->setInsertPoint(defaultBB);
    builder->createStore(builder->getInt32(999), alloca);
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    auto load = builder->createLoad(alloca, "final_result");
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %result = alloca i32\n"
              "  %cond1 = icmp eq i32 %arg0, 1\n"
              "  br i1 %cond1, label %case1, label %case2\n"
              "case1:\n"
              "  store i32 100, i32* %result\n"
              "  br label %exit\n"
              "case2:\n"
              "  %cond2 = icmp eq i32 %arg0, 2\n"
              "  br i1 %cond2, label %case3, label %default\n"
              "case3:\n"
              "  store i32 200, i32* %result\n"
              "  br label %exit\n"
              "default:\n"
              "  store i32 999, i32* %result\n"
              "  br label %exit\n"
              "exit:\n"
              "  %final_result = load i32, i32* %result\n"
              "  ret i32 %final_result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %cond1 = icmp eq i32 %arg0, 1\n"
              "  br i1 %cond1, label %case1, label %case2\n"
              "case1:\n"
              "  br label %exit\n"
              "case2:\n"
              "  %cond2 = icmp eq i32 %arg0, 2\n"
              "  br i1 %cond2, label %case3, label %default\n"
              "case3:\n"
              "  br label %exit\n"
              "default:\n"
              "  br label %exit\n"
              "exit:\n"
              "  %result.phi.1 = phi i32 [ 100, %case1 ], [ 200, %case3 ], [ "
              "999, %default ]\n"
              "  ret i32 %result.phi.1\n"
              "}\n");
}

TEST_F(Mem2RegTest, ComplexNestedControlFlow) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func =
        Function::Create(funcType, "test_func", {"arg0", "arg1"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerTrueBB = BasicBlock::Create(ctx.get(), "outer.true", func);
    auto outerFalseBB = BasicBlock::Create(ctx.get(), "outer.false", func);
    auto innerTrueBB = BasicBlock::Create(ctx.get(), "inner.true", func);
    auto innerFalseBB = BasicBlock::Create(ctx.get(), "inner.false", func);
    auto innerMergeBB = BasicBlock::Create(ctx.get(), "inner.merge", func);
    auto outerMergeBB = BasicBlock::Create(ctx.get(), "outer.merge", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "result");
    builder->createStore(builder->getInt32(0), alloca);

    auto outerCond = builder->createICmpSGT(func->getArg(0),
                                            builder->getInt32(0), "outer_cond");
    builder->createCondBr(outerCond, outerTrueBB, outerFalseBB);

    // Outer true branch with nested if
    builder->setInsertPoint(outerTrueBB);
    auto innerCond = builder->createICmpSGT(
        func->getArg(1), builder->getInt32(10), "inner_cond");
    builder->createCondBr(innerCond, innerTrueBB, innerFalseBB);

    builder->setInsertPoint(innerTrueBB);
    builder->createStore(builder->getInt32(100), alloca);
    builder->createBr(innerMergeBB);

    builder->setInsertPoint(innerFalseBB);
    builder->createStore(builder->getInt32(50), alloca);
    builder->createBr(innerMergeBB);

    builder->setInsertPoint(innerMergeBB);
    builder->createBr(outerMergeBB);

    // Outer false branch
    builder->setInsertPoint(outerFalseBB);
    builder->createStore(builder->getInt32(200), alloca);
    builder->createBr(outerMergeBB);

    builder->setInsertPoint(outerMergeBB);
    auto load = builder->createLoad(alloca, "final_result");
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0, i32 %arg1) {\n"
              "entry:\n"
              "  %result = alloca i32\n"
              "  store i32 0, i32* %result\n"
              "  %outer_cond = icmp sgt i32 %arg0, 0\n"
              "  br i1 %outer_cond, label %outer.true, label %outer.false\n"
              "outer.true:\n"
              "  %inner_cond = icmp sgt i32 %arg1, 10\n"
              "  br i1 %inner_cond, label %inner.true, label %inner.false\n"
              "outer.false:\n"
              "  store i32 200, i32* %result\n"
              "  br label %outer.merge\n"
              "inner.true:\n"
              "  store i32 100, i32* %result\n"
              "  br label %inner.merge\n"
              "inner.false:\n"
              "  store i32 50, i32* %result\n"
              "  br label %inner.merge\n"
              "inner.merge:\n"
              "  br label %outer.merge\n"
              "outer.merge:\n"
              "  %final_result = load i32, i32* %result\n"
              "  ret i32 %final_result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        "define i32 @test_func(i32 %arg0, i32 %arg1) {\n"
        "entry:\n"
        "  %outer_cond = icmp sgt i32 %arg0, 0\n"
        "  br i1 %outer_cond, label %outer.true, label %outer.false\n"
        "outer.true:\n"
        "  %inner_cond = icmp sgt i32 %arg1, 10\n"
        "  br i1 %inner_cond, label %inner.true, label %inner.false\n"
        "outer.false:\n"
        "  br label %outer.merge\n"
        "inner.true:\n"
        "  br label %inner.merge\n"
        "inner.false:\n"
        "  br label %inner.merge\n"
        "inner.merge:\n"
        "  %result.phi.1 = phi i32 [ 100, %inner.true ], [ 50, %inner.false ]\n"
        "  br label %outer.merge\n"
        "outer.merge:\n"
        "  %result.phi.2 = phi i32 [ %result.phi.1, %inner.merge ], [ 200, "
        "%outer.false ]\n"
        "  ret i32 %result.phi.2\n"
        "}\n");
}

// Advanced multiple alloca tests
TEST_F(Mem2RegTest, MultipleAllocasWithInterdependencies) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func =
        Function::Create(funcType, "test_func", {"arg0", "arg1"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto allocaA = builder->createAlloca(intType, nullptr, "a");
    auto allocaB = builder->createAlloca(intType, nullptr, "b");
    auto allocaC = builder->createAlloca(intType, nullptr, "c");

    // Initialize with arguments
    builder->createStore(func->getArg(0), allocaA);
    builder->createStore(func->getArg(1), allocaB);

    // Interdependent calculations: c = a + b, a = a * 2, b = c - a
    auto loadA1 = builder->createLoad(allocaA, "a1");
    auto loadB1 = builder->createLoad(allocaB, "b1");
    auto c_val = builder->createAdd(loadA1, loadB1, "c_val");
    builder->createStore(c_val, allocaC);

    auto doubleA = builder->createMul(loadA1, builder->getInt32(2), "double_a");
    builder->createStore(doubleA, allocaA);

    auto loadC = builder->createLoad(allocaC, "c_load");
    auto loadA2 = builder->createLoad(allocaA, "a2");
    auto newB = builder->createSub(loadC, loadA2, "new_b");
    builder->createStore(newB, allocaB);

    // Final result: a + b + c
    auto finalA = builder->createLoad(allocaA, "final_a");
    auto finalB = builder->createLoad(allocaB, "final_b");
    auto finalC = builder->createLoad(allocaC, "final_c");
    auto sum1 = builder->createAdd(finalA, finalB, "sum1");
    auto result = builder->createAdd(sum1, finalC, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0, i32 %arg1) {\n"
              "entry:\n"
              "  %a = alloca i32\n"
              "  %b = alloca i32\n"
              "  %c = alloca i32\n"
              "  store i32 %arg0, i32* %a\n"
              "  store i32 %arg1, i32* %b\n"
              "  %a1 = load i32, i32* %a\n"
              "  %b1 = load i32, i32* %b\n"
              "  %c_val = add i32 %a1, %b1\n"
              "  store i32 %c_val, i32* %c\n"
              "  %double_a = mul i32 %a1, 2\n"
              "  store i32 %double_a, i32* %a\n"
              "  %c_load = load i32, i32* %c\n"
              "  %a2 = load i32, i32* %a\n"
              "  %new_b = sub i32 %c_load, %a2\n"
              "  store i32 %new_b, i32* %b\n"
              "  %final_a = load i32, i32* %a\n"
              "  %final_b = load i32, i32* %b\n"
              "  %final_c = load i32, i32* %c\n"
              "  %sum1 = add i32 %final_a, %final_b\n"
              "  %result = add i32 %sum1, %final_c\n"
              "  ret i32 %result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0, i32 %arg1) {\n"
              "entry:\n"
              "  %c_val = add i32 %arg0, %arg1\n"
              "  %double_a = mul i32 %arg0, 2\n"
              "  %new_b = sub i32 %c_val, %double_a\n"
              "  %sum1 = add i32 %double_a, %new_b\n"
              "  %result = add i32 %sum1, %c_val\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(Mem2RegTest, MultipleAllocasWithConditionalUse) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto allocaX = builder->createAlloca(intType, nullptr, "x");
    auto allocaY = builder->createAlloca(intType, nullptr, "y");
    auto allocaZ = builder->createAlloca(intType, nullptr, "z");

    // Initialize all variables
    builder->createStore(builder->getInt32(10), allocaX);
    builder->createStore(builder->getInt32(20), allocaY);
    builder->createStore(builder->getInt32(30), allocaZ);

    auto condition =
        builder->createICmpSGT(func->getArg(0), builder->getInt32(0), "cond");
    builder->createCondBr(condition, trueBB, falseBB);

    // True branch: modify x and y
    builder->setInsertPoint(trueBB);
    auto loadX1 = builder->createLoad(allocaX, "x1");
    auto loadY1 = builder->createLoad(allocaY, "y1");
    auto newX = builder->createAdd(loadX1, builder->getInt32(100), "new_x");
    auto newY = builder->createMul(loadY1, builder->getInt32(2), "new_y");
    builder->createStore(newX, allocaX);
    builder->createStore(newY, allocaY);
    builder->createBr(mergeBB);

    // False branch: modify y and z
    builder->setInsertPoint(falseBB);
    auto loadY2 = builder->createLoad(allocaY, "y2");
    auto loadZ1 = builder->createLoad(allocaZ, "z1");
    auto newY2 = builder->createSub(loadY2, builder->getInt32(5), "new_y2");
    auto newZ = builder->createAdd(loadZ1, builder->getInt32(50), "new_z");
    builder->createStore(newY2, allocaY);
    builder->createStore(newZ, allocaZ);
    builder->createBr(mergeBB);

    // Merge: use all three variables
    builder->setInsertPoint(mergeBB);
    auto finalX = builder->createLoad(allocaX, "final_x");
    auto finalY = builder->createLoad(allocaY, "final_y");
    auto finalZ = builder->createLoad(allocaZ, "final_z");
    auto sum1 = builder->createAdd(finalX, finalY, "sum1");
    auto result = builder->createAdd(sum1, finalZ, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %x = alloca i32\n"
              "  %y = alloca i32\n"
              "  %z = alloca i32\n"
              "  store i32 10, i32* %x\n"
              "  store i32 20, i32* %y\n"
              "  store i32 30, i32* %z\n"
              "  %cond = icmp sgt i32 %arg0, 0\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  %x1 = load i32, i32* %x\n"
              "  %y1 = load i32, i32* %y\n"
              "  %new_x = add i32 %x1, 100\n"
              "  %new_y = mul i32 %y1, 2\n"
              "  store i32 %new_x, i32* %x\n"
              "  store i32 %new_y, i32* %y\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  %y2 = load i32, i32* %y\n"
              "  %z1 = load i32, i32* %z\n"
              "  %new_y2 = sub i32 %y2, 5\n"
              "  %new_z = add i32 %z1, 50\n"
              "  store i32 %new_y2, i32* %y\n"
              "  store i32 %new_z, i32* %z\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %final_x = load i32, i32* %x\n"
              "  %final_y = load i32, i32* %y\n"
              "  %final_z = load i32, i32* %z\n"
              "  %sum1 = add i32 %final_x, %final_y\n"
              "  %result = add i32 %sum1, %final_z\n"
              "  ret i32 %result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    auto ir_code = IRPrinter().print(func);
    EXPECT_TRUE(
        ir_code ==
            "define i32 @test_func(i32 %arg0) {\n"
            "entry:\n"
            "  %cond = icmp sgt i32 %arg0, 0\n"
            "  br i1 %cond, label %if.true, label %if.false\n"
            "if.true:\n"
            "  %new_x = add i32 10, 100\n"
            "  %new_y = mul i32 20, 2\n"
            "  br label %if.merge\n"
            "if.false:\n"
            "  %new_y2 = sub i32 20, 5\n"
            "  %new_z = add i32 30, 50\n"
            "  br label %if.merge\n"
            "if.merge:\n"
            "  %z.phi.1 = phi i32 [ 30, %if.true ], [ %new_z, %if.false ]\n"
            "  %y.phi.2 = phi i32 [ %new_y, %if.true ], [ %new_y2, %if.false "
            "]\n"
            "  %x.phi.3 = phi i32 [ %new_x, %if.true ], [ 10, %if.false ]\n"
            "  %sum1 = add i32 %x.phi.3, %y.phi.2\n"
            "  %result = add i32 %sum1, %z.phi.1\n"
            "  ret i32 %result\n"
            "}\n" ||
        ir_code ==
            "define i32 @test_func(i32 %arg0) {\n"
            "entry:\n"
            "  %cond = icmp sgt i32 %arg0, 0\n"
            "  br i1 %cond, label %if.true, label %if.false\n"
            "if.true:\n"
            "  %new_x = add i32 10, 100\n"
            "  %new_y = mul i32 20, 2\n"
            "  br label %if.merge\n"
            "if.false:\n"
            "  %new_y2 = sub i32 20, 5\n"
            "  %new_z = add i32 30, 50\n"
            "  br label %if.merge\n"
            "if.merge:\n"
            "  %y.phi.1 = phi i32 [ %new_y, %if.true ], [ %new_y2, %if.false "
            "]\n"
            "  %z.phi.2 = phi i32 [ 30, %if.true ], [ %new_z, %if.false ]\n"
            "  %x.phi.3 = phi i32 [ %new_x, %if.true ], [ 10, %if.false ]\n"
            "  %sum1 = add i32 %x.phi.3, %y.phi.1\n"
            "  %result = add i32 %sum1, %z.phi.2\n"
            "  ret i32 %result\n"
            "}\n");
}

// Nested loops and branches tests
TEST_F(Mem2RegTest, NestedLoopsWithAlloca) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func =
        Function::Create(funcType, "test_func", {"arg0", "arg1"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerLoopBB = BasicBlock::Create(ctx.get(), "outer.loop", func);
    auto innerLoopBB = BasicBlock::Create(ctx.get(), "inner.loop", func);
    auto innerBodyBB = BasicBlock::Create(ctx.get(), "inner.body", func);
    auto outerBodyBB = BasicBlock::Create(ctx.get(), "outer.body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto allocaSum = builder->createAlloca(intType, nullptr, "sum");
    auto allocaI = builder->createAlloca(intType, nullptr, "i");
    auto allocaJ = builder->createAlloca(intType, nullptr, "j");

    // Initialize sum = 0, i = 0
    builder->createStore(builder->getInt32(0), allocaSum);
    builder->createStore(builder->getInt32(0), allocaI);
    builder->createBr(outerLoopBB);

    // Outer loop: i < arg0
    builder->setInsertPoint(outerLoopBB);
    auto loadI = builder->createLoad(allocaI, "i_val");
    auto outerCond =
        builder->createICmpSLT(loadI, func->getArg(0), "outer_cond");
    builder->createCondBr(outerCond, innerLoopBB, exitBB);

    // Inner loop header: j = 0
    builder->setInsertPoint(innerLoopBB);
    builder->createStore(builder->getInt32(0), allocaJ);
    builder->createBr(innerBodyBB);

    // Inner loop: j < arg1
    builder->setInsertPoint(innerBodyBB);
    auto loadJ = builder->createLoad(allocaJ, "j_val");
    auto innerCond =
        builder->createICmpSLT(loadJ, func->getArg(1), "inner_cond");
    builder->createCondBr(innerCond, outerBodyBB, exitBB);

    // Inner loop body: sum += i * j; j++
    builder->setInsertPoint(outerBodyBB);
    auto currentSum = builder->createLoad(allocaSum, "sum_val");
    auto currentI = builder->createLoad(allocaI, "i_val2");
    auto currentJ = builder->createLoad(allocaJ, "j_val2");
    auto product = builder->createMul(currentI, currentJ, "product");
    auto newSum = builder->createAdd(currentSum, product, "new_sum");
    builder->createStore(newSum, allocaSum);
    auto newJ = builder->createAdd(currentJ, builder->getInt32(1), "new_j");
    builder->createStore(newJ, allocaJ);
    builder->createBr(innerBodyBB);

    // Exit block
    builder->setInsertPoint(exitBB);
    // Check if we came from outer loop (i++)
    auto loadI2 = builder->createLoad(allocaI, "i_val3");
    auto newI = builder->createAdd(loadI2, builder->getInt32(1), "new_i");
    builder->createStore(newI, allocaI);
    auto shouldContinue =
        builder->createICmpSLT(newI, func->getArg(0), "should_continue");
    builder->createCondBr(shouldContinue, outerLoopBB, exitBB);

    // Final return
    auto finalSum = builder->createLoad(allocaSum, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0, i32 %arg1) {\n"
              "entry:\n"
              "  %sum = alloca i32\n"
              "  %i = alloca i32\n"
              "  %j = alloca i32\n"
              "  store i32 0, i32* %sum\n"
              "  store i32 0, i32* %i\n"
              "  br label %outer.loop\n"
              "outer.loop:\n"
              "  %i_val = load i32, i32* %i\n"
              "  %outer_cond = icmp slt i32 %i_val, %arg0\n"
              "  br i1 %outer_cond, label %inner.loop, label %exit\n"
              "inner.loop:\n"
              "  store i32 0, i32* %j\n"
              "  br label %inner.body\n"
              "inner.body:\n"
              "  %j_val = load i32, i32* %j\n"
              "  %inner_cond = icmp slt i32 %j_val, %arg1\n"
              "  br i1 %inner_cond, label %outer.body, label %exit\n"
              "outer.body:\n"
              "  %sum_val = load i32, i32* %sum\n"
              "  %i_val2 = load i32, i32* %i\n"
              "  %j_val2 = load i32, i32* %j\n"
              "  %product = mul i32 %i_val2, %j_val2\n"
              "  %new_sum = add i32 %sum_val, %product\n"
              "  store i32 %new_sum, i32* %sum\n"
              "  %new_j = add i32 %j_val2, 1\n"
              "  store i32 %new_j, i32* %j\n"
              "  br label %inner.body\n"
              "exit:\n"
              "  %i_val3 = load i32, i32* %i\n"
              "  %new_i = add i32 %i_val3, 1\n"
              "  store i32 %new_i, i32* %i\n"
              "  %should_continue = icmp slt i32 %new_i, %arg0\n"
              "  br i1 %should_continue, label %outer.loop, label %exit\n"
              "  %final_sum = load i32, i32* %sum\n"
              "  ret i32 %final_sum\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Note: This is a complex nested loop that creates multiple phi nodes
    // The exact output depends on the SSA construction algorithm
    EXPECT_NE(IRPrinter().print(func).find("phi"), std::string::npos);
}

TEST_F(Mem2RegTest, LoopWithConditionalBreak) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto bodyBB = BasicBlock::Create(ctx.get(), "body", func);
    auto continueBB = BasicBlock::Create(ctx.get(), "continue", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto allocaI = builder->createAlloca(intType, nullptr, "i");
    auto allocaSum = builder->createAlloca(intType, nullptr, "sum");

    builder->createStore(builder->getInt32(0), allocaI);
    builder->createStore(builder->getInt32(0), allocaSum);
    builder->createBr(loopBB);

    // Loop condition: i < 100
    builder->setInsertPoint(loopBB);
    auto loadI = builder->createLoad(allocaI, "i_val");
    auto loopCond =
        builder->createICmpSLT(loadI, builder->getInt32(100), "loop_cond");
    builder->createCondBr(loopCond, bodyBB, exitBB);

    // Loop body with conditional break
    builder->setInsertPoint(bodyBB);
    auto currentI = builder->createLoad(allocaI, "current_i");
    auto currentSum = builder->createLoad(allocaSum, "current_sum");
    auto newSum = builder->createAdd(currentSum, currentI, "new_sum");
    builder->createStore(newSum, allocaSum);

    // Break if sum > arg0
    auto breakCond =
        builder->createICmpSGT(newSum, func->getArg(0), "break_cond");
    builder->createCondBr(breakCond, exitBB, continueBB);

    // Continue: increment i
    builder->setInsertPoint(continueBB);
    auto currentI2 = builder->createLoad(allocaI, "current_i2");
    auto newI = builder->createAdd(currentI2, builder->getInt32(1), "new_i");
    builder->createStore(newI, allocaI);
    builder->createBr(loopBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalSum = builder->createLoad(allocaSum, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %i = alloca i32\n"
              "  %sum = alloca i32\n"
              "  store i32 0, i32* %i\n"
              "  store i32 0, i32* %sum\n"
              "  br label %loop\n"
              "loop:\n"
              "  %i_val = load i32, i32* %i\n"
              "  %loop_cond = icmp slt i32 %i_val, 100\n"
              "  br i1 %loop_cond, label %body, label %exit\n"
              "body:\n"
              "  %current_i = load i32, i32* %i\n"
              "  %current_sum = load i32, i32* %sum\n"
              "  %new_sum = add i32 %current_sum, %current_i\n"
              "  store i32 %new_sum, i32* %sum\n"
              "  %break_cond = icmp sgt i32 %new_sum, %arg0\n"
              "  br i1 %break_cond, label %exit, label %continue\n"
              "continue:\n"
              "  %current_i2 = load i32, i32* %i\n"
              "  %new_i = add i32 %current_i2, 1\n"
              "  store i32 %new_i, i32* %i\n"
              "  br label %loop\n"
              "exit:\n"
              "  %final_sum = load i32, i32* %sum\n"
              "  ret i32 %final_sum\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    auto ir_code = IRPrinter().print(func);

    EXPECT_TRUE(
        ir_code ==
            "define i32 @test_func(i32 %arg0) {\n"
            "entry:\n"
            "  br label %loop\n"
            "loop:\n"
            "  %sum.phi.1 = phi i32 [ 0, %entry ], [ %new_sum, %continue ]\n"
            "  %i.phi.3 = phi i32 [ 0, %entry ], [ %new_i, %continue ]\n"
            "  %loop_cond = icmp slt i32 %i.phi.3, 100\n"
            "  br i1 %loop_cond, label %body, label %exit\n"
            "body:\n"
            "  %new_sum = add i32 %sum.phi.1, %i.phi.3\n"
            "  %break_cond = icmp sgt i32 %new_sum, %arg0\n"
            "  br i1 %break_cond, label %exit, label %continue\n"
            "continue:\n"
            "  %new_i = add i32 %i.phi.3, 1\n"
            "  br label %loop\n"
            "exit:\n"
            "  %sum.phi.2 = phi i32 [ %new_sum, %body ], [ %sum.phi.1, %loop "
            "]\n"
            "  ret i32 %sum.phi.2\n"
            "}\n" ||
        ir_code ==
            "define i32 @test_func(i32 %arg0) {\n"
            "entry:\n"
            "  br label %loop\n"
            "loop:\n"
            "  %sum.phi.2 = phi i32 [ 0, %entry ], [ %new_sum, %continue ]\n"
            "  %i.phi.3 = phi i32 [ 0, %entry ], [ %new_i, %continue ]\n"
            "  %loop_cond = icmp slt i32 %i.phi.3, 100\n"
            "  br i1 %loop_cond, label %body, label %exit\n"
            "body:\n"
            "  %new_sum = add i32 %sum.phi.2, %i.phi.3\n"
            "  %break_cond = icmp sgt i32 %new_sum, %arg0\n"
            "  br i1 %break_cond, label %exit, label %continue\n"
            "continue:\n"
            "  %new_i = add i32 %i.phi.3, 1\n"
            "  br label %loop\n"
            "exit:\n"
            "  %sum.phi.1 = phi i32 [ %new_sum, %body ], [ %sum.phi.2, %loop "
            "]\n"
            "  ret i32 %sum.phi.1\n"
            "}\n");
}

// Advanced edge cases and stress tests
TEST_F(Mem2RegTest, AllocaWithComplexDataFlow) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto bb1 = BasicBlock::Create(ctx.get(), "bb1", func);
    auto bb2 = BasicBlock::Create(ctx.get(), "bb2", func);
    auto bb3 = BasicBlock::Create(ctx.get(), "bb3", func);
    auto bb4 = BasicBlock::Create(ctx.get(), "bb4", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto alloca = builder->createAlloca(intType, nullptr, "var");
    builder->createStore(builder->getInt32(1), alloca);

    auto cond1 =
        builder->createICmpSGT(func->getArg(0), builder->getInt32(0), "cond1");
    builder->createCondBr(cond1, bb1, bb2);

    // BB1: var = var * 2
    builder->setInsertPoint(bb1);
    auto load1 = builder->createLoad(alloca, "load1");
    auto mul1 = builder->createMul(load1, builder->getInt32(2), "mul1");
    builder->createStore(mul1, alloca);
    builder->createBr(bb3);

    // BB2: var = var + 10
    builder->setInsertPoint(bb2);
    auto load2 = builder->createLoad(alloca, "load2");
    auto add1 = builder->createAdd(load2, builder->getInt32(10), "add1");
    builder->createStore(add1, alloca);
    builder->createBr(bb3);

    // BB3: converge and branch again
    builder->setInsertPoint(bb3);
    auto load3 = builder->createLoad(alloca, "load3");
    auto cond2 = builder->createICmpSLT(load3, builder->getInt32(20), "cond2");
    builder->createCondBr(cond2, bb4, exitBB);

    // BB4: var = var - 5
    builder->setInsertPoint(bb4);
    auto load4 = builder->createLoad(alloca, "load4");
    auto sub1 = builder->createSub(load4, builder->getInt32(5), "sub1");
    builder->createStore(sub1, alloca);
    builder->createBr(exitBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalLoad = builder->createLoad(alloca, "final_load");
    builder->createRet(finalLoad);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %var = alloca i32\n"
              "  store i32 1, i32* %var\n"
              "  %cond1 = icmp sgt i32 %arg0, 0\n"
              "  br i1 %cond1, label %bb1, label %bb2\n"
              "bb1:\n"
              "  %load1 = load i32, i32* %var\n"
              "  %mul1 = mul i32 %load1, 2\n"
              "  store i32 %mul1, i32* %var\n"
              "  br label %bb3\n"
              "bb2:\n"
              "  %load2 = load i32, i32* %var\n"
              "  %add1 = add i32 %load2, 10\n"
              "  store i32 %add1, i32* %var\n"
              "  br label %bb3\n"
              "bb3:\n"
              "  %load3 = load i32, i32* %var\n"
              "  %cond2 = icmp slt i32 %load3, 20\n"
              "  br i1 %cond2, label %bb4, label %exit\n"
              "bb4:\n"
              "  %load4 = load i32, i32* %var\n"
              "  %sub1 = sub i32 %load4, 5\n"
              "  store i32 %sub1, i32* %var\n"
              "  br label %exit\n"
              "exit:\n"
              "  %final_load = load i32, i32* %var\n"
              "  ret i32 %final_load\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %cond1 = icmp sgt i32 %arg0, 0\n"
              "  br i1 %cond1, label %bb1, label %bb2\n"
              "bb1:\n"
              "  %mul1 = mul i32 1, 2\n"
              "  br label %bb3\n"
              "bb2:\n"
              "  %add1 = add i32 1, 10\n"
              "  br label %bb3\n"
              "bb3:\n"
              "  %var.phi.1 = phi i32 [ %mul1, %bb1 ], [ %add1, %bb2 ]\n"
              "  %cond2 = icmp slt i32 %var.phi.1, 20\n"
              "  br i1 %cond2, label %bb4, label %exit\n"
              "bb4:\n"
              "  %sub1 = sub i32 %var.phi.1, 5\n"
              "  br label %exit\n"
              "exit:\n"
              "  %var.phi.2 = phi i32 [ %sub1, %bb4 ], [ %var.phi.1, %bb3 ]\n"
              "  ret i32 %var.phi.2\n"
              "}\n");
}

TEST_F(Mem2RegTest, AllocaWithMixedTypesAndCasts) {
    auto intType = ctx->getIntegerType(32);
    auto floatType = ctx->getFloatType();
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {floatType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto intAlloca = builder->createAlloca(intType, nullptr, "int_val");
    auto floatAlloca = builder->createAlloca(floatType, nullptr, "float_val");
    auto boolAlloca = builder->createAlloca(boolType, nullptr, "bool_val");

    // Initialize
    builder->createStore(builder->getInt32(42), intAlloca);
    builder->createStore(func->getArg(0), floatAlloca);
    builder->createStore(builder->getInt1(false), boolAlloca);

    // Branch based on float value
    auto floatLoad = builder->createLoad(floatAlloca, "float_load");
    auto floatCond = builder->createFCmpOGT(floatLoad, builder->getFloat(0.0f),
                                            "float_cond");
    builder->createCondBr(floatCond, trueBB, falseBB);

    // True branch: convert float to int and store
    builder->setInsertPoint(trueBB);
    auto floatLoad2 = builder->createLoad(floatAlloca, "float_load2");
    auto floatToInt =
        builder->createFPToSI(floatLoad2, intType, "float_to_int");
    builder->createStore(floatToInt, intAlloca);
    builder->createStore(builder->getInt1(true), boolAlloca);
    builder->createBr(mergeBB);

    // False branch: negate int
    builder->setInsertPoint(falseBB);
    auto intLoad = builder->createLoad(intAlloca, "int_load");
    auto negInt = builder->createSub(builder->getInt32(0), intLoad, "neg_int");
    builder->createStore(negInt, intAlloca);
    builder->createBr(mergeBB);

    // Merge and return
    builder->setInsertPoint(mergeBB);
    auto finalInt = builder->createLoad(intAlloca, "final_int");
    auto finalBool = builder->createLoad(boolAlloca, "final_bool");
    auto boolToInt =
        builder->createCast(CastInst::ZExt, finalBool, intType, "bool_to_int");
    auto result = builder->createAdd(finalInt, boolToInt, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(float %arg0) {\n"
              "entry:\n"
              "  %int_val = alloca i32\n"
              "  %float_val = alloca float\n"
              "  %bool_val = alloca i1\n"
              "  store i32 42, i32* %int_val\n"
              "  store float %arg0, float* %float_val\n"
              "  store i1 0, i1* %bool_val\n"
              "  %float_load = load float, float* %float_val\n"
              "  %float_cond = fcmp ogt float %float_load, 0.000000e+00\n"
              "  br i1 %float_cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  %float_load2 = load float, float* %float_val\n"
              "  %float_to_int = fptosi float %float_load2 to i32\n"
              "  store i32 %float_to_int, i32* %int_val\n"
              "  store i1 1, i1* %bool_val\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  %int_load = load i32, i32* %int_val\n"
              "  %neg_int = sub i32 0, %int_load\n"
              "  store i32 %neg_int, i32* %int_val\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %final_int = load i32, i32* %int_val\n"
              "  %final_bool = load i1, i1* %bool_val\n"
              "  %bool_to_int = zext i1 %final_bool to i32\n"
              "  %result = add i32 %final_int, %bool_to_int\n"
              "  ret i32 %result\n"
              "}\n");

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        "define i32 @test_func(float %arg0) {\n"
        "entry:\n"
        "  %float_cond = fcmp ogt float %arg0, 0.000000e+00\n"
        "  br i1 %float_cond, label %if.true, label %if.false\n"
        "if.true:\n"
        "  %float_to_int = fptosi float %arg0 to i32\n"
        "  br label %if.merge\n"
        "if.false:\n"
        "  %neg_int = sub i32 0, 42\n"
        "  br label %if.merge\n"
        "if.merge:\n"
        "  %bool_val.phi.1 = phi i1 [ 1, %if.true ], [ 0, %if.false ]\n"
        "  %int_val.phi.2 = phi i32 [ %float_to_int, %if.true ], [ %neg_int, "
        "%if.false ]\n"
        "  %bool_to_int = zext i1 %bool_val.phi.1 to i32\n"
        "  %result = add i32 %int_val.phi.2, %bool_to_int\n"
        "  ret i32 %result\n"
        "}\n");
}

TEST_F(Mem2RegTest, AllocaWithVeryLongChain) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto alloca = builder->createAlloca(intType, nullptr, "chain_var");
    builder->createStore(func->getArg(0), alloca);

    // Create a long chain of load-modify-store operations
    for (int i = 0; i < 10; i++) {
        auto load = builder->createLoad(alloca, "load" + std::to_string(i));
        auto modified = builder->createAdd(load, builder->getInt32(i + 1),
                                           "add" + std::to_string(i));
        builder->createStore(modified, alloca);
    }

    // Final load and return
    auto finalLoad = builder->createLoad(alloca, "final_load");
    builder->createRet(finalLoad);

    // Run Mem2RegPass
    Mem2RegPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // The result should be a chain of additions: arg0 + 1 + 2 + ... + 10
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("add"), std::string::npos);
    EXPECT_EQ(result.find("alloca"), std::string::npos);
    EXPECT_EQ(result.find("load"), std::string::npos);
    EXPECT_EQ(result.find("store"), std::string::npos);
}
