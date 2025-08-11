#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "Pass/Analysis/DominanceInfo.h"
#include "Pass/Pass.h"
#include "Pass/Transform/ComptimePass.h"

using namespace midend;

class ComptimeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();
        am->registerAnalysisType<DominanceAnalysis>();
        am->registerAnalysisType<PostDominanceAnalysis>();
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

// Test 1: Basic arithmetic operations with integer constants
TEST_F(ComptimeTest, BasicIntegerArithmetic) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create compile-time arithmetic: 10 + 5 - 3 * 2 / 1
    auto val1 = builder->getInt32(10);
    auto val2 = builder->getInt32(5);
    auto val3 = builder->getInt32(3);
    auto val4 = builder->getInt32(2);
    auto val5 = builder->getInt32(1);

    auto add1 = builder->createAdd(val1, val2, "add1");
    auto sub1 = builder->createSub(add1, val3, "sub1");
    auto mul1 = builder->createMul(sub1, val4, "mul1");
    auto div1 = builder->createDiv(mul1, val5, "div1");

    builder->createRet(div1);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @main() {
entry:
  %add1 = add i32 10, 5
  %sub1 = sub i32 %add1, 3
  %mul1 = mul i32 %sub1, 2
  %div1 = sdiv i32 %mul1, 1
  ret i32 %div1
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - expect constant folding: (10 + 5 - 3) * 2 / 1 = 24
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @main() {
entry:
  ret i32 24
}
)");
}

// Test 2: Basic arithmetic operations with float constants
TEST_F(ComptimeTest, BasicFloatArithmetic) {
    auto floatType = ctx->getFloatType();
    auto funcType = FunctionType::get(floatType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create compile-time arithmetic: 5.5 + 2.5 - 1.0
    auto val1 = builder->getFloat(5.5f);
    auto val2 = builder->getFloat(2.5f);
    auto val3 = builder->getFloat(1.0f);

    auto add1 = builder->createFAdd(val1, val2, "add1");
    auto sub1 = builder->createFSub(add1, val3, "sub1");

    builder->createRet(sub1);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define float @main() {
entry:
  %add1 = fadd float 5.500000e+00, 2.500000e+00
  %sub1 = fsub float %add1, 1.000000e+00
  ret float %sub1
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - expect constant folding: 5.5 + 2.5 - 1.0 = 7.0
    EXPECT_EQ(IRPrinter().print(func),
              R"(define float @main() {
entry:
  ret float 7.000000e+00
}
)");
}

// Test 3: Remainder operation with division by zero protection
TEST_F(ComptimeTest, RemainderOperationWithDivisionByZero) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Test valid remainder: 17 % 5
    auto val1 = builder->getInt32(17);
    auto val2 = builder->getInt32(5);
    auto rem1 = builder->createRem(val1, val2, "rem1");

    // Test division by zero protection: 10 % 0 (should not be optimized)
    auto val3 = builder->getInt32(10);
    auto val4 = builder->getInt32(0);
    auto rem2 = builder->createRem(val3, val4, "rem2");

    auto add_result = builder->createAdd(rem1, rem2, "result");
    builder->createRet(add_result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @main() {
entry:
  %rem1 = srem i32 17, 5
  %rem2 = srem i32 10, 0
  %result = add i32 %rem1, %rem2
  ret i32 %result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - expect only rem1 to be folded (17 % 5 = 2), rem2 unchanged
    // for safety
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @main() {
entry:
  %rem2 = srem i32 10, 0
  %result = add i32 2, %rem2
  ret i32 %result
}
)");
}

// Test 4: Unary operations (negation, logical not)
TEST_F(ComptimeTest, UnaryOperations) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Test unary operations
    auto val1 = builder->getInt32(42);
    auto val2 = builder->getInt32(0);

    auto neg1 = UnaryOperator::CreateUSub(val1, "neg1", entryBB);
    auto not1 = UnaryOperator::CreateNot(val2, "not1", entryBB);
    auto pos1 = UnaryOperator::CreateUAdd(val1, "pos1", entryBB);

    auto result = builder->createAdd(neg1, not1, "temp");
    result = builder->createAdd(result, pos1, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @main() {
entry:
  %neg1 = -42
  %not1 = !0
  %pos1 = +42
  %temp = add i32 %neg1, %not1
  %result = add i32 %temp, %pos1
  ret i32 %result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - expect constant folding: -42 + !0 + +42 = -42 + 1 + 42 = 1
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @main() {
entry:
  ret i32 1
}
)");
}

// Test 5: Comparison operations for integers
TEST_F(ComptimeTest, IntegerComparisons) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto val1 = builder->getInt32(10);
    auto val2 = builder->getInt32(20);

    auto cmp_eq =
        builder->createICmpEQ(val1, val1, "cmp_eq");  // 10 == 10 -> true
    auto cmp_ne =
        builder->createICmpNE(val1, val2, "cmp_ne");  // 10 != 20 -> true
    auto cmp_lt =
        builder->createICmpSLT(val1, val2, "cmp_lt");  // 10 < 20 -> true
    auto cmp_le =
        builder->createICmpSLE(val1, val2, "cmp_le");  // 10 <= 20 -> true
    auto cmp_gt =
        builder->createICmpSGT(val2, val1, "cmp_gt");  // 20 > 10 -> true
    auto cmp_ge =
        builder->createICmpSGE(val2, val1, "cmp_ge");  // 20 >= 10 -> true

    // Convert booleans to integers and add them (all true should sum to 6)
    auto ext1 =
        CastInst::Create(CastInst::ZExt, cmp_eq, intType, "ext1", entryBB);
    auto ext2 =
        CastInst::Create(CastInst::ZExt, cmp_ne, intType, "ext2", entryBB);
    auto ext3 =
        CastInst::Create(CastInst::ZExt, cmp_lt, intType, "ext3", entryBB);
    auto ext4 =
        CastInst::Create(CastInst::ZExt, cmp_le, intType, "ext4", entryBB);
    auto ext5 =
        CastInst::Create(CastInst::ZExt, cmp_gt, intType, "ext5", entryBB);
    auto ext6 =
        CastInst::Create(CastInst::ZExt, cmp_ge, intType, "ext6", entryBB);

    auto sum1 = builder->createAdd(ext1, ext2, "sum1");
    auto sum2 = builder->createAdd(sum1, ext3, "sum2");
    auto sum3 = builder->createAdd(sum2, ext4, "sum3");
    auto sum4 = builder->createAdd(sum3, ext5, "sum4");
    auto result = builder->createAdd(sum4, ext6, "result");

    builder->createRet(result);

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - all comparisons should evaluate to true (1), so result
    // should be 6
    auto resultIR = IRPrinter().print(func);
    EXPECT_TRUE(resultIR.find("ret i32 6") != std::string::npos);
}

