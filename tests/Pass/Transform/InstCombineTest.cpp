#include <gtest/gtest.h>

#include <memory>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"
#include "Pass/Transform/InstCombinePass.h"

using namespace midend;

class InstCombineTest : public ::testing::Test {
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

TEST_F(InstCombineTest, AddZeroIdentity) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_add_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* add = builder->createAdd(param, zero);
    builder->createRet(add);

    // Before pass - expect original addition
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_add_zero(i32 %arg0) {
entry:
  %0 = add i32 %arg0, 0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect identity optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_add_zero(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, AddConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {});
    auto* func = Function::Create(funcType, "test_add_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* lhs = ConstantInt::get(i32Type, 15);
    auto* rhs = ConstantInt::get(i32Type, 27);
    auto* add = builder->createAdd(lhs, rhs);
    builder->createRet(add);

    // Before pass - expect original addition
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_add_const() {
entry:
  %0 = add i32 15, 27
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_add_const() {
entry:
  ret i32 42
}
)");
}

TEST_F(InstCombineTest, SubZeroIdentity) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_sub_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* sub = builder->createSub(param, zero);
    builder->createRet(sub);

    // Before pass - expect original subtraction
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_sub_zero(i32 %arg0) {
entry:
  %0 = sub i32 %arg0, 0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect identity optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_sub_zero(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, SubSelfToZero) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_sub_self", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* sub = builder->createSub(param, param);
    builder->createRet(sub);

    // Before pass - expect original subtraction
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_sub_self(i32 %arg0) {
entry:
  %0 = sub i32 %arg0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to zero
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_sub_self(i32 %arg0) {
entry:
  ret i32 0
}
)");
}

TEST_F(InstCombineTest, MulOneIdentity) {
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

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect identity optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_one(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, MulZeroAbsorption) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_mul_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* mul = builder->createMul(zero, param);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_zero(i32 %arg0) {
entry:
  %0 = mul i32 0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect zero absorption
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_zero(i32 %arg0) {
entry:
  ret i32 0
}
)");
}

TEST_F(InstCombineTest, MulConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {});
    auto* func = Function::Create(funcType, "test_mul_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* lhs = ConstantInt::get(i32Type, 6);
    auto* rhs = ConstantInt::get(i32Type, 7);
    auto* mul = builder->createMul(lhs, rhs);
    builder->createRet(mul);

    // Before pass - expect original multiplication
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_const() {
entry:
  %0 = mul i32 6, 7
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_mul_const() {
entry:
  ret i32 42
}
)");
}

TEST_F(InstCombineTest, DivOneIdentity) {
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

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect identity optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_one(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, DivSelfToOne) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_div_self", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* div = builder->createDiv(param, param);
    builder->createRet(div);

    // Before pass - expect original division
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_self(i32 %arg0) {
entry:
  %0 = sdiv i32 %arg0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to one
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_div_self(i32 %arg0) {
entry:
  ret i32 1
}
)");
}

TEST_F(InstCombineTest, RemSelfToZero) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_rem_self", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* rem = builder->createRem(param, param);
    builder->createRet(rem);

    // Before pass - expect original remainder
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_rem_self(i32 %arg0) {
entry:
  %0 = srem i32 %arg0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to zero
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_rem_self(i32 %arg0) {
entry:
  ret i32 0
}
)");
}

TEST_F(InstCombineTest, AndZeroAbsorption) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_and_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* andOp = builder->createAnd(param, zero);
    builder->createRet(andOp);

    // Before pass - expect original AND
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_and_zero(i32 %arg0) {
entry:
  %0 = and i32 %arg0, 0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect zero absorption
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_and_zero(i32 %arg0) {
entry:
  ret i32 0
}
)");
}

