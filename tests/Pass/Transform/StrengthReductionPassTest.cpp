#include <gtest/gtest.h>

#include <memory>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"
#include "Pass/Transform/StrengthReductionPass.h"

using namespace midend;

class StrengthReductionPassTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();

        StrengthReductionPass::mulThreshold = 3;
        StrengthReductionPass::divThreshold = 4;
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

TEST_F(StrengthReductionPassTest, MultiplyByPowerOfTwo) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_power2", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* eight = ConstantInt::get(i32Type, 8);
    auto* mul = builder->createMul(param, eight);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_power2(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 8
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_power2(i32 %arg0) {
entry:
  %0 = shl i32 %arg0, 3
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyBy10) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_10", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* ten = ConstantInt::get(i32Type, 10);
    auto* mul = builder->createMul(param, ten);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_10(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 10
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift and add optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_mul_10(i32 %arg0) {
entry:
  %0 = shl i32 %arg0, 1
  %1 = shl i32 %arg0, 3
  %2 = add i32 %0, %1
  ret i32 %2
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyBy15) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_15", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* fifteen = ConstantInt::get(i32Type, 15);
    auto* mul = builder->createMul(param, fifteen);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_15(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 15
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift and subtract optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_mul_15(i32 %arg0) {
entry:
  %0 = shl i32 %arg0, 4
  %1 = sub i32 %0, %arg0
  ret i32 %1
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyByNegative) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_neg", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* negEight = ConstantInt::get(i32Type, -8);
    auto* mul = builder->createMul(param, negEight);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_neg(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, -8
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift and negate optimization
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @test_mul_neg(i32 %arg0) {
entry:
  %0 = shl i32 %arg0, 3
  %1 = sub i32 0, %0
  ret i32 %1
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyInThreshold) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_threshold", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* val = ConstantInt::get(i32Type, 127);
    auto* mul = builder->createMul(param, val);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_threshold(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 127
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_threshold(i32 %arg0) {
entry:
  %0 = shl i32 %arg0, 7
  %1 = sub i32 %0, %arg0
  ret i32 %1
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyThresholdExceeded) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_threshold", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* val = ConstantInt::get(i32Type, 563);
    auto* mul = builder->createMul(param, val);
    builder->createRet(mul);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_mul_threshold(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 563
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(StrengthReductionPassTest, DivideByPowerOfTwo) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_div_power2", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* sixteen = ConstantInt::get(i32Type, 16);
    auto* div = builder->createDiv(param, sixteen);
    builder->createRet(div);

    // Before pass - expect original division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_power2(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, 16
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_power2(i32 %arg0) {
entry:
  %0 = lshr i32 %arg0, 4
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, SignedDivideByPowerOfTwo) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_sdiv_power2", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* thirtytwo = ConstantInt::get(i32Type, 32);
    auto* div = builder->createDiv(param, thirtytwo);
    builder->createRet(div);

    // Before pass - expect original division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_sdiv_power2(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, 32
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_sdiv_power2(i32 %arg0) {
entry:
  %0 = lshr i32 %arg0, 5
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, DivideThresholdExceeded) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_div_threshold", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* val = ConstantInt::get(i32Type, 7);
    auto* div = builder->createDiv(param, val);
    builder->createRet(div);

    // Before pass - expect original division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_threshold(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, 7
  ret i32 %0
}
)");

    StrengthReductionPass::divThreshold = 2;

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // After pass with low threshold - expect no optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_threshold(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, 7
  ret i32 %0
}
)");

    StrengthReductionPass::divThreshold = 4;
}

TEST_F(StrengthReductionPassTest, MultiplyByZero) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* mul = builder->createMul(param, zero);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_zero(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 0
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding to 0
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_zero(i32 %arg0) {
entry:
  ret i32 0
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyByOne) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_one", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* mul = builder->createMul(param, one);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_one(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 1
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding to x
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_one(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(StrengthReductionPassTest, DivideByOne) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_div_one", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* div = builder->createDiv(param, one);
    builder->createRet(div);

    // Before pass - expect original division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_one(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, 1
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding to x
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_one(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(StrengthReductionPassTest, MultipleOptimizations) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type, i32Type});
    auto* func = Function::Create(funcType, "test_multiple", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param1 = func->getArg(0);
    auto* param2 = func->getArg(1);

    auto* eight = ConstantInt::get(i32Type, 8);
    auto* mul1 = builder->createMul(param1, eight);

    auto* sixteen = ConstantInt::get(i32Type, 16);
    auto* div1 = builder->createDiv(param2, sixteen);

    auto* result = builder->createAdd(mul1, div1);
    builder->createRet(result);

    // Before pass - expect original multiplication and division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_multiple(i32 %arg0, i32 %arg1) {
entry:
  %0 = mul i32 %arg0, 8
  %1 = sdiv i32 %arg1, 16
  %2 = add i32 %0, %1
  ret i32 %2
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect both optimized to shifts
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_multiple(i32 %arg0, i32 %arg1) {
entry:
  %0 = shl i32 %arg0, 3
  %1 = lshr i32 %arg1, 4
  %2 = add i32 %0, %1
  ret i32 %2
}
)");
}

TEST_F(StrengthReductionPassTest, ThresholdBoundaryMul) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func =
        Function::Create(funcType, "test_threshold_boundary", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* val = ConstantInt::get(i32Type, 42);
    auto* mul = builder->createMul(param, val);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_threshold_boundary(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 42
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect shift and add optimization (42 = 32 + 8 + 2)
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_threshold_boundary(i32 %arg0) {
entry:
  %0 = shl i32 %arg0, 1
  %1 = shl i32 %arg0, 3
  %2 = add i32 %0, %1
  %3 = shl i32 %arg0, 5
  %4 = add i32 %2, %3
  ret i32 %4
}
)");
}

TEST_F(StrengthReductionPassTest, NonConstantMultiply) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type, i32Type});
    auto* func = Function::Create(funcType, "test_non_constant", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param1 = func->getArg(0);
    auto* param2 = func->getArg(1);
    auto* mul = builder->createMul(param1, param2);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_non_constant(i32 %arg0, i32 %arg1) {
entry:
  %0 = mul i32 %arg0, %arg1
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // After pass - expect no optimization for non-constant
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_non_constant(i32 %arg0, i32 %arg1) {
entry:
  %0 = mul i32 %arg0, %arg1
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, MultiplyByMinusOne) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_minus_one", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* minusOne = ConstantInt::get(i32Type, -1);
    auto* mul = builder->createMul(param, minusOne);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_minus_one(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, -1
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_minus_one(i32 %arg0) {
entry:
  %0 = sub i32 0, %arg0
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, DivideByMinusOne) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_div_minus_one", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* minusOne = ConstantInt::get(i32Type, -1);
    auto* div = builder->createDiv(param, minusOne);
    builder->createRet(div);

    // Before pass - expect original division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_minus_one(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, -1
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to 0 - x
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_minus_one(i32 %arg0) {
entry:
  %0 = sub i32 0, %arg0
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, DivideByThree) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_div_three", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* three = ConstantInt::get(i32Type, 3);
    auto* div = builder->createDiv(param, three);
    builder->createRet(div);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_div_three(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, 3
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(StrengthReductionPassTest, ModuloByPowerOfTwo) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mod_power2", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* eight = ConstantInt::get(i32Type, 8);
    auto* rem = builder->createRem(param, eight);
    builder->createRet(rem);

    // Before pass - expect original modulo
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mod_power2(i32 %arg0) {
entry:
  %0 = srem i32 %arg0, 8
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect bitwise AND optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mod_power2(i32 %arg0) {
entry:
  %0 = and i32 %arg0, 7
  ret i32 %0
}
)");
}

TEST_F(StrengthReductionPassTest, ModuloByNonPowerOfTwo) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func =
        Function::Create(funcType, "test_mod_non_power2", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* seven = ConstantInt::get(i32Type, 7);
    auto* rem = builder->createRem(param, seven);
    builder->createRet(rem);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_mod_non_power2(i32 %arg0) {
entry:
  %0 = srem i32 %arg0, 7
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // After pass - expect no optimization for non-power-of-2
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(StrengthReductionPassTest, ModuloByNegativePowerOfTwo) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func =
        Function::Create(funcType, "test_mod_neg_power2", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* negEight = ConstantInt::get(i32Type, -8);
    auto* rem = builder->createRem(param, negEight);
    builder->createRet(rem);

    // Before pass - expect original modulo
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mod_neg_power2(i32 %arg0) {
entry:
  %0 = srem i32 %arg0, -8
  ret i32 %0
}
)");

    StrengthReductionPass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect bitwise AND optimization (same as positive case)
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mod_neg_power2(i32 %arg0) {
entry:
  %0 = and i32 %arg0, 7
  ret i32 %0
}
)");
}