// Test 6: Float comparisons
TEST_F(ComptimeTest, FloatComparisons) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto val1 = builder->getFloat(3.14f);
    auto val2 = builder->getFloat(2.71f);

    auto cmp_feq =
        builder->createFCmpOEQ(val1, val1, "cmp_feq");  // 3.14 == 3.14 -> true
    auto cmp_fne =
        builder->createFCmpONE(val1, val2, "cmp_fne");  // 3.14 != 2.71 -> true
    auto cmp_flt =
        builder->createFCmpOLT(val2, val1, "cmp_flt");  // 2.71 < 3.14 -> true
    auto cmp_fle =
        builder->createFCmpOLE(val2, val1, "cmp_fle");  // 2.71 <= 3.14 -> true
    auto cmp_fgt =
        builder->createFCmpOGT(val1, val2, "cmp_fgt");  // 3.14 > 2.71 -> true
    auto cmp_fge =
        builder->createFCmpOGE(val1, val2, "cmp_fge");  // 3.14 >= 2.71 -> true

    // Convert booleans to integers and add them
    auto ext1 =
        CastInst::Create(CastInst::ZExt, cmp_feq, intType, "ext1", entryBB);
    auto ext2 =
        CastInst::Create(CastInst::ZExt, cmp_fne, intType, "ext2", entryBB);
    auto ext3 =
        CastInst::Create(CastInst::ZExt, cmp_flt, intType, "ext3", entryBB);
    auto ext4 =
        CastInst::Create(CastInst::ZExt, cmp_fle, intType, "ext4", entryBB);
    auto ext5 =
        CastInst::Create(CastInst::ZExt, cmp_fgt, intType, "ext5", entryBB);
    auto ext6 =
        CastInst::Create(CastInst::ZExt, cmp_fge, intType, "ext6", entryBB);

    auto sum1 = builder->createAdd(ext1, ext2, "sum1");
    auto sum2 = builder->createAdd(sum1, ext3, "sum2");
    auto sum3 = builder->createAdd(sum2, ext4, "sum3");
    auto sum4 = builder->createAdd(sum3, ext5, "sum4");
    auto result = builder->createAdd(sum4, ext6, "result");

    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %cmp_feq = fcmp oeq float 3.140000e+00, 3.140000e+00
  %cmp_fne = fcmp one float 3.140000e+00, 2.710000e+00
  %cmp_flt = fcmp olt float 2.710000e+00, 3.140000e+00
  %cmp_fle = fcmp ole float 2.710000e+00, 3.140000e+00
  %cmp_fgt = fcmp ogt float 3.140000e+00, 2.710000e+00
  %cmp_fge = fcmp oge float 3.140000e+00, 2.710000e+00
  %ext1 = zext i1 %cmp_feq to i32
  %ext2 = zext i1 %cmp_fne to i32
  %ext3 = zext i1 %cmp_flt to i32
  %ext4 = zext i1 %cmp_fle to i32
  %ext5 = zext i1 %cmp_fgt to i32
  %ext6 = zext i1 %cmp_fge to i32
  %sum1 = add i32 %ext1, %ext2
  %sum2 = add i32 %sum1, %ext3
  %sum3 = add i32 %sum2, %ext4
  %sum4 = add i32 %sum3, %ext5
  %result = add i32 %sum4, %ext6
  ret i32 %result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    std::cout << IRPrinter().print(func) << std::endl;
    // After pass - all comparisons should evaluate to true (1), so result
    // should be 6
    auto resultIR = IRPrinter().print(func);
    EXPECT_TRUE(resultIR.find("ret i32 6") != std::string::npos);
}

