#include <gtest/gtest.h>

#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/BinaryOps.h"
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

    Function* getRuntimeFunction() {
        auto retType = builder->getInt32Type();
        auto funcType = FunctionType::get(retType, {});
        auto func = Function::Create(funcType, "runtimeFunc", module.get());
        return func;
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
    // EXPECT_TRUE(resultIR.find("ret i32 6") != std::string::npos);
    std::cout << IRPrinter().print(func) << std::endl;
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
  %3 = getelementptr [15 x i32], [15 x i32]* %arr, i32 0
  store i32 10, i32* %3
  %4 = getelementptr [15 x i32], [15 x i32]* %arr, i32 1
  store i32 20, i32* %4
  %5 = getelementptr [15 x i32], [15 x i32]* %arr, i32 2
  store i32 30, i32* %5
  %gep0 = getelementptr [15 x i32], [15 x i32]* %arr, i32 0
  %gep1 = getelementptr [15 x i32], [15 x i32]* %arr, i32 1
  %gep2 = getelementptr [15 x i32], [15 x i32]* %arr, i32 2
  ret i32 60
}
)");
}

// Test 9.3: Multiple local arrays - 1D array a[11] + 3D array b[2][2][3]
TEST_F(ComptimeTest, MultipleLocalArrays) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create 1D array a[11]
    auto arrayType1D = ArrayType::get(intType, 11);
    auto arr1D = builder->createAlloca(arrayType1D, nullptr, "a");
    // Create 3D array b[2][2][3]
    auto innerArrayType = ArrayType::get(intType, 3);
    auto middleArrayType = ArrayType::get(innerArrayType, 2);
    auto arrayType3D = ArrayType::get(middleArrayType, 2);
    auto arr3D = builder->createAlloca(arrayType3D, nullptr, "b");

    // Initialize 1D array with values: a[i] = i * 2
    for (int i = 0; i < 2; i++) {
        auto idx = builder->getInt32(i);
        auto gep = builder->createGEP(arr1D, idx);
        builder->createStore(builder->getInt32(i * 2), gep);
    }

    // Initialize 3D array with values: b[i][j][k] = i*100 + j*10 + k
    for (int i = 1; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 2; k < 3; k++) {
                auto idx_i = builder->getInt32(i);
                auto idx_j = builder->getInt32(j);
                auto idx_k = builder->getInt32(k);
                auto gep = builder->createGEP(arrayType3D, arr3D,
                                              {idx_i, idx_j, idx_k});
                builder->createStore(builder->getInt32(i * 100 + j * 10 + k),
                                     gep);
            }
        }
    }

    // Sum some elements: a[5] + b[1][1][2]
    auto gep_a5 =
        builder->createGEP(arrayType1D, arr1D, {builder->getInt32(1)});
    auto load_a5 = builder->createLoad(gep_a5, "a1");

    auto gep_b112 = builder->createGEP(
        arrayType3D, arr3D,
        {builder->getInt32(1), builder->getInt32(1), builder->getInt32(2)});
    auto load_b112 = builder->createLoad(gep_b112, "b112");

    auto sum = builder->createAdd(load_a5, load_b112, "sum");
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %a = alloca [11 x i32]
  %b = alloca [2 x [2 x [3 x i32]]]
  %0 = getelementptr [11 x i32], [11 x i32]* %a, i32 0
  store i32 0, i32* %0
  %1 = getelementptr [11 x i32], [11 x i32]* %a, i32 1
  store i32 2, i32* %1
  %2 = getelementptr [2 x [2 x [3 x i32]]], [2 x [2 x [3 x i32]]]* %b, i32 1, i32 0, i32 2
  store i32 102, i32* %2
  %3 = getelementptr [2 x [2 x [3 x i32]]], [2 x [2 x [3 x i32]]]* %b, i32 1, i32 1, i32 2
  store i32 112, i32* %3
  %4 = getelementptr [11 x i32], [11 x i32]* %a, i32 1
  %a1 = load i32, i32* %4
  %5 = getelementptr [2 x [2 x [3 x i32]]], [2 x [2 x [3 x i32]]]* %b, i32 1, i32 1, i32 2
  %b112 = load i32, i32* %5
  %sum = add i32 %a1, %b112
  ret i32 %sum
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // After pass - should compute: a[5] + b[1][1][2] = 10 + 112 = 122
    auto resultIR = IRPrinter().print(func);
    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %a = alloca [11 x i32]
  %b = alloca [2 x [2 x [3 x i32]]]
  br label %comptime.array.cond.0
