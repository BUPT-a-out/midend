#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "Pass/Pass.h"
#include "Pass/Transform/ConstantPropagationPass.h"

using namespace midend;

class ConstantPropagationTest : public ::testing::Test {
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

TEST_F(ConstantPropagationTest, SimpleBinaryConstantFolding) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: 10 + 20
    auto add = builder->createAdd(builder->getInt32(10), builder->getInt32(20), "result");
    builder->createRet(add);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %result = add i32 10, 20\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 30\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, ChainedBinaryOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: ((5 + 10) * 2) - 4
    auto add = builder->createAdd(builder->getInt32(5), builder->getInt32(10), "add");
    auto mul = builder->createMul(add, builder->getInt32(2), "mul");
    auto sub = builder->createSub(mul, builder->getInt32(4), "result");
    builder->createRet(sub);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %add = add i32 5, 10\n"
              "  %mul = mul i32 %add, 2\n"
              "  %result = sub i32 %mul, 4\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 26\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, MixedConstantAndVariable) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: (arg0 + 5) + 10
    auto add1 = builder->createAdd(func->getArg(0), builder->getInt32(5), "add1");
    auto add2 = builder->createAdd(add1, builder->getInt32(10), "result");
    builder->createRet(add2);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %add1 = add i32 %arg0, 5\n"
              "  %result = add i32 %add1, 10\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should not change because arg0 is not constant
    EXPECT_FALSE(changed);
}