// Test 7: Control flow with compile-time conditional branches
TEST_F(ComptimeTest, CompileTimeConditionalBranches) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_branch", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_branch", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    // Entry block - compile-time condition
    builder->setInsertPoint(entryBB);
    auto val1 = builder->getInt32(10);
    auto val2 = builder->getInt32(5);
    auto cond =
        builder->createICmpSGT(val1, val2, "cond");  // 10 > 5 -> always true
    builder->createCondBr(cond, trueBB, falseBB);

    // True branch
    builder->setInsertPoint(trueBB);
    auto true_result = builder->getInt32(100);
    builder->createBr(mergeBB);

    // False branch
    builder->setInsertPoint(falseBB);
    auto false_result = builder->getInt32(200);
    builder->createBr(mergeBB);

    // Merge block
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(true_result, trueBB);
    phi->addIncoming(false_result, falseBB);
    builder->createRet(phi);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %cond = icmp sgt i32 10, 5
  br i1 %cond, label %true_branch, label %false_branch
true_branch:
  br label %merge
false_branch:
  br label %merge
merge:
  %phi = phi i32 [ 100, %true_branch ], [ 200, %false_branch ]
  ret i32 %phi
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  br i1 1, label %true_branch, label %false_branch
true_branch:
  br label %merge
false_branch:
  br label %merge
merge:
  ret i32 100
}
)");

    // After pass - should take true branch only, result should be 100
    auto resultIR = IRPrinter().print(func);
    EXPECT_TRUE(resultIR.find("ret i32 100") != std::string::npos);
}