comptime.array.cond.0:
  %comptime.array.i.0 = phi i32 [ 0, %entry ], [ %0, %comptime.array.body.0 ]
  %1 = icmp slt i32 %comptime.array.i.0, 11
  br i1 %1, label %comptime.array.body.0, label %entry.split
comptime.array.body.0:
  %2 = getelementptr [11 x i32], [11 x i32]* %a, i32 %comptime.array.i.0
  store i32 0, i32* %2
  %0 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split:
  %3 = getelementptr [11 x i32], [11 x i32]* %a, i32 1
  store i32 2, i32* %3
  br label %comptime.array.cond.0
comptime.array.cond.0:
  %comptime.array.i.0 = phi i32 [ 0, %entry.split ], [ %4, %comptime.array.body.0 ]
  %5 = icmp slt i32 %comptime.array.i.0, 12
  br i1 %5, label %comptime.array.body.0, label %entry.split
comptime.array.body.0:
  %6 = getelementptr [12 x i32], [2 x [2 x [3 x i32]]]* %b, i32 %comptime.array.i.0
  store i32 0, i32* %6
  %4 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split:
  %7 = getelementptr [12 x i32], [2 x [2 x [3 x i32]]]* %b, i32 8
  store i32 102, i32* %7
  %8 = getelementptr [12 x i32], [2 x [2 x [3 x i32]]]* %b, i32 11
  store i32 112, i32* %8
  %9 = getelementptr [11 x i32], [11 x i32]* %a, i32 0
  %10 = getelementptr [11 x i32], [11 x i32]* %a, i32 1
  %11 = getelementptr [2 x [2 x [3 x i32]]], [2 x [2 x [3 x i32]]]* %b, i32 1, i32 0, i32 2
  %12 = getelementptr [2 x [2 x [3 x i32]]], [2 x [2 x [3 x i32]]]* %b, i32 1, i32 1, i32 2
  %13 = getelementptr [11 x i32], [11 x i32]* %a, i32 1
  %14 = getelementptr [2 x [2 x [3 x i32]]], [2 x [2 x [3 x i32]]]* %b, i32 1, i32 1, i32 2
  ret i32 114
}
)");
}

// Test 9.4: Global array assignment
TEST_F(ComptimeTest, GlobalArrayAssignment) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ArrayType::get(intType, 5);

    // Create global array with zero initializer
    std::vector<Constant*> zeros(5, builder->getInt32(0));
    auto zeroInit = ConstantArray::get(arrayType, zeros);
    auto globalArray = GlobalVariable::Create(
        arrayType, false, GlobalVariable::InternalLinkage, zeroInit,
        "global_arr", module.get());

    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Store values to global array
    for (int i = 0; i < 5; i++) {
        auto gep =
            builder->createGEP(arrayType, globalArray, {builder->getInt32(i)});
        builder->createStore(builder->getInt32((i + 1) * 10), gep);
    }

    // Load and sum elements: global_arr[1] + global_arr[3]
    auto gep1 =
        builder->createGEP(arrayType, globalArray, {builder->getInt32(1)});
    auto load1 = builder->createLoad(gep1, "elem1");

    auto gep3 =
        builder->createGEP(arrayType, globalArray, {builder->getInt32(3)});
    auto load3 = builder->createLoad(gep3, "elem3");

    auto sum = builder->createAdd(load1, load3, "sum");
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@global_arr = internal global [5 x i32] [...]

define i32 @main() {
entry:
  %0 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 0
  store i32 10, i32* %0
  %1 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 1
  store i32 20, i32* %1
  %2 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 2
  store i32 30, i32* %2
  %3 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 3
  store i32 40, i32* %3
  %4 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 4
  store i32 50, i32* %4
  %5 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 1
  %elem1 = load i32, i32* %5
  %6 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 3
  %elem3 = load i32, i32* %6
  %sum = add i32 %elem1, %elem3
  ret i32 %sum
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module),
              R"(; ModuleID = 'test_module'

@global_arr = internal global [5 x i32] [i32 10, i32 20, i32 30, i32 40, i32 50]

define i32 @main() {
entry:
  %0 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 0
  %1 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 1
  %2 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 2
  %3 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 3
  %4 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 4
  %5 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 1
  %6 = getelementptr [5 x i32], [5 x i32]* @global_arr, i32 3
  ret i32 60
}

)");
}

// TODO: 多维全局数组

