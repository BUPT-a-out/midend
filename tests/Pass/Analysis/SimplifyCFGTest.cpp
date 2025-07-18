#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "Pass/Pass.h"
#include "Pass/Transform/SimplifyCFGPass.h"

using namespace midend;

class SimplifyCFGTest : public ::testing::Test {
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

TEST_F(SimplifyCFGTest, RemoveUnreachableBlock_SimpleCase) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto reachableBB = BasicBlock::Create(ctx.get(), "reachable", func);
    auto unreachableBB = BasicBlock::Create(ctx.get(), "unreachable", func);

    builder->setInsertPoint(entryBB);
    builder->createBr(reachableBB);

    builder->setInsertPoint(reachableBB);
    builder->createRet(builder->getInt32(42));

    builder->setInsertPoint(unreachableBB);
    builder->createRet(builder->getInt32(100));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  br label %reachable\n"
              "reachable:\n"
              "  ret i32 42\n"
              "unreachable:\n"
              "  ret i32 100\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 42\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, RemoveUnreachableBlock_MultipleUnreachable) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
    auto unreachable1BB = BasicBlock::Create(ctx.get(), "unreachable1", func);
    auto unreachable2BB = BasicBlock::Create(ctx.get(), "unreachable2", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto cond = func->getArg(0);
    cond->setName("cond");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(exitBB);

    builder->setInsertPoint(falseBB);
    builder->createBr(exitBB);

    builder->setInsertPoint(unreachable1BB);
    builder->createBr(unreachable2BB);

    builder->setInsertPoint(unreachable2BB);
    builder->createRet(builder->getInt32(999));

    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(42));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %false_bb\n"
              "true_bb:\n"
              "  br label %exit\n"
              "false_bb:\n"
              "  br label %exit\n"
              "unreachable1:\n"
              "  br label %unreachable2\n"
              "unreachable2:\n"
              "  ret i32 999\n"
              "exit:\n"
              "  ret i32 42\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %exit, label %exit\n"
              "exit:\n"
              "  ret i32 42\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, RemoveUnreachableBlock_NoUnreachable) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);

    auto cond = func->getArg(0);
    cond->setName("cond");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createRet(builder->getInt32(1));

    builder->setInsertPoint(falseBB);
    builder->createRet(builder->getInt32(0));