// Test 8: PHI nodes with compile-time values
TEST_F(ComptimeTest, PHINodesWithCompileTimeValues) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loop1BB = BasicBlock::Create(ctx.get(), "loop1", func);
    auto loop2BB = BasicBlock::Create(ctx.get(), "loop2", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    // Entry block
    builder->setInsertPoint(entryBB);
    auto const5 = builder->getInt32(5);
    auto const10 = builder->getInt32(10);
    auto cmp1 = builder->createICmpSGT(builder->getInt32(6), const5, "cmp1");
    builder->createCondBr(cmp1, loop1BB, loop2BB);

    // Loop1 block
    builder->setInsertPoint(loop1BB);
    auto val1 = builder->createAdd(const10, const5, "val1");  // 15
    builder->createBr(exitBB);

    // Loop2 block
    builder->setInsertPoint(loop2BB);
    auto val2 = builder->createMul(const10, const5, "val2");  // 50
    builder->createBr(exitBB);

    // Exit block with PHI
    builder->setInsertPoint(exitBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(val1, loop1BB);
    phi->addIncoming(val2, loop2BB);

    auto result = builder->createAdd(phi, const5, "result");
    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %cmp1 = icmp sgt i32 6, 5
  br i1 %cmp1, label %loop1, label %loop2
loop1:
  %val1 = add i32 10, 5
  br label %exit
loop2:
  %val2 = mul i32 10, 5
  br label %exit
exit:
  %phi = phi i32 [ %val1, %loop1 ], [ %val2, %loop2 ]
  %result = add i32 %phi, 5
  ret i32 %result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  br i1 1, label %loop1, label %loop2
loop1:
  br label %exit
loop2:
  %val2 = mul i32 10, 5
  br label %exit
exit:
  ret i32 20
}
)");
}

// Test 9: Array operations - zero initialization and GEP
TEST_F(ComptimeTest, ArrayOperations) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ctx->getArrayType(intType, 5);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Allocate array
    auto arr = builder->createAlloca(arrayType, nullptr, "arr");

    // Store some compile-time values
    auto idx0 = builder->getInt32(0);
    auto idx1 = builder->getInt32(1);
    auto idx2 = builder->getInt32(2);

    auto gep0 = builder->createGEP(arr, idx0, "gep0");
    auto gep1 = builder->createGEP(arr, idx1, "gep1");
    auto gep2 = builder->createGEP(arr, idx2, "gep2");

    auto val1 = builder->getInt32(10);
    auto val2 = builder->getInt32(20);
    auto val3 = builder->getInt32(30);

    builder->createStore(val1, gep0);
    builder->createStore(val2, gep1);
    builder->createStore(val3, gep2);

    // Load and sum
    auto load0 = builder->createLoad(gep0, "load0");
    auto load1 = builder->createLoad(gep1, "load1");
    auto load2 = builder->createLoad(gep2, "load2");

    auto sum1 = builder->createAdd(load0, load1, "sum1");
    auto result = builder->createAdd(sum1, load2, "result");

    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %arr = alloca [5 x i32]
  %gep0 = getelementptr [5 x i32], [5 x i32]* %arr, i32 0
  %gep1 = getelementptr [5 x i32], [5 x i32]* %arr, i32 1
  %gep2 = getelementptr [5 x i32], [5 x i32]* %arr, i32 2
  store i32 10, i32* %gep0
  store i32 20, i32* %gep1
  store i32 30, i32* %gep2
  %load0 = load i32, i32* %gep0
  %load1 = load i32, i32* %gep1
  %load2 = load i32, i32* %gep2
  %sum1 = add i32 %load0, %load1
  %result = add i32 %sum1, %load2
  ret i32 %result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %arr = alloca [5 x i32]
  %0 = getelementptr [5 x i32], [5 x i32]* %arr, i32 0
  store i32 10, i32* %0
  %1 = getelementptr [5 x i32], [5 x i32]* %arr, i32 1
  store i32 20, i32* %1
  %2 = getelementptr [5 x i32], [5 x i32]* %arr, i32 2
  store i32 30, i32* %2
  %3 = getelementptr [5 x i32], [5 x i32]* %arr, i32 3
  store i32 0, i32* %3
  %4 = getelementptr [5 x i32], [5 x i32]* %arr, i32 4
  store i32 0, i32* %4
  %gep0 = getelementptr [5 x i32], [5 x i32]* %arr, i32 0
  %gep1 = getelementptr [5 x i32], [5 x i32]* %arr, i32 1
  %gep2 = getelementptr [5 x i32], [5 x i32]* %arr, i32 2
  ret i32 60
}
)");
}