// Test 9.5: Array as function parameter with assignment
TEST_F(ComptimeTest, ArrayAsParameterWithAssignment) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ArrayType::get(intType, 4);
    auto ptrType = PointerType::get(arrayType);

    // Create helper function that modifies array: void init_array(int arr[4])
    auto helperFuncType = FunctionType::get(ctx->getVoidType(), {ptrType});
    auto helperFunc =
        Function::Create(helperFuncType, "init_array", module.get());
    auto helperBB = BasicBlock::Create(ctx.get(), "entry", helperFunc);
    builder->setInsertPoint(helperBB);

    auto arrParam = helperFunc->getArg(0);
    // Initialize array in function: arr[i] = (i+1) * 5
    for (int i = 0; i < 4; i++) {
        auto gep =
            builder->createGEP(arrayType, arrParam, {builder->getInt32(i)});
        builder->createStore(builder->getInt32((i + 1) * 5), gep);
    }
    builder->createRetVoid();

    // Create main function
    auto mainFuncType = FunctionType::get(intType, {});
    auto mainFunc = Function::Create(mainFuncType, "main", module.get());
    auto mainBB = BasicBlock::Create(ctx.get(), "entry", mainFunc);
    builder->setInsertPoint(mainBB);

    // Create local array
    auto localArray = builder->createAlloca(arrayType, nullptr, "local_arr");

    // Call helper function to initialize array
    builder->createCall(helperFunc, {localArray});

    // Load and sum array[0] + array[2]
    auto gep0 =
        builder->createGEP(arrayType, localArray, {builder->getInt32(0)});
    auto load0 = builder->createLoad(gep0, "elem0");

    auto gep2 =
        builder->createGEP(arrayType, localArray, {builder->getInt32(2)});
    auto load2 = builder->createLoad(gep2, "elem2");

    auto sum = builder->createAdd(load0, load2, "sum");
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define void @init_array([4 x i32]* %arg0) {
entry:
  %0 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 0
  store i32 5, i32* %0
  %1 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 1
  store i32 10, i32* %1
  %2 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 2
  store i32 15, i32* %2
  %3 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 3
  store i32 20, i32* %3
  ret void
}

define i32 @main() {
entry:
  %local_arr = alloca [4 x i32]
  call void @init_array([4 x i32]* %local_arr)
  %4 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 0
  %elem0 = load i32, i32* %4
  %5 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 2
  %elem2 = load i32, i32* %5
  %sum = add i32 %elem0, %elem2
  ret i32 %sum
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define void @init_array([4 x i32]* %arg0) {
entry:
  %0 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 0
  store i32 5, i32* %0
  %1 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 1
  store i32 10, i32* %1
  %2 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 2
  store i32 15, i32* %2
  %3 = getelementptr [4 x i32], [4 x i32]* %arg0, i32 3
  store i32 20, i32* %3
  ret void
}

define i32 @main() {
entry:
  %local_arr = alloca [4 x i32]
  %4 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 0
  store i32 5, i32* %4
  %5 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 1
  store i32 10, i32* %5
  %6 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 2
  store i32 15, i32* %6
  %7 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 3
  store i32 20, i32* %7
  %8 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 0
  %9 = getelementptr [4 x i32], [4 x i32]* %local_arr, i32 2
  ret i32 20
}

)");
}

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

    EXPECT_EQ(IRPrinter().print(mainFunc), R"(define i32 @main() {
entry:
  %call_result = call i32 @square(i32 7)
  ret i32 %call_result
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(mainFunc), R"(define i32 @main() {
entry:
  ret i32 49
}
)");
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
    builder->createStore(result, globalVar);
    auto load = builder->createLoad(globalVar, "final_load");
    builder->createRet(load);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@global_var = internal global i32 42

