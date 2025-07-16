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
        am->registerAnalysisType<PostDominanceAnalysis>();
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;

    // Helper function to create a function with if-else structure
    Function* createIfElseFunction(const std::string& name, bool useVars,
                                   bool hasStoreInTrue, bool hasStoreInFalse) {
        auto intType = ctx->getIntegerType(32);
        auto ptrType = PointerType::get(intType);
        auto funcType =
            FunctionType::get(intType, {ctx->getIntegerType(1), ptrType});
        auto func = Function::Create(funcType, name, module.get());

        auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
        auto trueBB = BasicBlock::Create(ctx.get(), "true_bb", func);
        auto falseBB = BasicBlock::Create(ctx.get(), "false_bb", func);
        auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

        auto cond = func->getArg(0);
        auto ptr = func->getArg(1);
        cond->setName("cond");
        ptr->setName("ptr");

        // Entry block
        builder->setInsertPoint(entryBB);
        auto var1 = builder->createAdd(builder->getInt32(10),
                                       builder->getInt32(20), "var1");
        auto var2 = builder->createMul(builder->getInt32(5),
                                       builder->getInt32(6), "var2");
        builder->createCondBr(cond, trueBB, falseBB);

        // True branch
        builder->setInsertPoint(trueBB);
        auto trueVal =
            builder->createAdd(var1, builder->getInt32(1), "true_val");
        if (hasStoreInTrue) {
            builder->createStore(trueVal, ptr);
        }
        builder->createBr(mergeBB);

        // False branch
        builder->setInsertPoint(falseBB);
        auto falseVal =
            builder->createMul(var2, builder->getInt32(2), "false_val");
        if (hasStoreInFalse) {
            builder->createStore(falseVal, ptr);
        }
        builder->createBr(mergeBB);

        // Merge block
        builder->setInsertPoint(mergeBB);
        if (useVars) {
            auto phi = builder->createPHI(intType, "result");
            phi->addIncoming(trueVal, trueBB);
            phi->addIncoming(falseVal, falseBB);
            builder->createRet(phi);
        } else {
            builder->createRet(builder->getInt32(42));
        }

        return func;
    }

    // Helper function to create a function with loop structure
    Function* createLoopFunction(const std::string& name, bool finite,
                                 bool useVars, bool hasStoreInLoop) {
        auto intType = ctx->getIntegerType(32);
        auto ptrType = PointerType::get(intType);
        auto funcType = FunctionType::get(intType, {intType, ptrType});
        auto func = Function::Create(funcType, name, module.get());

        auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
        auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
        auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

        auto param = func->getArg(0);
        auto ptr = func->getArg(1);
        param->setName("param");
        ptr->setName("ptr");

        // Entry block
        builder->setInsertPoint(entryBB);
        auto var1 = builder->createAdd(param, builder->getInt32(10), "var1");
        auto var2 = builder->createMul(param, builder->getInt32(2), "var2");
        builder->createBr(loopBB);

        // Loop block
        builder->setInsertPoint(loopBB);
        auto phi = builder->createPHI(intType, "i");
        phi->addIncoming(builder->getInt32(0), entryBB);

        auto loopVal = builder->createAdd(phi, var1, "loop_val");
        if (hasStoreInLoop) {
            builder->createStore(loopVal, ptr);
        }

        auto nextI = builder->createAdd(phi, builder->getInt32(1), "next_i");
        phi->addIncoming(nextI, loopBB);

        if (finite) {
            auto cond =
                builder->createICmpSLT(nextI, builder->getInt32(10), "cond");
            builder->createCondBr(cond, loopBB, exitBB);
        } else {
            builder->createBr(loopBB);  // Infinite loop
        }

        // Exit block (only for finite loops)
        if (finite) {
            builder->setInsertPoint(exitBB);
            if (useVars) {
                auto result = builder->createAdd(var1, var2, "result");
                builder->createRet(result);
            } else {
                builder->createRet(builder->getInt32(100));
            }
        }

        return func;
    }

    // Helper function to create nested if structure
    Function* createNestedIfFunction(const std::string& name, bool useVars,
                                     bool hasStore) {
        auto intType = ctx->getIntegerType(32);
        auto ptrType = PointerType::get(intType);
        auto funcType = FunctionType::get(
            intType, {ctx->getIntegerType(1), ctx->getIntegerType(1), ptrType});
        auto func = Function::Create(funcType, name, module.get());

        auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
        auto outerTrueBB = BasicBlock::Create(ctx.get(), "outer_true", func);
        auto innerTrueBB = BasicBlock::Create(ctx.get(), "inner_true", func);
        auto innerFalseBB = BasicBlock::Create(ctx.get(), "inner_false", func);
        auto outerFalseBB = BasicBlock::Create(ctx.get(), "outer_false", func);
        auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

        auto cond1 = func->getArg(0);
        auto cond2 = func->getArg(1);
        auto ptr = func->getArg(2);
        cond1->setName("cond1");
        cond2->setName("cond2");
        ptr->setName("ptr");

        // Entry
        builder->setInsertPoint(entryBB);
        auto var1 = builder->createAdd(builder->getInt32(10),
                                       builder->getInt32(20), "var1");
        auto var2 = builder->createMul(builder->getInt32(5),
                                       builder->getInt32(6), "var2");
        builder->createCondBr(cond1, outerTrueBB, outerFalseBB);

        // Outer true - has nested if
        builder->setInsertPoint(outerTrueBB);
        auto outerVal =
            builder->createAdd(var1, builder->getInt32(1), "outer_val");
        builder->createCondBr(cond2, innerTrueBB, innerFalseBB);

        // Inner true
        builder->setInsertPoint(innerTrueBB);
        auto innerTrueVal = builder->createMul(outerVal, builder->getInt32(2),
                                               "inner_true_val");
        if (hasStore) {
            builder->createStore(innerTrueVal, ptr);
        }
        builder->createBr(mergeBB);

        // Inner false
        builder->setInsertPoint(innerFalseBB);
        auto innerFalseVal = builder->createSub(outerVal, builder->getInt32(3),
                                                "inner_false_val");
        builder->createBr(mergeBB);

        // Outer false
        builder->setInsertPoint(outerFalseBB);
        auto outerFalseVal =
            builder->createMul(var2, builder->getInt32(3), "outer_false_val");
        builder->createBr(mergeBB);

        // Merge
        builder->setInsertPoint(mergeBB);
        if (useVars) {
            auto phi = builder->createPHI(intType, "result");
            phi->addIncoming(innerTrueVal, innerTrueBB);
            phi->addIncoming(innerFalseVal, innerFalseBB);
            phi->addIncoming(outerFalseVal, outerFalseBB);
            builder->createRet(phi);
        } else {
            builder->createRet(builder->getInt32(0));
        }

        return func;
    }
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
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  br label %bb1\n"
              "bb1:\n"
              "  br label %bb3\n"
              "bb2:\n"
              "  br label %bb3\n"
              "bb3:\n"
              "  br label %bb4\n"
              "bb4:\n"
              "  br label %exit\n"
              "exit:\n"
              "  ret i32 %arg0\n"
              "}\n");
}