// Test 9.2: Array operations 2
TEST_F(ComptimeTest, ArrayOperations2) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ctx->getArrayType(intType, 15);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Allocate array
    auto arr = builder->createAlloca(arrayType, nullptr, "arr");

    // Store some compile-time values
    auto idx0 = builder->getInt32(0);
    auto idx1 = builder->getInt32(1);
    auto idx2 = builder->getInt32(2);

    auto gep0 = builder->createGEP(arr, idx0, "gep0");
    auto gep1 = builder->createGEP(arr, idx1, "gep1");
    auto gep2 = builder->createGEP(arr, idx2, "gep2");

    auto val1 = builder->getInt32(10);
    auto val2 = builder->getInt32(20);
    auto val3 = builder->getInt32(30);

    builder->createStore(val1, gep0);
    builder->createStore(val2, gep1);
    builder->createStore(val3, gep2);

    // Load and sum
    auto load0 = builder->createLoad(gep0, "load0");
    auto load1 = builder->createLoad(gep1, "load1");
    auto load2 = builder->createLoad(gep2, "load2");

    auto sum1 = builder->createAdd(load0, load1, "sum1");
    auto result = builder->createAdd(sum1, load2, "result");

    builder->createRet(result);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %arr = alloca [15 x i32]
  %gep0 = getelementptr [15 x i32], [15 x i32]* %arr, i32 0
  %gep1 = getelementptr [15 x i32], [15 x i32]* %arr, i32 1
  %gep2 = getelementptr [15 x i32], [15 x i32]* %arr, i32 2
  store i32 10, i32* %gep0
  store i32 20, i32* %gep1
  store i32 30, i32* %gep2
  %load0 = load i32, i32* %gep0
  %load1 = load i32, i32* %gep1
  %load2 = load i32, i32* %gep2
  %sum1 = add i32 %load0, %load1
  %result = add i32 %sum1, %load2
  ret i32 %result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %arr = alloca [15 x i32]
  br label %comptime.array.cond.0
comptime.array.cond.0:
  %comptime.array.i.0 = phi i32 [ 0, %entry ], [ %0, %comptime.array.body.0 ]
  %1 = icmp slt i32 %comptime.array.i.0, 15
  br i1 %1, label %comptime.array.body.0, label %entry.split
comptime.array.body.0:
  %2 = getelementptr [15 x i32], [15 x i32]* %arr, i32 %comptime.array.i.0
  store i32 0, i32* %2
  %0 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split:
  %3 = getelementptr [15 x i32]*, [15 x i32]* %arr, i32 0
  store i32 10, [15 x i32]* %3
  %4 = getelementptr [15 x i32]*, [15 x i32]* %arr, i32 1
  store i32 20, [15 x i32]* %4
  %5 = getelementptr [15 x i32]*, [15 x i32]* %arr, i32 2
  store i32 30, [15 x i32]* %5
  %gep0 = getelementptr [15 x i32], [15 x i32]* %arr, i32 0
  %gep1 = getelementptr [15 x i32], [15 x i32]* %arr, i32 1
  %gep2 = getelementptr [15 x i32], [15 x i32]* %arr, i32 2
  ret i32 60
}
)");
}

// TODO: 多个局部数组: 一维数组 + 三维数组
// TODO: 全局数组
// TODO: 数组作为参数传入函数

// Test 10: Function calls with compile-time arguments
TEST_F(ComptimeTest, FunctionCallsWithCompileTimeArgs) {
    auto intType = ctx->getIntegerType(32);

    // Create a helper function: square(x) = x * x
    auto helperFuncType = FunctionType::get(intType, {intType});
    auto helperFunc = Function::Create(helperFuncType, "square", module.get());
    auto helperBB = BasicBlock::Create(ctx.get(), "entry", helperFunc);
    builder->setInsertPoint(helperBB);
    auto param = helperFunc->getArg(0);
    auto squared = builder->createMul(param, param, "squared");
    builder->createRet(squared);

    // Create main function
    auto mainFuncType = FunctionType::get(intType, {});
    auto mainFunc = Function::Create(mainFuncType, "main", module.get());
    auto mainBB = BasicBlock::Create(ctx.get(), "entry", mainFunc);
    builder->setInsertPoint(mainBB);

    // Call helper with compile-time argument
    auto arg = builder->getInt32(7);
    auto call_result = builder->createCall(helperFunc, {arg}, "call_result");
    builder->createRet(call_result);

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - should evaluate function call: square(7) = 49
    auto resultIR = IRPrinter().print(mainFunc);
    EXPECT_TRUE(resultIR.find("ret i32 49") != std::string::npos);
}