TEST_F(InstCombineTest, AndSelfIdempotent) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_and_self", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* andOp = builder->createAnd(param, param);
    builder->createRet(andOp);

    // Before pass - expect original AND
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_and_self(i32 %arg0) {
entry:
  %0 = and i32 %arg0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect idempotent optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_and_self(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, OrZeroIdentity) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_or_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* orOp = builder->createOr(zero, param);
    builder->createRet(orOp);

    // Before pass - expect original OR
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_or_zero(i32 %arg0) {
entry:
  %0 = or i32 0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect identity optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_or_zero(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, XorZeroIdentity) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_xor_zero", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* xorOp = builder->createXor(param, zero);
    builder->createRet(xorOp);

    // Before pass - expect original XOR
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_xor_zero(i32 %arg0) {
entry:
  %0 = xor i32 %arg0, 0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect identity optimization
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_xor_zero(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, XorSelfToZero) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_xor_self", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* xorOp = builder->createXor(param, param);
    builder->createRet(xorOp);

    // Before pass - expect original XOR
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_xor_self(i32 %arg0) {
entry:
  %0 = xor i32 %arg0, %arg0
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to zero
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_xor_self(i32 %arg0) {
entry:
  ret i32 0
}
)");
}

TEST_F(InstCombineTest, UnaryAddElimination) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_uadd", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* uadd = UnaryOperator::CreateUAdd(param, "uadd", entry);
    builder->createRet(uadd);

    // Before pass - expect original unary add
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_uadd(i32 %arg0) {
entry:
  %uadd = +%arg0
  ret i32 %uadd
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect unary add elimination
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_uadd(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, UnarySubConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {});
    auto* func = Function::Create(funcType, "test_usub_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* val = ConstantInt::get(i32Type, 42);
    auto* usub = UnaryOperator::CreateUSub(val, "usub", entry);
    builder->createRet(usub);

    // Before pass - expect original unary subtract
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_usub_const() {
entry:
  %usub = -42
  ret i32 %usub
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_usub_const() {
entry:
  ret i32 -42
}
)");
}

TEST_F(InstCombineTest, NotConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {});
    auto* func = Function::Create(funcType, "test_not_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* val = ConstantInt::get(i32Type, 0xAAAAAAAA);
    auto* notOp = UnaryOperator::CreateNot(val, "not", entry);
    builder->createRet(notOp);

    // Before pass - expect original NOT
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_not_const() {
entry:
  %not = !-1431655766
  ret i1 %not
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding (~0xAAAAAAAA = -2863311531)
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_not_const() {
entry:
  ret i32 1431655765
}
)");
}

TEST_F(InstCombineTest, CompareConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* boolType = IntegerType::get(ctx.get(), 1);
    auto* funcType = FunctionType::get(boolType, {});
    auto* func = Function::Create(funcType, "test_cmp_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* lhs = ConstantInt::get(i32Type, 15);
    auto* rhs = ConstantInt::get(i32Type, 10);
    auto* cmp = builder->createICmpSGT(lhs, rhs);
    builder->createRet(cmp);

    // Before pass - expect original comparison
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i1 @test_cmp_const() {
entry:
  %0 = icmp sgt i32 15, 10
  ret i1 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding (15 > 10 = true)
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i1 @test_cmp_const() {
entry:
  ret i1 1
}
)");
}

TEST_F(InstCombineTest, CompareSelfToTrue) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* boolType = IntegerType::get(ctx.get(), 1);
    auto* funcType = FunctionType::get(boolType, {i32Type});
    auto* func = Function::Create(funcType, "test_cmp_self", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* cmp = builder->createICmpEQ(param, param);
    builder->createRet(cmp);

    // Before pass - expect original comparison
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i1 @test_cmp_self(i32 %arg0) {
entry:
  %0 = icmp eq i32 %arg0, %arg0
  ret i1 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to true
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i1 @test_cmp_self(i32 %arg0) {
entry:
  ret i1 1
}
)");
}

TEST_F(InstCombineTest, CompareSelfToFalse) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* boolType = IntegerType::get(ctx.get(), 1);
    auto* funcType = FunctionType::get(boolType, {i32Type});
    auto* func =
        Function::Create(funcType, "test_cmp_self_false", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* cmp = builder->createICmpNE(param, param);
    builder->createRet(cmp);

    // Before pass - expect original comparison
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i1 @test_cmp_self_false(i32 %arg0) {
entry:
  %0 = icmp ne i32 %arg0, %arg0
  ret i1 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect optimization to false
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i1 @test_cmp_self_false(i32 %arg0) {
entry:
  ret i1 0
}
)");
}

TEST_F(InstCombineTest, NoOptimizationNeeded) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type, i32Type});
    auto* func = Function::Create(funcType, "test_no_opt", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param1 = func->getArg(0);
    auto* param2 = func->getArg(1);
    auto* add = builder->createAdd(param1, param2);
    builder->createRet(add);

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR,
              R"(define i32 @test_no_opt(i32 %arg0, i32 %arg1) {
entry:
  %0 = add i32 %arg0, %arg1
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_FALSE(changed);

    // After pass - expect no changes
    EXPECT_EQ(IRPrinter().print(func), beforeIR);
}

TEST_F(InstCombineTest, MultipleOptimizations) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});
    auto* func = Function::Create(funcType, "test_multiple", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* param = func->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* one = ConstantInt::get(i32Type, 1);

    // Chain of operations: ((x + 0) * 1) - 0
    auto* add = builder->createAdd(param, zero);
    auto* mul = builder->createMul(add, one);
    auto* sub = builder->createSub(mul, zero);
    builder->createRet(sub);

    // Before pass - expect original operations
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_multiple(i32 %arg0) {
entry:
  %0 = add i32 %arg0, 0
  %1 = mul i32 %0, 1
  %2 = sub i32 %1, 0
  ret i32 %2
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect all optimizations to reduce to just the parameter
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_multiple(i32 %arg0) {
entry:
  ret i32 %arg0
}
)");
}