// ===================== IF STATEMENT TESTS =====================

TEST_F(ADCETest, IfElseWithUsedVariables) {
    auto func = createIfElseFunction("test_if_used", true, false, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 should be kept (used in true branch), var2 should be kept (used in
    // false branch)
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("true_val"), std::string::npos);
    EXPECT_NE(result.find("false_val"), std::string::npos);
}

TEST_F(ADCETest, IfElseWithUnusedVariables) {
    auto func = createIfElseFunction("test_if_unused", false, false, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // All variables should be eliminated since they're not used in return
    std::string result = IRPrinter().print(func);
    EXPECT_EQ(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("true_val"), std::string::npos);
    EXPECT_EQ(result.find("false_val"), std::string::npos);
}

TEST_F(ADCETest, IfElseWithStoreInTrueBranch) {
    auto func = createIfElseFunction("test_if_store_true", false, true, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 and true_val should be kept due to store, var2 and false_val should
    // be eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("true_val"), std::string::npos);
    EXPECT_EQ(result.find("false_val"), std::string::npos);
}

TEST_F(ADCETest, IfElseWithStoreInBothBranches) {
    auto func = createIfElseFunction("test_if_store_both", false, true, true);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // All variables should be kept due to stores
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("true_val"), std::string::npos);
    EXPECT_NE(result.find("false_val"), std::string::npos);
}

TEST_F(ADCETest, NestedIfWithUsedVariables) {
    auto func = createNestedIfFunction("test_nested_if_used", true, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 should be kept (used in inner paths), var2 should be kept (used in
    // outer false)
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
}

TEST_F(ADCETest, NestedIfWithUnusedVariables) {
    auto func = createNestedIfFunction("test_nested_if_unused", false, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // All computation should be eliminated since return is constant
    std::string result = IRPrinter().print(func);
    EXPECT_EQ(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
}

TEST_F(ADCETest, NestedIfWithStoreInInnerBranch) {
    auto func = createNestedIfFunction("test_nested_if_store", false, true);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 and computation leading to store should be kept
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("inner_true_val"), std::string::npos);
}

// ===================== LOOP TESTS =====================

TEST_F(ADCETest, FiniteLoopWithUsedVariables) {
    auto func = createLoopFunction("test_finite_loop_used", true, true, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // All variables should be kept since they're used in return
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("loop_val"), std::string::npos);
}

TEST_F(ADCETest, FiniteLoopWithUnusedVariables) {
    auto func =
        createLoopFunction("test_finite_loop_unused", true, false, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Loop variables should be eliminated since they're not used in return
    std::string result = IRPrinter().print(func);
    EXPECT_EQ(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("loop_val"), std::string::npos);
}

TEST_F(ADCETest, FiniteLoopWithStoreInLoop) {
    auto func = createLoopFunction("test_finite_loop_store", true, false, true);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // Loop variables should be kept due to store
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("loop_val"), std::string::npos);
}

TEST_F(ADCETest, InfiniteLoopWithStore) {
    auto func =
        createLoopFunction("test_infinite_loop_store", false, false, true);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // All loop variables should be kept due to store
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("loop_val"), std::string::npos);
}

TEST_F(ADCETest, InfiniteLoopWithoutStore) {
    auto func =
        createLoopFunction("test_infinite_loop_no_store", false, false, false);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // Variables should be eliminated since they have no effect
    std::string result = IRPrinter().print(func);
    EXPECT_EQ(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("loop_val"), std::string::npos);
}

// ===================== COMPOSITE STRUCTURE TESTS =====================

TEST_F(ADCETest, IfInsideLoop) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType = FunctionType::get(intType, {intType, ptrType});
    auto func = Function::Create(funcType, "test_if_in_loop", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto ifTrueBB = BasicBlock::Create(ctx.get(), "if_true", func);
    auto ifFalseBB = BasicBlock::Create(ctx.get(), "if_false", func);
    auto loopContBB = BasicBlock::Create(ctx.get(), "loop_cont", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto param = func->getArg(0);
    auto ptr = func->getArg(1);
    param->setName("param");
    ptr->setName("ptr");

    // Entry
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(5), "var1");
    auto var2 = builder->createMul(param, builder->getInt32(3), "var2");
    builder->createBr(loopBB);

    // Loop header
    builder->setInsertPoint(loopBB);
    auto phi = builder->createPHI(intType, "i");
    phi->addIncoming(builder->getInt32(0), entryBB);

    auto cond1 =
        builder->createICmpSLT(phi, builder->getInt32(10), "loop_cond");
    builder->createCondBr(cond1, ifTrueBB, exitBB);

    // If true branch inside loop
    builder->setInsertPoint(ifTrueBB);
    auto cond2 = builder->createICmpSGT(phi, builder->getInt32(5), "if_cond");
    builder->createCondBr(cond2, ifFalseBB, loopContBB);

    // If false branch (var1 used, var2 dead)
    builder->setInsertPoint(ifFalseBB);
    auto storeVal = builder->createAdd(var1, phi, "store_val");
    builder->createStore(storeVal, ptr);
    builder->createBr(loopContBB);

    // Loop continuation
    builder->setInsertPoint(loopContBB);
    auto deadVal = builder->createMul(var2, builder->getInt32(2), "dead_val");
    auto nextI = builder->createAdd(phi, builder->getInt32(1), "next_i");
    phi->addIncoming(nextI, loopContBB);
    builder->createBr(loopBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(param);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 should be kept (used in store), var2 and dead_val should be
    // eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("dead_val"), std::string::npos);
}

TEST_F(ADCETest, LoopInsideIf) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType =
        FunctionType::get(intType, {ctx->getIntegerType(1), intType, ptrType});
    auto func = Function::Create(funcType, "test_loop_in_if", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto ifTrueBB = BasicBlock::Create(ctx.get(), "if_true", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto ifFalseBB = BasicBlock::Create(ctx.get(), "if_false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto param = func->getArg(1);
    auto ptr = func->getArg(2);
    cond->setName("cond");
    param->setName("param");
    ptr->setName("ptr");

    // Entry
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(10), "var1");
    auto var2 = builder->createMul(param, builder->getInt32(2), "var2");
    builder->createCondBr(cond, ifTrueBB, ifFalseBB);

    // If true - has loop
    builder->setInsertPoint(ifTrueBB);
    auto initVal = builder->createAdd(var1, builder->getInt32(1), "init_val");
    builder->createBr(loopBB);

    // Loop inside if
    builder->setInsertPoint(loopBB);
    auto phi = builder->createPHI(intType, "loop_var");
    phi->addIncoming(initVal, ifTrueBB);

    auto loopVal = builder->createMul(phi, builder->getInt32(2), "loop_val");
    builder->createStore(loopVal, ptr);

    auto nextVal = builder->createAdd(phi, builder->getInt32(1), "next_val");
    auto loopCond =
        builder->createICmpSLT(nextVal, builder->getInt32(20), "loop_cond");
    phi->addIncoming(nextVal, loopBB);
    builder->createCondBr(loopCond, loopBB, mergeBB);

    // If false - dead computation
    builder->setInsertPoint(ifFalseBB);
    auto deadVal = builder->createSub(var2, builder->getInt32(5), "dead_val");
    builder->createBr(mergeBB);

    // Merge
    builder->setInsertPoint(mergeBB);
    builder->createRet(param);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 should be kept (used in loop), var2 and dead_val should be
    // eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("dead_val"), std::string::npos);
}

TEST_F(ADCETest, NestedLoops) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType = FunctionType::get(intType, {intType, ptrType});
    auto func = Function::Create(funcType, "test_nested_loops", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto outerLoopBB = BasicBlock::Create(ctx.get(), "outer_loop", func);
    auto innerLoopBB = BasicBlock::Create(ctx.get(), "inner_loop", func);
    auto innerExitBB = BasicBlock::Create(ctx.get(), "inner_exit", func);
    auto outerExitBB = BasicBlock::Create(ctx.get(), "outer_exit", func);

    auto param = func->getArg(0);
    auto ptr = func->getArg(1);
    param->setName("param");
    ptr->setName("ptr");

    // Entry
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(5), "var1");
    auto var2 = builder->createMul(param, builder->getInt32(3), "var2");
    builder->createBr(outerLoopBB);

    // Outer loop
    builder->setInsertPoint(outerLoopBB);
    auto outerPhi = builder->createPHI(intType, "outer_i");
    outerPhi->addIncoming(builder->getInt32(0), entryBB);

    auto outerVal = builder->createAdd(outerPhi, var1, "outer_val");
    builder->createBr(innerLoopBB);

    // Inner loop
    builder->setInsertPoint(innerLoopBB);
    auto innerPhi = builder->createPHI(intType, "inner_j");
    innerPhi->addIncoming(builder->getInt32(0), outerLoopBB);

    auto innerVal = builder->createMul(innerPhi, outerVal, "inner_val");
    builder->createStore(innerVal, ptr);

    auto deadComputation = builder->createSub(var2, innerPhi, "dead_comp");

    auto nextJ = builder->createAdd(innerPhi, builder->getInt32(1), "next_j");
    auto innerCond =
        builder->createICmpSLT(nextJ, builder->getInt32(5), "inner_cond");
    innerPhi->addIncoming(nextJ, innerLoopBB);
    builder->createCondBr(innerCond, innerLoopBB, innerExitBB);

    // Inner exit
    builder->setInsertPoint(innerExitBB);
    auto nextI = builder->createAdd(outerPhi, builder->getInt32(1), "next_i");
    auto outerCond =
        builder->createICmpSLT(nextI, builder->getInt32(3), "outer_cond");
    outerPhi->addIncoming(nextI, innerExitBB);
    builder->createCondBr(outerCond, outerLoopBB, outerExitBB);

    // Outer exit
    builder->setInsertPoint(outerExitBB);
    builder->createRet(param);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 should be kept (used in store chain), var2 and dead_comp should be
    // eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("dead_comp"), std::string::npos);
}

// ===================== COMPLEX CFG PATTERNS =====================

TEST_F(ADCETest, EarlyReturn) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType = FunctionType::get(intType, {intType, ptrType});
    auto func = Function::Create(funcType, "test_early_return", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto earlyRetBB = BasicBlock::Create(ctx.get(), "early_return", func);
    auto contBB = BasicBlock::Create(ctx.get(), "continue", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto param = func->getArg(0);
    auto ptr = func->getArg(1);
    param->setName("param");
    ptr->setName("ptr");

    // Entry
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(10), "var1");
    auto var2 = builder->createMul(param, builder->getInt32(2), "var2");
    auto var3 = builder->createSub(param, builder->getInt32(5), "var3");

    auto cond = builder->createICmpSLT(param, builder->getInt32(0), "cond");
    builder->createCondBr(cond, earlyRetBB, contBB);

    // Early return path - uses var1
    builder->setInsertPoint(earlyRetBB);
    builder->createRet(var1);

    // Continue path - uses var2 in store, var3 is dead
    builder->setInsertPoint(contBB);
    builder->createStore(var2, ptr);
    auto deadVal = builder->createAdd(var3, builder->getInt32(1), "dead_val");
    builder->createBr(exitBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 and var2 should be kept, var3 and dead_val should be eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("var3"), std::string::npos);
    EXPECT_EQ(result.find("dead_val"), std::string::npos);
}

TEST_F(ADCETest, ReturnInLoop) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType = FunctionType::get(intType, {intType, ptrType});
    auto func = Function::Create(funcType, "test_return_in_loop", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto returnBB = BasicBlock::Create(ctx.get(), "return_bb", func);
    auto contBB = BasicBlock::Create(ctx.get(), "continue", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto param = func->getArg(0);
    auto ptr = func->getArg(1);
    param->setName("param");
    ptr->setName("ptr");

    // Entry
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(10), "var1");
    auto var2 = builder->createMul(param, builder->getInt32(3), "var2");
    builder->createBr(loopBB);

    // Loop
    builder->setInsertPoint(loopBB);
    auto phi = builder->createPHI(intType, "i");
    phi->addIncoming(builder->getInt32(0), entryBB);

    auto loopVal = builder->createAdd(phi, var1, "loop_val");
    auto cond1 =
        builder->createICmpEQ(loopVal, builder->getInt32(15), "return_cond");
    builder->createCondBr(cond1, returnBB, contBB);

    // Return from loop - uses loopVal
    builder->setInsertPoint(returnBB);
    builder->createRet(loopVal);

    // Continue loop - uses var2
    builder->setInsertPoint(contBB);
    builder->createStore(var2, ptr);
    auto nextI = builder->createAdd(phi, builder->getInt32(1), "next_i");
    auto cond2 =
        builder->createICmpSLT(nextI, builder->getInt32(20), "loop_cond");
    phi->addIncoming(nextI, contBB);
    builder->createCondBr(cond2, loopBB, exitBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(0));

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // All variables should be kept as they're used in different paths
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("loop_val"), std::string::npos);
}

TEST_F(ADCETest, ReturnInIf) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType =
        FunctionType::get(intType, {ctx->getIntegerType(1), intType, ptrType});
    auto func = Function::Create(funcType, "test_return_in_if", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto ifTrueBB = BasicBlock::Create(ctx.get(), "if_true", func);
    auto ifFalseBB = BasicBlock::Create(ctx.get(), "if_false", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    auto cond = func->getArg(0);
    auto param = func->getArg(1);
    auto ptr = func->getArg(2);
    cond->setName("cond");
    param->setName("param");
    ptr->setName("ptr");

    // Entry
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(5), "var1");
    auto var2 = builder->createMul(param, builder->getInt32(2), "var2");
    auto var3 = builder->createSub(param, builder->getInt32(1), "var3");
    builder->createCondBr(cond, ifTrueBB, ifFalseBB);

    // If true - early return using var1
    builder->setInsertPoint(ifTrueBB);
    auto returnVal =
        builder->createAdd(var1, builder->getInt32(10), "return_val");
    builder->createRet(returnVal);

    // If false - uses var2 in store, var3 is dead
    builder->setInsertPoint(ifFalseBB);
    builder->createStore(var2, ptr);
    auto deadVal = builder->createMul(var3, builder->getInt32(4), "dead_val");
    builder->createBr(exitBB);

    // Exit
    builder->setInsertPoint(exitBB);
    builder->createRet(builder->getInt32(100));

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1 and var2 should be kept, var3 and dead_val should be eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("var3"), std::string::npos);
    EXPECT_EQ(result.find("dead_val"), std::string::npos);
}

// ===================== VARIABLE USAGE PATTERN TESTS =====================

TEST_F(ADCETest, PartialVariableUsageInComplexCFG) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType =
        FunctionType::get(intType, {ctx->getIntegerType(1), intType, ptrType});
    auto func = Function::Create(funcType, "test_partial_usage", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto branch1BB = BasicBlock::Create(ctx.get(), "branch1", func);
    auto branch2BB = BasicBlock::Create(ctx.get(), "branch2", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto param = func->getArg(1);
    auto ptr = func->getArg(2);
    cond->setName("cond");
    param->setName("param");
    ptr->setName("ptr");

    // Entry - create multiple variables
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(1), "var1");
    auto var2 = builder->createAdd(param, builder->getInt32(2), "var2");
    auto var3 = builder->createAdd(param, builder->getInt32(3), "var3");
    auto var4 = builder->createAdd(param, builder->getInt32(4), "var4");
    auto var5 = builder->createAdd(param, builder->getInt32(5), "var5");
    builder->createCondBr(cond, branch1BB, branch2BB);

    // Branch 1 - uses var1, var3, var5 (odd numbered)
    builder->setInsertPoint(branch1BB);
    auto use1 = builder->createMul(var1, var3, "use1");
    auto use2 = builder->createAdd(use1, var5, "use2");
    builder->createStore(use2, ptr);
    builder->createBr(mergeBB);

    // Branch 2 - uses var2, var4 (even numbered)
    builder->setInsertPoint(branch2BB);
    auto use3 = builder->createSub(var2, var4, "use3");
    builder->createStore(use3, ptr);
    builder->createBr(mergeBB);

    // Merge - return param (no variable usage)
    builder->setInsertPoint(mergeBB);
    builder->createRet(param);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // All variables should be kept as they're used in different branches
    std::string result = IRPrinter().print(func);
    EXPECT_NE(result.find("var1"), std::string::npos);
    EXPECT_NE(result.find("var2"), std::string::npos);
    EXPECT_NE(result.find("var3"), std::string::npos);
    EXPECT_NE(result.find("var4"), std::string::npos);
    EXPECT_NE(result.find("var5"), std::string::npos);
}

TEST_F(ADCETest, NoVariableUsageInComplexCFG) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType =
        FunctionType::get(intType, {ctx->getIntegerType(1), intType, ptrType});
    auto func = Function::Create(funcType, "test_no_usage", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto branch1BB = BasicBlock::Create(ctx.get(), "branch1", func);
    auto branch2BB = BasicBlock::Create(ctx.get(), "branch2", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto param = func->getArg(1);
    auto ptr = func->getArg(2);
    cond->setName("cond");
    param->setName("param");
    ptr->setName("ptr");

    // Entry - create multiple variables but don't use them
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(1), "var1");
    auto var2 = builder->createAdd(param, builder->getInt32(2), "var2");
    auto var3 = builder->createAdd(param, builder->getInt32(3), "var3");
    auto var4 = builder->createAdd(param, builder->getInt32(4), "var4");
    auto var5 = builder->createAdd(param, builder->getInt32(5), "var5");
    (void)var1;
    (void)var2;
    (void)var3;
    (void)var4;
    (void)var5;  // Suppress unused variable warnings
    builder->createCondBr(cond, branch1BB, branch2BB);

    // Branch 1 - no variable usage
    builder->setInsertPoint(branch1BB);
    builder->createStore(builder->getInt32(100), ptr);
    builder->createBr(mergeBB);

    // Branch 2 - no variable usage
    builder->setInsertPoint(branch2BB);
    builder->createStore(builder->getInt32(200), ptr);
    builder->createBr(mergeBB);

    // Merge - return param (no variable usage)
    builder->setInsertPoint(mergeBB);
    builder->createRet(param);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // All variables should be eliminated
    std::string result = IRPrinter().print(func);
    EXPECT_EQ(result.find("var1"), std::string::npos);
    EXPECT_EQ(result.find("var2"), std::string::npos);
    EXPECT_EQ(result.find("var3"), std::string::npos);
    EXPECT_EQ(result.find("var4"), std::string::npos);
    EXPECT_EQ(result.find("var5"), std::string::npos);
}

// ===================== COMBINED FEATURE TESTS =====================

TEST_F(ADCETest, IfLoopCombinedWithPartialUsageAndStore) {
    auto intType = ctx->getIntegerType(32);
    auto ptrType = PointerType::get(intType);
    auto funcType =
        FunctionType::get(intType, {ctx->getIntegerType(1), intType, ptrType});
    auto func = Function::Create(funcType, "test_combined", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto ifTrueBB = BasicBlock::Create(ctx.get(), "if_true", func);
    auto loopBB = BasicBlock::Create(ctx.get(), "loop", func);
    auto ifFalseBB = BasicBlock::Create(ctx.get(), "if_false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto cond = func->getArg(0);
    auto param = func->getArg(1);
    auto ptr = func->getArg(2);
    cond->setName("cond");
    param->setName("param");
    ptr->setName("ptr");

    // Entry - multiple variables with different usage patterns
    builder->setInsertPoint(entryBB);
    auto var1 = builder->createAdd(param, builder->getInt32(10),
                                   "var1");  // Used in loop
    auto var2 = builder->createMul(param, builder->getInt32(2),
                                   "var2");  // Used in return
    auto var3 =
        builder->createSub(param, builder->getInt32(5), "var3");  // Dead
    auto var4 = builder->createAdd(param, builder->getInt32(20),
                                   "var4");  // Used in store
    builder->createCondBr(cond, ifTrueBB, ifFalseBB);

    // If true - has loop with store
    builder->setInsertPoint(ifTrueBB);
    auto initVal = builder->createAdd(var1, builder->getInt32(1), "init_val");
    builder->createBr(loopBB);

    // Loop - uses var1 and var4, stores result
    builder->setInsertPoint(loopBB);
    auto phi = builder->createPHI(intType, "loop_var");
    phi->addIncoming(initVal, ifTrueBB);

    auto loopVal = builder->createAdd(phi, var4, "loop_val");
    builder->createStore(loopVal, ptr);

    auto deadInLoop =
        builder->createMul(var3, builder->getInt32(3), "dead_in_loop");
    (void)deadInLoop;  // Suppress unused variable warning

    auto nextVal = builder->createAdd(phi, builder->getInt32(1), "next_val");
    auto loopCond =
        builder->createICmpSLT(nextVal, builder->getInt32(15), "loop_cond");
    phi->addIncoming(nextVal, loopBB);
    builder->createCondBr(loopCond, loopBB, mergeBB);

    // If false - no variable usage
    builder->setInsertPoint(ifFalseBB);
    auto deadInFalse =
        builder->createDiv(var3, builder->getInt32(2), "dead_in_false");
    (void)deadInFalse;  // Suppress unused variable warning
    builder->createBr(mergeBB);

    // Merge - return uses var2
    builder->setInsertPoint(mergeBB);
    auto result = builder->createAdd(var2, builder->getInt32(100), "result");
    builder->createRet(result);

    ADCEPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // var1, var2, var4 should be kept; var3 and dead computations should be
    // eliminated
    std::string ir_result = IRPrinter().print(func);
    EXPECT_NE(ir_result.find("var1"), std::string::npos);
    EXPECT_NE(ir_result.find("var2"), std::string::npos);
    EXPECT_EQ(ir_result.find("var3"), std::string::npos);
    EXPECT_NE(ir_result.find("var4"), std::string::npos);
    EXPECT_EQ(ir_result.find("dead_in_loop"), std::string::npos);
    EXPECT_EQ(ir_result.find("dead_in_false"), std::string::npos);
}