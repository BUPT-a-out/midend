#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "Pass/Pass.h"
#include "Pass/Transform/ADCEPass.h"
#include "Pass/Transform/Mem2RegPass.h"

using namespace midend;

class ADCETest : public ::testing::Test {
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

TEST_F(ADCETest, SimpleDeadCodeRemoval) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create some dead code
    auto deadVal1 = builder->createAdd(builder->getInt32(10),
                                       builder->getInt32(20), "dead1");
    builder->createMul(deadVal1, builder->getInt32(2), "dead2");

    // Create live code
    auto liveVal =
        builder->createAdd(builder->getInt32(5), builder->getInt32(10), "live");
    builder->createRet(liveVal);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %dead1 = add i32 10, 20\n"
              "  %dead2 = mul i32 %dead1, 2\n"
              "  %live = add i32 5, 10\n"
              "  ret i32 %live\n"
              "}\n");

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Dead instructions should be removed
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %live = add i32 5, 10\n"
              "  ret i32 %live\n"
              "}\n");
}

TEST_F(ADCETest, DeadCodeWithComplexDependencies) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto param1 = func->getArg(0);
    auto param2 = func->getArg(1);
    param1->setName("a");
    param2->setName("b");

    // Create a chain of dead computations
    auto dead1 = builder->createAdd(param1, param2, "dead1");
    auto dead2 = builder->createSub(dead1, param1, "dead2");
    auto dead3 = builder->createMul(dead2, dead1, "dead3");
    builder->createAdd(dead3, dead2, "dead4");

    // Create live computation
    auto live1 = builder->createMul(param1, param2, "live1");
    builder->createRet(live1);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %a, i32 %b) {\n"
              "entry:\n"
              "  %dead1 = add i32 %a, %b\n"
              "  %dead2 = sub i32 %dead1, %a\n"
              "  %dead3 = mul i32 %dead2, %dead1\n"
              "  %dead4 = add i32 %dead3, %dead2\n"
              "  %live1 = mul i32 %a, %b\n"
              "  ret i32 %live1\n"
              "}\n");

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // All dead instructions should be removed
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %a, i32 %b) {\n"
              "entry:\n"
              "  %live1 = mul i32 %a, %b\n"
              "  ret i32 %live1\n"
              "}\n");
}