    std::string beforeIR = IRPrinter().print(func);

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(SimplifyCFGTest, MergeBlocks_SimpleMerge) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto middleBB = BasicBlock::Create(ctx.get(), "middle", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto param = func->getArg(0);
    param->setName("x");

    builder->setInsertPoint(entryBB);
    auto val1 = builder->createAdd(param, builder->getInt32(10), "val1");
    builder->createBr(middleBB);

    builder->setInsertPoint(middleBB);
    auto val2 = builder->createMul(val1, builder->getInt32(2), "val2");
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    auto val3 = builder->createSub(val2, param, "val3");
    builder->createRet(val3);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  br label %middle\n"
              "middle:\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  br label %exit\n"
              "exit:\n"
              "  %val3 = sub i32 %val2, %x\n"
              "  ret i32 %val3\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  %val3 = sub i32 %val2, %x\n"
              "  ret i32 %val3\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, MergeBlocks_CannotMergeMultiplePredecessors) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    cond->setName("cond");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(falseBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    builder->createRet(builder->getInt32(42));

    std::string beforeIR = IRPrinter().print(func);

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // The pass will optimize empty blocks by redirecting predecessors
    EXPECT_TRUE(changed);
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %merge, label %merge\n"
              "merge:\n"
              "  ret i32 42\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, MergeBlocks_CannotMergeWithPHI) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto middleBB = BasicBlock::Create(ctx.get(), "middle", func);

    auto param = func->getArg(0);
    param->setName("x");

    builder->setInsertPoint(entryBB);
    auto val1 = builder->createAdd(param, builder->getInt32(10), "val1");
    builder->createBr(middleBB);

    builder->setInsertPoint(middleBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(val1, entryBB);
    builder->createRet(phi);

    std::string beforeIR = IRPrinter().print(func);

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(SimplifyCFGTest, EliminateEmptyBlocks_SimpleCase) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto emptyBB = BasicBlock::Create(ctx.get(), "empty", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto cond = func->getArg(0);
    cond->setName("cond");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, emptyBB, exitBB);

    builder->setInsertPoint(emptyBB);
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(42));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %empty, label %exit\n"
              "empty:\n"
              "  br label %exit\n"
              "exit:\n"
              "  ret i32 42\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %exit, label %exit\n"
              "exit:\n"
              "  ret i32 42\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, EliminateEmptyBlocks_WithPHI) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto emptyBB = BasicBlock::Create(ctx.get(), "empty", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto cond = func->getArg(0);
    cond->setName("cond");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, emptyBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(exitBB);

    builder->setInsertPoint(emptyBB);
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    auto phi = builder->createPHI(intType, "result");
    phi->addIncoming(builder->getInt32(1), trueBB);
    phi->addIncoming(builder->getInt32(0), emptyBB);
    builder->createRet(phi);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %empty\n"
              "true_bb:\n"
              "  br label %exit\n"
              "empty:\n"
              "  br label %exit\n"
              "exit:\n"
              "  %result = phi i32 [ 1, %true_bb ], [ 0, %empty ]\n"
              "  ret i32 %result\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After optimization:
    // - Both empty blocks are eliminated (empty and true_bb)
    // - The conditional branch is simplified since both targets go to exit
    // - The phi node is updated to have entry as the predecessor for both
    // values
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %exit, label %exit\n"
              "exit:\n"
              "  %result = phi i32 [ 1, %entry ], [ 0, %entry ]\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, EliminateEmptyBlocks_ChainedEmpty) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto empty1BB = BasicBlock::Create(ctx.get(), "empty1", func);
    auto empty2BB = BasicBlock::Create(ctx.get(), "empty2", func);
    auto empty3BB = BasicBlock::Create(ctx.get(), "empty3", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    builder->createBr(empty1BB);

    builder->setInsertPoint(empty1BB);
    builder->createBr(empty2BB);

    builder->setInsertPoint(empty2BB);
    builder->createBr(empty3BB);

    builder->setInsertPoint(empty3BB);
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(42));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  br label %empty1\n"
              "empty1:\n"
              "  br label %empty2\n"
              "empty2:\n"
              "  br label %empty3\n"
              "empty3:\n"
              "  br label %exit\n"
              "exit:\n"
              "  ret i32 42\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 42\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, RemoveDuplicatePHI_SimpleCase) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto val1 = func->getArg(1);
    auto val2 = func->getArg(2);
    cond->setName("cond");
    val1->setName("val1");
    val2->setName("val2");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(falseBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    auto phi1 = builder->createPHI(intType, "phi1");
    phi1->addIncoming(val1, trueBB);
    phi1->addIncoming(val2, falseBB);

    auto phi2 = builder->createPHI(intType, "phi2");
    phi2->addIncoming(val1, trueBB);
    phi2->addIncoming(val2, falseBB);

    auto result = builder->createAdd(phi1, phi2, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %false_bb\n"
              "true_bb:\n"
              "  br label %merge\n"
              "false_bb:\n"
              "  br label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val1, %true_bb ], [ %val2, %false_bb ]\n"
              "  %phi2 = phi i32 [ %val1, %true_bb ], [ %val2, %false_bb ]\n"
              "  %result = add i32 %phi1, %phi2\n"
              "  ret i32 %result\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
              "entry:\n"
              "  br i1 %cond, label %merge, label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val1, %entry ], [ %val2, %entry ]\n"
              "  %result = add i32 %phi1, %phi1\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, RemoveDuplicatePHI_MultipleDuplicates) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto val1 = func->getArg(1);
    auto val2 = func->getArg(2);
    cond->setName("cond");
    val1->setName("val1");
    val2->setName("val2");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    auto trueVal = builder->createAdd(val1, builder->getInt32(1), "true_val");
    builder->createBr(mergeBB);

    builder->setInsertPoint(falseBB);
    auto falseVal = builder->createSub(val2, builder->getInt32(1), "false_val");
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    auto phi1 = builder->createPHI(intType, "phi1");
    phi1->addIncoming(val1, trueBB);
    phi1->addIncoming(val2, falseBB);

    auto phi2 = builder->createPHI(intType, "phi2");
    phi2->addIncoming(val1, trueBB);
    phi2->addIncoming(val2, falseBB);

    auto phi3 = builder->createPHI(intType, "phi3");
    phi3->addIncoming(trueVal, trueBB);
    phi3->addIncoming(falseVal, falseBB);

    auto phi4 = builder->createPHI(intType, "phi4");
    phi4->addIncoming(val1, trueBB);
    phi4->addIncoming(val2, falseBB);

    auto sum1 = builder->createAdd(phi1, phi2, "sum1");
    auto sum2 = builder->createAdd(phi3, phi4, "sum2");
    auto result = builder->createAdd(sum1, sum2, "result");
    builder->createRet(result);

    EXPECT_EQ(
        IRPrinter().print(func),
        "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
        "entry:\n"
        "  br i1 %cond, label %true_bb, label %false_bb\n"
        "true_bb:\n"
        "  %true_val = add i32 %val1, 1\n"
        "  br label %merge\n"
        "false_bb:\n"
        "  %false_val = sub i32 %val2, 1\n"
        "  br label %merge\n"
        "merge:\n"
        "  %phi1 = phi i32 [ %val1, %true_bb ], [ %val2, %false_bb ]\n"
        "  %phi2 = phi i32 [ %val1, %true_bb ], [ %val2, %false_bb ]\n"
        "  %phi3 = phi i32 [ %true_val, %true_bb ], [ %false_val, %false_bb ]\n"
        "  %phi4 = phi i32 [ %val1, %true_bb ], [ %val2, %false_bb ]\n"
        "  %sum1 = add i32 %phi1, %phi2\n"
        "  %sum2 = add i32 %phi3, %phi4\n"
        "  %result = add i32 %sum1, %sum2\n"
        "  ret i32 %result\n"
        "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(
        IRPrinter().print(func),
        "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
        "entry:\n"
        "  br i1 %cond, label %true_bb, label %false_bb\n"
        "true_bb:\n"
        "  %true_val = add i32 %val1, 1\n"
        "  br label %merge\n"
        "false_bb:\n"
        "  %false_val = sub i32 %val2, 1\n"
        "  br label %merge\n"
        "merge:\n"
        "  %phi1 = phi i32 [ %val1, %true_bb ], [ %val2, %false_bb ]\n"
        "  %phi3 = phi i32 [ %true_val, %true_bb ], [ %false_val, %false_bb ]\n"
        "  %sum1 = add i32 %phi1, %phi1\n"
        "  %sum2 = add i32 %phi3, %phi1\n"
        "  %result = add i32 %sum1, %sum2\n"
        "  ret i32 %result\n"
        "}\n");
}

TEST_F(SimplifyCFGTest, RemoveDuplicatePHI_NoDuplicates) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto val1 = func->getArg(1);
    auto val2 = func->getArg(2);
    cond->setName("cond");
    val1->setName("val1");
    val2->setName("val2");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(falseBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    auto phi1 = builder->createPHI(intType, "phi1");
    phi1->addIncoming(val1, trueBB);
    phi1->addIncoming(val2, falseBB);

    auto phi2 = builder->createPHI(intType, "phi2");
    phi2->addIncoming(val2, trueBB);
    phi2->addIncoming(val1, falseBB);

    auto result = builder->createAdd(phi1, phi2, "result");
    builder->createRet(result);

    std::string beforeIR = IRPrinter().print(func);

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
              "entry:\n"
              "  br i1 %cond, label %merge, label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val1, %entry ], [ %val2, %entry ]\n"
              "  %phi2 = phi i32 [ %val2, %entry ], [ %val1, %entry ]\n"
              "  %result = add i32 %phi1, %phi2\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, CombinedOptimizations_UnreachableAndMerge) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto middleBB = BasicBlock::Create(ctx.get(), "middle", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);
    auto unreachableBB = BasicBlock::Create(ctx.get(), "unreachable", func);

    auto param = func->getArg(0);
    param->setName("x");

    builder->setInsertPoint(entryBB);
    auto val1 = builder->createAdd(param, builder->getInt32(10), "val1");
    builder->createBr(middleBB);

    builder->setInsertPoint(middleBB);
    auto val2 = builder->createMul(val1, builder->getInt32(2), "val2");
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    builder->createRet(val2);

    builder->setInsertPoint(unreachableBB);
    builder->createRet(builder->getInt32(999));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  br label %middle\n"
              "middle:\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  br label %exit\n"
              "exit:\n"
              "  ret i32 %val2\n"
              "unreachable:\n"
              "  ret i32 999\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  ret i32 %val2\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, CombinedOptimizations_EmptyAndDuplicatePHI) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto emptyBB = BasicBlock::Create(ctx.get(), "empty", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto val1 = func->getArg(1);
    auto val2 = func->getArg(2);
    cond->setName("cond");
    val1->setName("val1");
    val2->setName("val2");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, emptyBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(emptyBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    auto phi1 = builder->createPHI(intType, "phi1");
    phi1->addIncoming(val1, trueBB);
    phi1->addIncoming(val2, emptyBB);

    auto phi2 = builder->createPHI(intType, "phi2");
    phi2->addIncoming(val1, trueBB);
    phi2->addIncoming(val2, emptyBB);

    auto result = builder->createAdd(phi1, phi2, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %empty\n"
              "true_bb:\n"
              "  br label %merge\n"
              "empty:\n"
              "  br label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val1, %true_bb ], [ %val2, %empty ]\n"
              "  %phi2 = phi i32 [ %val1, %true_bb ], [ %val2, %empty ]\n"
              "  %result = add i32 %phi1, %phi2\n"
              "  ret i32 %result\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %empty\n"
              "true_bb:\n"
              "  br label %merge\n"
              "empty:\n"
              "  br label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val1, %true_bb ], [ %val2, %empty ]\n"
              "  %result = add i32 %phi1, %phi1\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, CombinedOptimizations_Empty) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType, intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto emptyBB = BasicBlock::Create(ctx.get(), "empty", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto val1 = func->getArg(1);
    auto val2 = func->getArg(2);
    cond->setName("cond");
    val1->setName("val1");
    val2->setName("val2");

    builder->setInsertPoint(entryBB);
    builder->createCondBr(cond, trueBB, emptyBB);

    builder->setInsertPoint(trueBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(emptyBB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    builder->createRet(cond);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %empty\n"
              "true_bb:\n"
              "  br label %merge\n"
              "empty:\n"
              "  br label %merge\n"
              "merge:\n"
              "  ret i1 %cond\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    std::cout << IRPrinter().print(func) << std::endl;

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_func(i1 %cond, i32 %val1, i32 %val2) {
entry:
  ret i1 %cond
}
)");
}

TEST_F(SimplifyCFGTest, DISABLED_CombinedOptimizations_ComplexFlow) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType, boolType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto cond1BB = BasicBlock::Create(ctx.get(), "cond1", func);
    auto path1BB = BasicBlock::Create(ctx.get(), "path1", func);
    auto path2BB = BasicBlock::Create(ctx.get(), "path2", func);
    auto empty1BB = BasicBlock::Create(ctx.get(), "empty1", func);
    auto empty2BB = BasicBlock::Create(ctx.get(), "empty2", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);
    auto unreachableBB = BasicBlock::Create(ctx.get(), "unreachable", func);

    auto cond1 = func->getArg(0);
    auto cond2 = func->getArg(1);
    auto val = func->getArg(2);
    cond1->setName("cond1");
    cond2->setName("cond2");
    val->setName("val");

    builder->setInsertPoint(entryBB);
    builder->createBr(cond1BB);

    builder->setInsertPoint(cond1BB);
    builder->createCondBr(cond1, path1BB, path2BB);

    builder->setInsertPoint(path1BB);
    builder->createBr(empty1BB);

    builder->setInsertPoint(path2BB);
    builder->createCondBr(cond2, empty2BB, mergeBB);

    builder->setInsertPoint(empty1BB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(empty2BB);
    builder->createBr(mergeBB);

    builder->setInsertPoint(mergeBB);
    auto phi1 = builder->createPHI(intType, "phi1");
    phi1->addIncoming(val, empty1BB);
    phi1->addIncoming(val, empty2BB);
    phi1->addIncoming(val, path2BB);

    auto phi2 = builder->createPHI(intType, "phi2");
    phi2->addIncoming(val, empty1BB);
    phi2->addIncoming(val, empty2BB);
    phi2->addIncoming(val, path2BB);

    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    auto result = builder->createAdd(phi1, phi2, "result");
    builder->createRet(result);

    builder->setInsertPoint(unreachableBB);
    builder->createRet(builder->getInt32(999));

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond1, i1 %cond2, i32 %val) {\n"
              "entry:\n"
              "  br label %cond1\n"
              "cond1:\n"
              "  br i1 %cond1, label %path1, label %path2\n"
              "path1:\n"
              "  br label %empty1\n"
              "path2:\n"
              "  br i1 %cond2, label %empty2, label %merge\n"
              "empty1:\n"
              "  br label %merge\n"
              "empty2:\n"
              "  br label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val, %empty1 ], [ %val, %empty2 ], [ %val, "
              "%path2 ]\n"
              "  %phi2 = phi i32 [ %val, %empty1 ], [ %val, %empty2 ], [ %val, "
              "%path2 ]\n"
              "  br label %exit\n"
              "exit:\n"
              "  %result = add i32 %phi1, %phi2\n"
              "  ret i32 %result\n"
              "unreachable:\n"
              "  ret i32 999\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond1, i1 %cond2, i32 %val) {\n"
              "entry:\n"
              "  br i1 %cond1, label %path1, label %path2\n"
              "path1:\n"
              "  br label %merge\n"
              "path2:\n"
              "  br i1 %cond2, label %merge, label %merge\n"
              "merge:\n"
              "  %phi1 = phi i32 [ %val, %path1 ], [ %val, %path2 ], [ %val, "
              "%path2 ]\n"
              "  %result = add i32 %phi1, %phi1\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(SimplifyCFGTest, IterativeOptimization_CreateNewOpportunities) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto bb1 = BasicBlock::Create(ctx.get(), "bb1", func);
    auto bb2 = BasicBlock::Create(ctx.get(), "bb2", func);
    auto bb3 = BasicBlock::Create(ctx.get(), "bb3", func);
    auto bb4 = BasicBlock::Create(ctx.get(), "bb4", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto param = func->getArg(0);
    param->setName("x");

    builder->setInsertPoint(entryBB);
    builder->createBr(bb1);

    builder->setInsertPoint(bb1);
    builder->createBr(bb2);

    builder->setInsertPoint(bb2);
    auto val1 = builder->createAdd(param, builder->getInt32(10), "val1");
    builder->createBr(bb3);

    builder->setInsertPoint(bb3);
    builder->createBr(bb4);

    builder->setInsertPoint(bb4);
    auto val2 = builder->createMul(val1, builder->getInt32(2), "val2");
    builder->createBr(exitBB);

    builder->setInsertPoint(exitBB);
    builder->createRet(val2);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x) {\n"
              "entry:\n"
              "  br label %bb1\n"
              "bb1:\n"
              "  br label %bb2\n"
              "bb2:\n"
              "  %val1 = add i32 %x, 10\n"
              "  br label %bb3\n"
              "bb3:\n"
              "  br label %bb4\n"
              "bb4:\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  br label %exit\n"
              "exit:\n"
              "  ret i32 %val2\n"
              "}\n");

    SimplifyCFGPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  ret i32 %val2\n"
              "}\n");
}