// Test 11: Global variables with compile-time initialization
TEST_F(ComptimeTest, GlobalVariablesCompileTime) {
    auto intType = ctx->getIntegerType(32);

    // Create global variable with initializer
    auto globalVar = GlobalVariable::Create(
        intType, false, GlobalVariable::InternalLinkage, builder->getInt32(42),
        "global_var", module.get());

    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Load global variable and use in computation
    auto loaded = builder->createLoad(globalVar, "loaded");
    auto const10 = builder->getInt32(10);
    auto result = builder->createAdd(loaded, const10, "result");
    builder->createRet(result);

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - should compute: 42 + 10 = 52
    auto resultIR = IRPrinter().print(func);
    EXPECT_TRUE(resultIR.find("ret i32 52") != std::string::npos);
}

// Test 12: Non-compile-time branches should remain unchanged
TEST_F(ComptimeTest, NonCompileTimeBranches) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto trueBB = BasicBlock::Create(ctx.get(), "true_branch", func);
    auto falseBB = BasicBlock::Create(ctx.get(), "false_branch", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    auto n = builder->getInt32(5);

    // Entry block - runtime condition
    builder->setInsertPoint(entryBB);
    auto zero = builder->getInt32(0);
    auto cond = builder->createICmpSGT(n, zero,
                                       "cond");  // arg > 0 -> runtime condition
    builder->createCondBr(cond, trueBB, falseBB);

    // True branch
    builder->setInsertPoint(trueBB);
    auto const10 = builder->getInt32(10);
    auto const5 = builder->getInt32(5);
    auto true_val = builder->createAdd(const10, const5, "true_val");  // 15
    builder->createBr(mergeBB);

    // False branch
    builder->setInsertPoint(falseBB);
    auto const20 = builder->getInt32(20);
    auto const3 = builder->getInt32(3);
    auto false_val = builder->createMul(const20, const3, "false_val");  // 60
    builder->createBr(mergeBB);

    // Merge block
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "phi");
    phi->addIncoming(true_val, trueBB);
    phi->addIncoming(false_val, falseBB);
    builder->createRet(phi);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %cond = icmp sgt i32 5, 0
  br i1 %cond, label %true_branch, label %false_branch
true_branch:
  %true_val = add i32 10, 5
  br label %merge
false_branch:
  %false_val = mul i32 20, 3
  br label %merge
merge:
  %phi = phi i32 [ %true_val, %true_branch ], [ %false_val, %false_branch ]
  ret i32 %phi
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    auto resultIR = IRPrinter().print(func);
    EXPECT_TRUE(resultIR.find("ret i32 15") != std::string::npos);
}

// Test 13: Cast instructions with compile-time values
TEST_F(ComptimeTest, CastInstructionsCompileTime) {
    auto intType = ctx->getIntegerType(32);
    auto floatType = ctx->getFloatType();
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Integer to float and back
    auto intVal = builder->getInt32(42);
    auto toFloat =
        builder->createCast(CastInst::SIToFP, intVal, floatType, "to_float");
    auto backToInt =
        builder->createCast(CastInst::FPToSI, toFloat, intType, "back_to_int");

    // Float to integer
    auto floatVal = builder->getFloat(3.7f);
    auto floatToInt = builder->createCast(CastInst::FPToSI, floatVal, intType,
                                          "float_to_int");

    auto result = builder->createAdd(backToInt, floatToInt, "result");
    builder->createRet(result);

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - should compute: 42 + 3 = 45 (3.7 truncated to 3)
    auto resultIR = IRPrinter().print(func);
    EXPECT_TRUE(resultIR.find("ret i32 45") != std::string::npos);
}