define i32 @main() {
entry:
  %loaded = load i32, i32* @global_var
  %result = add i32 %loaded, 10
  store i32 %result, i32* @global_var
  %final_load = load i32, i32* @global_var
  ret i32 %final_load
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@global_var = internal global i32 52

define i32 @main() {
entry:
  ret i32 52
}

)");
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

    // Entry block - runtime condition
    builder->setInsertPoint(entryBB);
    auto n = builder->createCall(getRuntimeFunction(), {});
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

    auto beforeIR = IRPrinter().print(func);
    EXPECT_EQ(beforeIR, R"(define i32 @main() {
entry:
  %0 = call i32 @runtimeFunc()
  %cond = icmp sgt i32 %0, 0
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
    EXPECT_FALSE(changed);

    EXPECT_EQ(IRPrinter().print(func), beforeIR);
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

// Test 14: Translate a.c while-loop with global and runtime call
TEST_F(ComptimeTest, WhileLoopWithGlobalAndRuntimeCall) {
    auto intType = ctx->getIntegerType(32);

    auto globalG =
        GlobalVariable::Create(intType, false, GlobalVariable::InternalLinkage,
                               builder->getInt32(0), "g", module.get());
    auto globalRes =
        GlobalVariable::Create(intType, false, GlobalVariable::InternalLinkage,
                               builder->getInt32(0), "res", module.get());

    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    // Create basic blocks
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopCondBB = BasicBlock::Create(ctx.get(), "loop.cond", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop.body", func);
    auto thenBB = BasicBlock::Create(ctx.get(), "then", func);
    auto elseBB = BasicBlock::Create(ctx.get(), "else", func);
    auto afterBB = BasicBlock::Create(ctx.get(), "after", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);
    auto one = builder->getInt32(1);
    builder->setInsertPoint(entryBB);
    builder->createBr(loopCondBB);

    builder->setInsertPoint(loopCondBB);
    auto phiI = builder->createPHI(intType, "i");
    phiI->addIncoming(builder->getInt32(0), entryBB);
    auto cmpLoop = builder->createICmpSLT(phiI, builder->getInt32(2), "cmp");
    builder->createCondBr(cmpLoop, loopBodyBB, exitBB);

    builder->setInsertPoint(loopBodyBB);
    auto condThen = builder->createICmpSLT(phiI, builder->getInt32(1), "cond");
    builder->createCondBr(condThen, thenBB, elseBB);

    builder->setInsertPoint(thenBB);
    auto g_then = builder->createLoad(globalG, "g_then");
    auto add_then = builder->createAdd(g_then, one, "add_then");
    builder->createStore(add_then, globalG);
    auto res_then = builder->createLoad(globalRes, "res_then");
    auto add_res = builder->createAdd(res_then, one, "add_res");
    builder->createStore(add_res, globalRes);
    builder->createBr(afterBB);

    builder->setInsertPoint(elseBB);
    auto g_else = builder->createLoad(globalG, "g_else");
    auto call_rt = builder->createCall(getRuntimeFunction(), {}, "call");
    auto add_else = builder->createAdd(g_else, call_rt, "add_else");
    builder->createStore(add_else, globalG);
    builder->createBr(afterBB);

    builder->setInsertPoint(afterBB);
    auto i_next = builder->createAdd(phiI, builder->getInt32(1), "i.next");
    builder->createBr(loopCondBB);
    phiI->addIncoming(i_next, afterBB);

    builder->setInsertPoint(exitBB);
    auto g_final = builder->createLoad(globalG, "g.final");
    builder->createRet(g_final);

    // Before pass - full module IR (includes global @g)
    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@g = internal global i32 0
@res = internal global i32 0

define i32 @main() {
entry:
  br label %loop.cond
loop.cond:
  %i = phi i32 [ 0, %entry ], [ %i.next, %after ]
  %cmp = icmp slt i32 %i, 2
  br i1 %cmp, label %loop.body, label %exit
loop.body:
  %cond = icmp slt i32 %i, 1
  br i1 %cond, label %then, label %else
then:
  %g_then = load i32, i32* @g
  %add_then = add i32 %g_then, 1
  store i32 %add_then, i32* @g
  %res_then = load i32, i32* @res
  %add_res = add i32 %res_then, 1
  store i32 %add_res, i32* @res
  br label %after
else:
  %g_else = load i32, i32* @g
  %call = call i32 @runtimeFunc()
  %add_else = add i32 %g_else, %call
  store i32 %add_else, i32* @g
  br label %after
after:
  %i.next = add i32 %i, 1
  br label %loop.cond
exit:
  %g.final = load i32, i32* @g
  ret i32 %g.final
}

define i32 @runtimeFunc()

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@g = internal global i32 1
@res = internal global i32 1

define i32 @main() {
entry:
  br label %loop.cond
loop.cond:
  %i = phi i32 [ 0, %entry ], [ %i.next, %after ]
  %cmp = icmp slt i32 %i, 2
  br i1 %cmp, label %loop.body, label %exit
loop.body:
  %cond = icmp slt i32 %i, 1
  br i1 %cond, label %then, label %else
then:
  br label %after
else:
  %call = call i32 @runtimeFunc()
  %add_else = add i32 1, %call
  store i32 %add_else, i32* @g
  br label %after
after:
  %i.next = add i32 %i, 1
  br label %loop.cond
exit:
  %g.final = load i32, i32* @g
  ret i32 %g.final
}

define i32 @runtimeFunc()

)");
}