TEST_F(ADCETest, StoreInstructionsAlwaysLive) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType = FunctionType::get(ctx->getVoidType(), {ptrType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto ptr = func->getArg(0);
    ptr->setName("ptr");

    // Create some dead arithmetic
    auto dead1 = builder->createAdd(builder->getInt32(10),
                                    builder->getInt32(20), "dead1");

    // Store is always live even if its value is dead
    auto val =
        builder->createMul(builder->getInt32(5), builder->getInt32(6), "val");
    builder->createStore(val, ptr);

    // More dead code after store
    builder->createSub(dead1, builder->getInt32(5), "dead2");

    builder->createRetVoid();

    EXPECT_EQ(IRPrinter().print(func),
              "define void @test_func(i32* %ptr) {\n"
              "entry:\n"
              "  %dead1 = add i32 10, 20\n"
              "  %val = mul i32 5, 6\n"
              "  store i32 %val, i32* %ptr\n"
              "  %dead2 = sub i32 %dead1, 5\n"
              "  ret void\n"
              "}\n");

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Store and its operands should remain, dead code should be removed
    EXPECT_EQ(IRPrinter().print(func),
              "define void @test_func(i32* %ptr) {\n"
              "entry:\n"
              "  %val = mul i32 5, 6\n"
              "  store i32 %val, i32* %ptr\n"
              "  ret void\n"
              "}\n");
}

TEST_F(ADCETest, ConditionalBranchWithDeadCode) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(intType, {boolType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    builder->setInsertPoint(entryBB);
    auto cond = func->getArg(0);
    cond->setName("cond");

    // Dead code in entry
    builder->createAdd(builder->getInt32(1), builder->getInt32(2),
                       "dead_entry");

    builder->createCondBr(cond, trueBB, falseBB);

    // True branch
    builder->setInsertPoint(trueBB);
    auto trueVal = builder->getInt32(10);
    // Dead code in true branch
    builder->createMul(trueVal, builder->getInt32(2), "dead_true");
    builder->createBr(mergeBB);

    // False branch
    builder->setInsertPoint(falseBB);
    auto falseVal = builder->getInt32(20);
    // Dead code in false branch
    builder->createAdd(falseVal, builder->getInt32(5), "dead_false");
    builder->createBr(mergeBB);

    // Merge block with PHI
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "result");
    phi->addIncoming(trueVal, trueBB);
    phi->addIncoming(falseVal, falseBB);
    builder->createRet(phi);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  %dead_entry = add i32 1, 2\n"
              "  br i1 %cond, label %true_bb, label %false_bb\n"
              "true_bb:\n"
              "  %dead_true = mul i32 10, 2\n"
              "  br label %merge\n"
              "false_bb:\n"
              "  %dead_false = add i32 20, 5\n"
              "  br label %merge\n"
              "merge:\n"
              "  %result = phi i32 [ 10, %true_bb ], [ 20, %false_bb ]\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // All dead code should be removed, but control flow remains
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i1 %cond) {\n"
              "entry:\n"
              "  br i1 %cond, label %true_bb, label %false_bb\n"
              "true_bb:\n"
              "  br label %merge\n"
              "false_bb:\n"
              "  br label %merge\n"
              "merge:\n"
              "  %result = phi i32 [ 10, %true_bb ], [ 20, %false_bb ]\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(ADCETest, NoDeadCode) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto param1 = func->getArg(0);
    auto param2 = func->getArg(1);
    param1->setName("a");
    param2->setName("b");

    // All instructions contribute to the return value
    auto add = builder->createAdd(param1, param2, "add");
    auto mul = builder->createMul(add, param1, "mul");
    auto sub = builder->createSub(mul, param2, "sub");
    builder->createRet(sub);

    std::string beforeIR = IRPrinter().print(func);

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // Nothing should change
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(ADCETest, CallInstructionsAlwaysLive) {
    auto intType = ctx->getIntegerType(32);
    auto voidType = ctx->getVoidType();

    // Create a function to call
    auto calleeFuncType = FunctionType::get(intType, {intType});
    auto calleeFunc = Function::Create(calleeFuncType, "callee", module.get());

    // Create main function
    auto funcType = FunctionType::get(voidType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Dead arithmetic before call
    auto dead1 = builder->createAdd(builder->getInt32(10),
                                    builder->getInt32(20), "dead1");

    // Call instruction - always live even if result unused
    builder->createCall(calleeFunc, {builder->getInt32(42)}, "call_result");

    // Dead arithmetic after call
    builder->createMul(dead1, builder->getInt32(2), "dead2");

    builder->createRetVoid();

    EXPECT_EQ(IRPrinter().print(func),
              "define void @test_func() {\n"
              "entry:\n"
              "  %dead1 = add i32 10, 20\n"
              "  %call_result = call i32 @callee(i32 42)\n"
              "  %dead2 = mul i32 %dead1, 2\n"
              "  ret void\n"
              "}\n");

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Call and its arguments should remain, dead code should be removed
    EXPECT_EQ(IRPrinter().print(func),
              "define void @test_func() {\n"
              "entry:\n"
              "  %call_result = call i32 @callee(i32 42)\n"
              "  ret void\n"
              "}\n");
}

TEST_F(ADCETest, PartiallyDeadComputationChain) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType = FunctionType::get(intType, {intType, ptrType});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto param = func->getArg(0);
    auto ptr = func->getArg(1);
    param->setName("x");
    ptr->setName("ptr");

    // Create a computation chain where only part is used
    auto val1 = builder->createAdd(param, builder->getInt32(10), "val1");
    auto val2 = builder->createMul(val1, builder->getInt32(2), "val2");
    auto val3 = builder->createSub(val2, param, "val3");
    auto val4 = builder->createAdd(val3, val1, "val4");  // Uses val3 and val1

    // Store val3 (makes val1, val2, val3 live, but not val4)
    builder->createStore(val3, ptr);

    // Create another dead chain
    auto dead1 = builder->createMul(val4, builder->getInt32(3), "dead1");
    builder->createAdd(dead1, val2, "dead2");

    // Return a different value
    builder->createRet(param);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x, i32* %ptr) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  %val3 = sub i32 %val2, %x\n"
              "  %val4 = add i32 %val3, %val1\n"
              "  store i32 %val3, i32* %ptr\n"
              "  %dead1 = mul i32 %val4, 3\n"
              "  %dead2 = add i32 %dead1, %val2\n"
              "  ret i32 %x\n"
              "}\n");

    // Run ADCE Pass
    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // val1, val2, val3 should remain (used by store)
    // val4, dead1, dead2 should be removed
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %x, i32* %ptr) {\n"
              "entry:\n"
              "  %val1 = add i32 %x, 10\n"
              "  %val2 = mul i32 %val1, 2\n"
              "  %val3 = sub i32 %val2, %x\n"
              "  store i32 %val3, i32* %ptr\n"
              "  ret i32 %x\n"
              "}\n");
}

TEST_F(ADCETest, EliminateAfterMem2Reg) {
    GTEST_SKIP();
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
    builder->createLoad(alloca, "final_load");
    builder->createRet(func->getArg(0));

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
              "  ret i32 %arg0\n"
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
              "  ret i32 %arg0\n"
              "}\n");

    // Run ADCE Pass again to eliminate dead code
    ADCEPass adcePass;
    changed = adcePass.runOnFunction(*func, *am);
    EXPECT_TRUE(changed);
    // All dead code should be removed, but control flow remains
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "  ret i32 %arg0\n"
              "}\n");
}