TEST_F(InstCombineTest, BitwiseConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {});
    auto* func = Function::Create(funcType, "test_bitwise_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* lhs = ConstantInt::get(i32Type, 0xF0F0F0F0);
    auto* rhs = ConstantInt::get(i32Type, 0x0F0F0F0F);
    auto* andOp = builder->createAnd(lhs, rhs);
    auto* orOp = builder->createOr(lhs, rhs);
    auto* xorOp = builder->createXor(andOp, orOp);
    builder->createRet(xorOp);

    // Before pass - expect original bitwise operations
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_bitwise_const() {
entry:
  %0 = and i32 -252645136, 252645135
  %1 = or i32 -252645136, 252645135
  %2 = xor i32 %0, %1
  ret i32 %2
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding
    // 0xF0F0F0F0 & 0x0F0F0F0F = 0x00000000 = 0
    // 0xF0F0F0F0 | 0x0F0F0F0F = 0xFFFFFFFF = 4294967295
    // 0x00000000 ^ 0xFFFFFFFF = 0xFFFFFFFF = 4294967295
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_bitwise_const() {
entry:
  ret i32 -1
}
)");
}

TEST_F(InstCombineTest, ArithmeticConstantFolding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* funcType = FunctionType::get(i32Type, {});
    auto* func = Function::Create(funcType, "test_arith_const", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* val1 = ConstantInt::get(i32Type, 100);
    auto* val2 = ConstantInt::get(i32Type, 25);
    auto* val3 = ConstantInt::get(i32Type, 5);

    auto* sub = builder->createSub(val1, val2);  // 100 - 25 = 75
    auto* div = builder->createDiv(sub, val3);   // 75 / 5 = 15
    auto* rem = builder->createRem(div, val2);   // 15 % 25 = 15
    builder->createRet(rem);

    // Before pass - expect original arithmetic operations
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_arith_const() {
entry:
  %0 = sub i32 100, 25
  %1 = sdiv i32 %0, 5
  %2 = srem i32 %1, 25
  ret i32 %2
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect constant folding
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_arith_const() {
entry:
  ret i32 15
}
)");
}