TEST_F(ConstantPropagationTest, ConstantComparison) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: 10 < 20
    auto cmp = builder->createICmpSLT(builder->getInt32(10), builder->getInt32(20), "cmp");
    auto ext = builder->createCast(CastInst::ZExt, cmp, intType, "result");
    builder->createRet(ext);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %cmp = icmp slt i32 10, 20\n"
              "  %result = zext i1 %cmp to i32\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %result = zext i1 1 to i32\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, ConstantSelect) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: select true, 100, 200
    auto select = builder->createSelect(builder->getInt1(true), 
                                      builder->getInt32(100), 
                                      builder->getInt32(200), "result");
    builder->createRet(select);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %result = select i1 1, i32 100, i32 200\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 100\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, ConditionalWithConstantPropagation) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "if.true", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "if.false", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "if.merge", func);

    builder->setInsertPoint(entryBB);
    auto cond = builder->createICmpSGT(func->getArg(0), builder->getInt32(0), "cond");
    builder->createCondBr(cond, trueBB, falseBB);

    // True branch: return constant 10 + 20
    builder->setInsertPoint(trueBB);
    auto add = builder->createAdd(builder->getInt32(10), builder->getInt32(20), "add");
    builder->createBr(mergeBB);

    // False branch: return constant 30 * 2
    builder->setInsertPoint(falseBB);
    auto mul = builder->createMul(builder->getInt32(30), builder->getInt32(2), "mul");
    builder->createBr(mergeBB);

    // Merge block
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "result");
    phi->addIncoming(add, trueBB);
    phi->addIncoming(mul, falseBB);
    builder->createRet(phi);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %cond = icmp sgt i32 %arg0, 0\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  %add = add i32 10, 20\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  %mul = mul i32 30, 2\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %result = phi i32 [ %add, %if.true ], [ %mul, %if.false ]\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0) {\n"
              "entry:\n"
              "  %cond = icmp sgt i32 %arg0, 0\n"
              "  br i1 %cond, label %if.true, label %if.false\n"
              "if.true:\n"
              "  br label %if.merge\n"
              "if.false:\n"
              "  br label %if.merge\n"
              "if.merge:\n"
              "  %result = phi i32 [ 30, %if.true ], [ 60, %if.false ]\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, PHINodeWithAllConstantValues) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType});
    auto func = Function::Create(funcType, "test_func", {"arg0"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto bb1 = BasicBlock::Create(ctx.get(), "bb1", func);
    auto bb2 = BasicBlock::Create(ctx.get(), "bb2", func);
    auto bb3 = BasicBlock::Create(ctx.get(), "bb3", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    builder->setInsertPoint(entryBB);
    auto cond1 = builder->createICmpEQ(func->getArg(0), builder->getInt32(1), "cond1");
    builder->createCondBr(cond1, bb1, bb2);

    builder->setInsertPoint(bb1);
    builder->createBr(exitBB);

    builder->setInsertPoint(bb2);
    auto cond2 = builder->createICmpEQ(func->getArg(0), builder->getInt32(2), "cond2");
    builder->createCondBr(cond2, bb3, exitBB);

    builder->setInsertPoint(bb3);
    builder->createBr(exitBB);

    // Exit with PHI having all same constant value
    builder->setInsertPoint(exitBB);
    auto phi = builder->createPHI(intType, "result");
    phi->addIncoming(builder->getInt32(42), bb1);
    phi->addIncoming(builder->getInt32(42), bb2);
    phi->addIncoming(builder->getInt32(42), bb3);
    builder->createRet(phi);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // PHI should be replaced with constant 42
    auto result = IRPrinter().print(func);
    EXPECT_NE(result.find("ret i32 42"), std::string::npos);
}

TEST_F(ConstantPropagationTest, FloatConstantFolding) {
    auto floatType = ctx->getFloatType();
    auto funcType = FunctionType::get(floatType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: (2.5 + 3.5) * 2.0
    auto add = builder->createFAdd(builder->getFloat(2.5f), builder->getFloat(3.5f), "add");
    auto mul = builder->createFMul(add, builder->getFloat(2.0f), "result");
    builder->createRet(mul);

    EXPECT_EQ(IRPrinter().print(func),
              "define float @test_func() {\n"
              "entry:\n"
              "  %add = fadd float 2.500000e+00, 3.500000e+00\n"
              "  %result = fmul float %add, 2.000000e+00\n"
              "  ret float %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define float @test_func() {\n"
              "entry:\n"
              "  ret float 1.200000e+01\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, BitwiseOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: (0xFF & 0x0F) | (0xF0 ^ 0xA0)
    auto and_op = builder->createAnd(builder->getInt32(0xFF), builder->getInt32(0x0F), "and");
    auto xor_op = builder->createXor(builder->getInt32(0xF0), builder->getInt32(0xA0), "xor");
    auto or_op = builder->createOr(and_op, xor_op, "result");
    builder->createRet(or_op);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %and = and i32 255, 15\n"
              "  %xor = xor i32 240, 160\n"
              "  %result = or i32 %and, %xor\n"
              "  ret i32 %result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // (0xFF & 0x0F) = 0x0F = 15
    // (0xF0 ^ 0xA0) = 0x50 = 80
    // 15 | 80 = 95
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 95\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, DivisionByZero) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: 10 / 0 (should not fold)
    auto div = builder->createDiv(builder->getInt32(10), builder->getInt32(0), "result");
    builder->createRet(div);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should not change because division by zero is undefined
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %result = sdiv i32 10, 0\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, LogicalOperations) {
    auto intType = ctx->getIntegerType(32);
    auto boolType = ctx->getIntegerType(1);
    auto funcType = FunctionType::get(boolType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: (true && false) || true
    auto land = builder->createLAnd(builder->getInt32(1), builder->getInt32(0), "land");
    auto lor = builder->createLOr(land, builder->getInt32(1), "result");
    auto trunc = builder->createCast(CastInst::Trunc, lor, boolType, "bool_result");
    builder->createRet(trunc);

    EXPECT_EQ(IRPrinter().print(func),
              "define i1 @test_func() {\n"
              "entry:\n"
              "  %land = land i32 1, 0\n"
              "  %result = lor i32 %land, 1\n"
              "  %bool_result = trunc i32 %result to i1\n"
              "  ret i1 %bool_result\n"
              "}\n");

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              "define i1 @test_func() {\n"
              "entry:\n"
              "  %bool_result = trunc i32 1 to i1\n"
              "  ret i1 %bool_result\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, ComplexChainedOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: ((10 + 5) * 3 - 2) / 4 + (20 % 6)
    auto add1 = builder->createAdd(builder->getInt32(10), builder->getInt32(5), "add1");
    auto mul = builder->createMul(add1, builder->getInt32(3), "mul");
    auto sub = builder->createSub(mul, builder->getInt32(2), "sub");
    auto div = builder->createDiv(sub, builder->getInt32(4), "div");
    auto rem = builder->createRem(builder->getInt32(20), builder->getInt32(6), "rem");
    auto result = builder->createAdd(div, rem, "result");
    builder->createRet(result);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // ((10 + 5) * 3 - 2) / 4 + (20 % 6)
    // = (15 * 3 - 2) / 4 + 2
    // = (45 - 2) / 4 + 2
    // = 43 / 4 + 2
    // = 10 + 2
    // = 12
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 12\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, MixedIntegerAndFloatComparison) {
    auto intType = ctx->getIntegerType(32);
    auto floatType = ctx->getFloatType();
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Integer comparison
    auto icmp = builder->createICmpSGE(builder->getInt32(10), builder->getInt32(5), "icmp");
    auto zext1 = builder->createCast(CastInst::ZExt, icmp, intType, "icmp_int");
    
    // Float comparison  
    auto fcmp = builder->createFCmpOLT(builder->getFloat(3.14f), builder->getFloat(2.71f), "fcmp");
    auto zext2 = builder->createCast(CastInst::ZExt, fcmp, intType, "fcmp_int");
    
    // Add results
    auto result = builder->createAdd(zext1, zext2, "result");
    builder->createRet(result);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // 10 >= 5 is true (1), 3.14 < 2.71 is false (0)
    // 1 + 0 = 1
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  %icmp_int = zext i1 1 to i32\n"
              "  %fcmp_int = zext i1 0 to i32\n"
              "  %result = add i32 %icmp_int, %fcmp_int\n"
              "  ret i32 %result\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, SelectWithConstantCondition) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {intType, intType});
    auto func = Function::Create(funcType, "test_func", {"arg0", "arg1"}, module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: select (5 < 10), arg0, arg1
    auto cmp = builder->createICmpSLT(builder->getInt32(5), builder->getInt32(10), "cmp");
    auto select = builder->createSelect(cmp, func->getArg(0), func->getArg(1), "result");
    builder->createRet(select);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // 5 < 10 is true, so select should choose arg0
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func(i32 %arg0, i32 %arg1) {\n"
              "entry:\n"
              "  ret i32 %arg0\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, NegativeNumbers) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create: (-5 + 10) * (-2)
    auto add = builder->createAdd(builder->getInt32(-5), builder->getInt32(10), "add");
    auto mul = builder->createMul(add, builder->getInt32(-2), "result");
    builder->createRet(mul);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // (-5 + 10) * (-2) = 5 * (-2) = -10
    EXPECT_EQ(IRPrinter().print(func),
              "define i32 @test_func() {\n"
              "entry:\n"
              "  ret i32 -10\n"
              "}\n");
}

TEST_F(ConstantPropagationTest, UndefValues) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "test_func", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create operation with undef
    auto undef = UndefValue::get(intType);
    auto add = builder->createAdd(undef, builder->getInt32(10), "result");
    builder->createRet(add);

    // Run ConstantPropagationPass
    ConstantPropagationPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Should not change because one operand is undef
    EXPECT_FALSE(changed);
}