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
  br i1 %1, label %comptime.array.body.0, label %entry.split.1
comptime.array.body.0:
  %2 = getelementptr [15 x i32], [15 x i32]* %arr, i32 %comptime.array.i.0
  store i32 0, i32* %2
  %0 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split.1:
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
  br label %comptime.array.cond.1
comptime.array.cond.1:
  %comptime.array.i.1 = phi i32 [ 0, %entry ], [ %0, %comptime.array.body.1 ]
  %1 = icmp slt i32 %comptime.array.i.1, 11
  br i1 %1, label %comptime.array.body.1, label %entry.split.2
comptime.array.body.1:
  %2 = getelementptr [11 x i32], [11 x i32]* %a, i32 %comptime.array.i.1
  store i32 0, i32* %2
  %0 = add i32 %comptime.array.i.1, 1
  br label %comptime.array.cond.1
entry.split.2:
  %3 = getelementptr [11 x i32], [11 x i32]* %a, i32 1
  store i32 2, i32* %3
  br label %comptime.array.cond.0
comptime.array.cond.0:
  %comptime.array.i.0 = phi i32 [ 0, %entry.split.2 ], [ %4, %comptime.array.body.0 ]
  %5 = icmp slt i32 %comptime.array.i.0, 12
  br i1 %5, label %comptime.array.body.0, label %entry.split.1
comptime.array.body.0:
  %6 = getelementptr [12 x i32], [2 x [2 x [3 x i32]]]* %b, i32 %comptime.array.i.0
  store i32 0, i32* %6
  %4 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split.1:
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

// Test 15: Mixed compile-time/runtime lifecycle transitions
TEST_F(ComptimeTest, MixedComptimeRuntimeLifecycle) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto compTimeBB = BasicBlock::Create(ctx.get(), "comptime_path", func);
    auto runtimeBB = BasicBlock::Create(ctx.get(), "runtime_path", func);
    auto mergeBB = BasicBlock::Create(ctx.get(), "merge", func);

    builder->setInsertPoint(entryBB);
    // Start with compile-time condition
    auto comptimeVal = builder->createAdd(builder->getInt32(5),
                                          builder->getInt32(3), "comptime_add");
    auto comptimeCond = builder->createICmpEQ(comptimeVal, builder->getInt32(8),
                                              "comptime_cond");
    builder->createCondBr(comptimeCond, compTimeBB, runtimeBB);

    // Compile-time path - values should be computed at compile time
    builder->setInsertPoint(compTimeBB);
    auto comptimeResult = builder->createMul(
        builder->getInt32(10), builder->getInt32(4), "comptime_result");
    builder->createBr(mergeBB);

    // Runtime path - should not be taken due to compile-time condition
    builder->setInsertPoint(runtimeBB);
    auto runtimeCall = builder->createCall(getRuntimeFunction(), {});
    auto runtimeResult = builder->createAdd(runtimeCall, builder->getInt32(100),
                                            "runtime_result");
    builder->createBr(mergeBB);

    // Merge - PHI should be resolved to compile-time value
    builder->setInsertPoint(mergeBB);
    auto phi = builder->createPHI(intType, "merged_value");
    phi->addIncoming(comptimeResult, compTimeBB);
    phi->addIncoming(runtimeResult, runtimeBB);

    // Continue with more compile-time operations
    auto finalComptime =
        builder->createAdd(phi, builder->getInt32(20), "final_comptime");
    builder->createRet(finalComptime);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %comptime_add = add i32 5, 3
  %comptime_cond = icmp eq i32 %comptime_add, 8
  br i1 %comptime_cond, label %comptime_path, label %runtime_path
comptime_path:
  %comptime_result = mul i32 10, 4
  br label %merge
runtime_path:
  %0 = call i32 @runtimeFunc()
  %runtime_result = add i32 %0, 100
  br label %merge
merge:
  %merged_value = phi i32 [ %comptime_result, %comptime_path ], [ %runtime_result, %runtime_path ]
  %final_comptime = add i32 %merged_value, 20
  ret i32 %final_comptime
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  br i1 1, label %comptime_path, label %runtime_path
comptime_path:
  br label %merge
runtime_path:
  %0 = call i32 @runtimeFunc()
  %runtime_result = add i32 %0, 100
  br label %merge
merge:
  ret i32 60
}
)");
}

// Test 16: Complex multidimensional global arrays
TEST_F(ComptimeTest, ComplexMultidimensionalGlobalArrays) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});

    // Create 4D global array: global_4d[2][3][2][2]
    auto innerType = ArrayType::get(intType, 2);
    auto level2Type = ArrayType::get(innerType, 2);
    auto level3Type = ArrayType::get(level2Type, 3);
    auto globalArrayType = ArrayType::get(level3Type, 2);

    // Initialize with zeros
    std::vector<Constant*> zeros;
    std::function<void(Type*, std::vector<Constant*>&)> createZeros =
        [&](Type* type, std::vector<Constant*>& vec) {
            if (auto arrayType = dyn_cast<ArrayType>(type)) {
                std::vector<Constant*> elements;
                for (size_t i = 0; i < arrayType->getNumElements(); i++) {
                    createZeros(arrayType->getElementType(), elements);
                }
                vec.push_back(ConstantArray::get(arrayType, elements));
            } else {
                vec.push_back(builder->getInt32(0));
            }
        };

    createZeros(globalArrayType, zeros);
    auto globalArray = GlobalVariable::Create(
        globalArrayType, false, GlobalVariable::InternalLinkage, zeros[0],
        "global_4d", module.get());

    auto func = Function::Create(funcType, "main", module.get());
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Set some compile-time values: global_4d[1][2][1][0] = 42,
    // global_4d[0][1][0][1] = 77
    auto gep1 =
        builder->createGEP(globalArrayType, globalArray,
                           {builder->getInt32(1), builder->getInt32(2),
                            builder->getInt32(1), builder->getInt32(0)});
    builder->createStore(builder->getInt32(42), gep1);

    auto gep2 =
        builder->createGEP(globalArrayType, globalArray,
                           {builder->getInt32(0), builder->getInt32(1),
                            builder->getInt32(0), builder->getInt32(1)});
    builder->createStore(builder->getInt32(77), gep2);

    // Load and compute with compile-time indices
    auto load1 = builder->createLoad(gep1, "load1");
    auto load2 = builder->createLoad(gep2, "load2");
    auto sum = builder->createAdd(load1, load2, "sum");
    auto final = builder->createMul(sum, builder->getInt32(2), "final");

    builder->createRet(final);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@global_4d = internal global [2 x [3 x [2 x [2 x i32]]]] [[3 x [2 x [2 x i32]]] [[2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]]], [3 x [2 x [2 x i32]]] [[2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]]]]

define i32 @main() {
entry:
  %0 = getelementptr [2 x [3 x [2 x [2 x i32]]]], [2 x [3 x [2 x [2 x i32]]]]* @global_4d, i32 1, i32 2, i32 1, i32 0
  store i32 42, i32* %0
  %1 = getelementptr [2 x [3 x [2 x [2 x i32]]]], [2 x [3 x [2 x [2 x i32]]]]* @global_4d, i32 0, i32 1, i32 0, i32 1
  store i32 77, i32* %1
  %load1 = load i32, i32* %0
  %load2 = load i32, i32* %1
  %sum = add i32 %load1, %load2
  %final = mul i32 %sum, 2
  ret i32 %final
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

@global_4d = internal global [2 x [3 x [2 x [2 x i32]]]] [[3 x [2 x [2 x i32]]] [[2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [i32 0, i32 77], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]]], [3 x [2 x [2 x i32]]] [[2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [...]], [2 x [2 x i32]] [[2 x i32] [...], [2 x i32] [i32 42, ...]]]]

define i32 @main() {
entry:
  %0 = getelementptr [2 x [3 x [2 x [2 x i32]]]], [2 x [3 x [2 x [2 x i32]]]]* @global_4d, i32 1, i32 2, i32 1, i32 0
  %1 = getelementptr [2 x [3 x [2 x [2 x i32]]]], [2 x [3 x [2 x [2 x i32]]]]* @global_4d, i32 0, i32 1, i32 0, i32 1
  ret i32 238
}

)");
}

// Test 17: Local variable promotion with complex control flow
TEST_F(ComptimeTest, LocalVariablePromotionComplexControlFlow) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop_header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop_body", func);
    auto ifThenBB = BasicBlock::Create(ctx.get(), "if_then", func);
    auto ifElseBB = BasicBlock::Create(ctx.get(), "if_else", func);
    auto loopLatchBB = BasicBlock::Create(ctx.get(), "loop_latch", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    // Entry: allocate local variable and initialize
    builder->setInsertPoint(entryBB);
    auto localVar = builder->createAlloca(intType, nullptr, "local_var");
    builder->createStore(builder->getInt32(10), localVar);
    builder->createBr(loopHeaderBB);

    // Loop header: i from 0 to 3
    builder->setInsertPoint(loopHeaderBB);
    auto loopIVar = builder->createPHI(intType, "loop_i");
    loopIVar->addIncoming(builder->getInt32(0), entryBB);
    auto loopCond =
        builder->createICmpSLT(loopIVar, builder->getInt32(3), "loop_cond");
    builder->createCondBr(loopCond, loopBodyBB, exitBB);

    // Loop body: conditional based on compile-time value
    builder->setInsertPoint(loopBodyBB);
    auto currentVal = builder->createLoad(localVar, "current_val");
    auto isEven = builder->createICmpEQ(
        builder->createRem(loopIVar, builder->getInt32(2), "mod"),
        builder->getInt32(0), "is_even");
    builder->createCondBr(isEven, ifThenBB, ifElseBB);

    // If then: multiply by 2
    builder->setInsertPoint(ifThenBB);
    auto doubled =
        builder->createMul(currentVal, builder->getInt32(2), "doubled");
    builder->createStore(doubled, localVar);
    builder->createBr(loopLatchBB);

    // If else: add 5
    builder->setInsertPoint(ifElseBB);
    auto added = builder->createAdd(currentVal, builder->getInt32(5), "added");
    builder->createStore(added, localVar);
    builder->createBr(loopLatchBB);

    // Loop latch
    builder->setInsertPoint(loopLatchBB);
    auto nextI = builder->createAdd(loopIVar, builder->getInt32(1), "next_i");
    builder->createBr(loopHeaderBB);
    loopIVar->addIncoming(nextI, loopLatchBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalLoad = builder->createLoad(localVar, "final_load");
    builder->createRet(finalLoad);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %local_var = alloca i32
  store i32 10, i32* %local_var
  br label %loop_header
loop_header:
  %loop_i = phi i32 [ 0, %entry ], [ %next_i, %loop_latch ]
  %loop_cond = icmp slt i32 %loop_i, 3
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %current_val = load i32, i32* %local_var
  %mod = srem i32 %loop_i, 2
  %is_even = icmp eq i32 %mod, 0
  br i1 %is_even, label %if_then, label %if_else
if_then:
  %doubled = mul i32 %current_val, 2
  store i32 %doubled, i32* %local_var
  br label %loop_latch
if_else:
  %added = add i32 %current_val, 5
  store i32 %added, i32* %local_var
  br label %loop_latch
loop_latch:
  %next_i = add i32 %loop_i, 1
  br label %loop_header
exit:
  %final_load = load i32, i32* %local_var
  ret i32 %final_load
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %local_var = alloca i32
  store i32 50, i32* %local_var
  br label %loop_header
loop_header:
  %loop_i = phi i32 [ 0, %entry ], [ %next_i, %loop_latch ]
  %loop_cond = icmp slt i32 %loop_i, 3
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %current_val = load i32, i32* %local_var
  %mod = srem i32 %loop_i, 2
  %is_even = icmp eq i32 %mod, 0
  br i1 %is_even, label %if_then, label %if_else
if_then:
  %doubled = mul i32 %current_val, 2
  br label %loop_latch
if_else:
  br label %loop_latch
loop_latch:
  %next_i = add i32 %loop_i, 1
  br label %loop_header
exit:
  ret i32 50
}

)");
}

// Test 18: Function calls with mixed compile-time and runtime arguments
TEST_F(ComptimeTest, FunctionCallsMixedComptimeRuntimeArgs) {
    auto intType = ctx->getIntegerType(32);

    // Create helper function: compute(a, b) = a * a + b
    auto helperFuncType = FunctionType::get(intType, {intType, intType});
    auto helperFunc = Function::Create(helperFuncType, "compute", module.get());
    auto helperBB = BasicBlock::Create(ctx.get(), "entry", helperFunc);
    builder->setInsertPoint(helperBB);
    auto param_a = helperFunc->getArg(0);
    auto param_b = helperFunc->getArg(1);
    auto squared = builder->createMul(param_a, param_a, "a_squared");
    auto result = builder->createAdd(squared, param_b, "result");
    builder->createRet(result);

    // Main function
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Mix compile-time and runtime values
    auto comptimeArg = builder->getInt32(5);  // compile-time
    auto runtimeArg = builder->createCall(getRuntimeFunction(), {});  // runtime

    // Call 1: both compile-time (should be fully optimized)
    auto call1 = builder->createCall(
        helperFunc, {comptimeArg, builder->getInt32(10)}, "call1");

    // Call 2: mixed (partial optimization)
    auto call2 =
        builder->createCall(helperFunc, {comptimeArg, runtimeArg}, "call2");

    // Final computation
    auto sum = builder->createAdd(call1, call2, "sum");
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @compute(i32 %arg0, i32 %arg1) {
entry:
  %a_squared = mul i32 %arg0, %arg0
  %result = add i32 %a_squared, %arg1
  ret i32 %result
}

define i32 @main() {
entry:
  %0 = call i32 @runtimeFunc()
  %call1 = call i32 @compute(i32 5, i32 10)
  %call2 = call i32 @compute(i32 5, i32 %0)
  %sum = add i32 %call1, %call2
  ret i32 %sum
}

define i32 @runtimeFunc()

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // call1 should be optimized to: 5*5 + 10 = 35
    // call2 should partially optimize: 25 + runtime_value
    EXPECT_EQ(IRPrinter().print(module),
              R"(; ModuleID = 'test_module'

define i32 @compute(i32 %arg0, i32 %arg1) {
entry:
  %a_squared = mul i32 %arg0, %arg0
  %result = add i32 %a_squared, %arg1
  ret i32 %result
}

define i32 @main() {
entry:
  %0 = call i32 @runtimeFunc()
  %call2 = call i32 @compute(i32 5, i32 %0)
  %sum = add i32 35, %call2
  ret i32 %sum
}

define i32 @runtimeFunc()

)");
}

// Test 19: Array operations with runtime indices mixed with compile-time
TEST_F(ComptimeTest, ArrayOperationsRuntimeIndicesWithComptime) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ArrayType::get(intType, 10);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Create array and initialize with compile-time values
    auto array = builder->createAlloca(arrayType, nullptr, "array");
    for (int i = 0; i < 5; i++) {
        auto gep = builder->createGEP(arrayType, array, {builder->getInt32(i)});
        builder->createStore(builder->getInt32(i * i), gep);  // Store i^2
    }

    // Mix compile-time and runtime access
    auto comptimeIdx = builder->getInt32(3);
    auto runtimeIdx = builder->createCall(getRuntimeFunction(), {});

    // Compile-time access
    auto comptimeGEP = builder->createGEP(arrayType, array, {comptimeIdx});
    auto comptimeLoad = builder->createLoad(comptimeGEP, "comptime_load");

    // Runtime access
    auto runtimeGEP = builder->createGEP(arrayType, array, {runtimeIdx});
    auto runtimeLoad = builder->createLoad(runtimeGEP, "runtime_load");

    // Combine results
    auto combined = builder->createAdd(comptimeLoad, runtimeLoad, "combined");
    auto final = builder->createMul(combined, builder->getInt32(2), "final");

    builder->createRet(final);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %array = alloca [10 x i32]
  %0 = getelementptr [10 x i32], [10 x i32]* %array, i32 0
  store i32 0, i32* %0
  %1 = getelementptr [10 x i32], [10 x i32]* %array, i32 1
  store i32 1, i32* %1
  %2 = getelementptr [10 x i32], [10 x i32]* %array, i32 2
  store i32 4, i32* %2
  %3 = getelementptr [10 x i32], [10 x i32]* %array, i32 3
  store i32 9, i32* %3
  %4 = getelementptr [10 x i32], [10 x i32]* %array, i32 4
  store i32 16, i32* %4
  %5 = call i32 @runtimeFunc()
  %6 = getelementptr [10 x i32], [10 x i32]* %array, i32 3
  %comptime_load = load i32, i32* %6
  %7 = getelementptr [10 x i32], [10 x i32]* %array, i32 %5
  %runtime_load = load i32, i32* %7
  %combined = add i32 %comptime_load, %runtime_load
  %final = mul i32 %combined, 2
  ret i32 %final
}

define i32 @runtimeFunc()

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %array = alloca [10 x i32]
  br label %comptime.array.cond.0
comptime.array.cond.0:
  %comptime.array.i.0 = phi i32 [ 0, %entry ], [ %0, %comptime.array.body.0 ]
  %1 = icmp slt i32 %comptime.array.i.0, 10
  br i1 %1, label %comptime.array.body.0, label %entry.split.1
comptime.array.body.0:
  %2 = getelementptr [10 x i32], [10 x i32]* %array, i32 %comptime.array.i.0
  store i32 0, i32* %2
  %0 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split.1:
  %3 = getelementptr [10 x i32], [10 x i32]* %array, i32 1
  store i32 1, i32* %3
  %4 = getelementptr [10 x i32], [10 x i32]* %array, i32 2
  store i32 4, i32* %4
  %5 = getelementptr [10 x i32], [10 x i32]* %array, i32 3
  store i32 9, i32* %5
  %6 = getelementptr [10 x i32], [10 x i32]* %array, i32 4
  store i32 16, i32* %6
  %7 = getelementptr [10 x i32], [10 x i32]* %array, i32 0
  %8 = getelementptr [10 x i32], [10 x i32]* %array, i32 1
  %9 = getelementptr [10 x i32], [10 x i32]* %array, i32 2
  %10 = getelementptr [10 x i32], [10 x i32]* %array, i32 3
  %11 = getelementptr [10 x i32], [10 x i32]* %array, i32 4
  %12 = call i32 @runtimeFunc()
  %13 = getelementptr [10 x i32], [10 x i32]* %array, i32 3
  %14 = getelementptr [10 x i32], [10 x i32]* %array, i32 %12
  %runtime_load = load i32, i32* %14
  %combined = add i32 9, %runtime_load
  %final = mul i32 %combined, 2
  ret i32 %final
}

define i32 @runtimeFunc()

)");
}

// Test 20: Nested function calls with compile-time propagation
TEST_F(ComptimeTest, NestedFunctionCallsComptimePropagation) {
    auto intType = ctx->getIntegerType(32);

    // Helper1: square(x) = x * x
    auto helper1Type = FunctionType::get(intType, {intType});
    auto helper1 = Function::Create(helper1Type, "square", module.get());
    auto h1BB = BasicBlock::Create(ctx.get(), "entry", helper1);
    builder->setInsertPoint(h1BB);
    auto h1_param = helper1->getArg(0);
    auto h1_result = builder->createMul(h1_param, h1_param, "squared");
    builder->createRet(h1_result);

    // Helper2: add_ten(x) = x + 10
    auto helper2Type = FunctionType::get(intType, {intType});
    auto helper2 = Function::Create(helper2Type, "add_ten", module.get());
    auto h2BB = BasicBlock::Create(ctx.get(), "entry", helper2);
    builder->setInsertPoint(h2BB);
    auto h2_param = helper2->getArg(0);
    auto h2_result =
        builder->createAdd(h2_param, builder->getInt32(10), "add_ten");
    builder->createRet(h2_result);

    // Helper3: combine(x, y) = square(x) + add_ten(y)
    auto helper3Type = FunctionType::get(intType, {intType, intType});
    auto helper3 = Function::Create(helper3Type, "combine", module.get());
    auto h3BB = BasicBlock::Create(ctx.get(), "entry", helper3);
    builder->setInsertPoint(h3BB);
    auto h3_param1 = helper3->getArg(0);
    auto h3_param2 = helper3->getArg(1);
    auto h3_call1 = builder->createCall(helper1, {h3_param1}, "square_call");
    auto h3_call2 = builder->createCall(helper2, {h3_param2}, "add_ten_call");
    auto h3_result = builder->createAdd(h3_call1, h3_call2, "combined");
    builder->createRet(h3_result);

    // Main function
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());
    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    // Nested calls with compile-time values
    auto call1 = builder->createCall(
        helper3, {builder->getInt32(4), builder->getInt32(6)}, "nested_call");
    auto final = builder->createMul(call1, builder->getInt32(2), "final");

    builder->createRet(final);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @square(i32 %arg0) {
entry:
  %squared = mul i32 %arg0, %arg0
  ret i32 %squared
}

define i32 @add_ten(i32 %arg0) {
entry:
  %add_ten = add i32 %arg0, 10
  ret i32 %add_ten
}

define i32 @combine(i32 %arg0, i32 %arg1) {
entry:
  %square_call = call i32 @square(i32 %arg0)
  %add_ten_call = call i32 @add_ten(i32 %arg1)
  %combined = add i32 %square_call, %add_ten_call
  ret i32 %combined
}

define i32 @main() {
entry:
  %nested_call = call i32 @combine(i32 4, i32 6)
  %final = mul i32 %nested_call, 2
  ret i32 %final
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @square(i32 %arg0) {
entry:
  %squared = mul i32 %arg0, %arg0
  ret i32 %squared
}

define i32 @add_ten(i32 %arg0) {
entry:
  %add_ten = add i32 %arg0, 10
  ret i32 %add_ten
}

define i32 @combine(i32 %arg0, i32 %arg1) {
entry:
  %square_call = call i32 @square(i32 %arg0)
  %add_ten_call = call i32 @add_ten(i32 %arg1)
  %combined = add i32 %square_call, %add_ten_call
  ret i32 %combined
}

define i32 @main() {
entry:
  ret i32 64
}

)");
}

// Test 21: Compile-time loop unrolling with simple counting loop
TEST_F(ComptimeTest, CompileTimeLoopUnrollingSimple) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop_header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop_body", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    // Entry: initialize sum = 0
    builder->setInsertPoint(entryBB);
    auto sumVar = builder->createAlloca(intType, nullptr, "sum");
    builder->createStore(builder->getInt32(0), sumVar);
    builder->createBr(loopHeaderBB);

    // Loop header: for(i = 0; i < 5; i++)
    builder->setInsertPoint(loopHeaderBB);
    auto loopIVar = builder->createPHI(intType, "loop_i");
    loopIVar->addIncoming(builder->getInt32(0), entryBB);
    auto loopCond =
        builder->createICmpSLT(loopIVar, builder->getInt32(5), "loop_cond");
    builder->createCondBr(loopCond, loopBodyBB, exitBB);

    // Loop body: sum += i * 2
    builder->setInsertPoint(loopBodyBB);
    auto currentSum = builder->createLoad(sumVar, "current_sum");
    auto doubled_i =
        builder->createMul(loopIVar, builder->getInt32(2), "doubled_i");
    auto newSum = builder->createAdd(currentSum, doubled_i, "new_sum");
    builder->createStore(newSum, sumVar);

    auto nextI = builder->createAdd(loopIVar, builder->getInt32(1), "next_i");
    builder->createBr(loopHeaderBB);
    loopIVar->addIncoming(nextI, loopBodyBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalSum = builder->createLoad(sumVar, "final_sum");
    builder->createRet(finalSum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %sum = alloca i32
  store i32 0, i32* %sum
  br label %loop_header
loop_header:
  %loop_i = phi i32 [ 0, %entry ], [ %next_i, %loop_body ]
  %loop_cond = icmp slt i32 %loop_i, 5
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %current_sum = load i32, i32* %sum
  %doubled_i = mul i32 %loop_i, 2
  %new_sum = add i32 %current_sum, %doubled_i
  store i32 %new_sum, i32* %sum
  %next_i = add i32 %loop_i, 1
  br label %loop_header
exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // Should unroll to: 0*2 + 1*2 + 2*2 + 3*2 + 4*2 = 0 + 2 + 4 + 6 + 8 = 20
    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %sum = alloca i32
  store i32 20, i32* %sum
  br label %loop_header
loop_header:
  %loop_i = phi i32 [ 0, %entry ], [ %next_i, %loop_body ]
  %loop_cond = icmp slt i32 %loop_i, 5
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %current_sum = load i32, i32* %sum
  %doubled_i = mul i32 %loop_i, 2
  %new_sum = add i32 %current_sum, %doubled_i
  %next_i = add i32 %loop_i, 1
  br label %loop_header
exit:
  ret i32 20
}

)");
}

// Test 22: Complex loop unrolling with nested conditions
TEST_F(ComptimeTest, ComplexLoopUnrollingWithConditions) {
    auto intType = ctx->getIntegerType(32);
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    auto loopHeaderBB = BasicBlock::Create(ctx.get(), "loop_header", func);
    auto loopBodyBB = BasicBlock::Create(ctx.get(), "loop_body", func);
    auto ifEvenBB = BasicBlock::Create(ctx.get(), "if_even", func);
    auto ifOddBB = BasicBlock::Create(ctx.get(), "if_odd", func);
    auto loopLatchBB = BasicBlock::Create(ctx.get(), "loop_latch", func);
    auto exitBB = BasicBlock::Create(ctx.get(), "exit", func);

    // Entry: initialize result = 1
    builder->setInsertPoint(entryBB);
    auto resultVar = builder->createAlloca(intType, nullptr, "result");
    builder->createStore(builder->getInt32(1), resultVar);
    builder->createBr(loopHeaderBB);

    // Loop header: for(i = 1; i <= 4; i++)
    builder->setInsertPoint(loopHeaderBB);
    auto loopIVar = builder->createPHI(intType, "loop_i");
    loopIVar->addIncoming(builder->getInt32(1), entryBB);
    auto loopCond =
        builder->createICmpSLE(loopIVar, builder->getInt32(4), "loop_cond");
    builder->createCondBr(loopCond, loopBodyBB, exitBB);

    // Loop body: check if i is even or odd
    builder->setInsertPoint(loopBodyBB);
    auto isEven = builder->createICmpEQ(
        builder->createRem(loopIVar, builder->getInt32(2), "mod"),
        builder->getInt32(0), "is_even");
    builder->createCondBr(isEven, ifEvenBB, ifOddBB);

    // If even: result *= i
    builder->setInsertPoint(ifEvenBB);
    auto currentResult1 = builder->createLoad(resultVar, "current_result1");
    auto evenResult =
        builder->createMul(currentResult1, loopIVar, "even_result");
    builder->createStore(evenResult, resultVar);
    builder->createBr(loopLatchBB);

    // If odd: result += i * 3
    builder->setInsertPoint(ifOddBB);
    auto currentResult2 = builder->createLoad(resultVar, "current_result2");
    auto tripled =
        builder->createMul(loopIVar, builder->getInt32(3), "tripled");
    auto oddResult = builder->createAdd(currentResult2, tripled, "odd_result");
    builder->createStore(oddResult, resultVar);
    builder->createBr(loopLatchBB);

    // Loop latch
    builder->setInsertPoint(loopLatchBB);
    auto nextI = builder->createAdd(loopIVar, builder->getInt32(1), "next_i");
    builder->createBr(loopHeaderBB);
    loopIVar->addIncoming(nextI, loopLatchBB);

    // Exit
    builder->setInsertPoint(exitBB);
    auto finalResult = builder->createLoad(resultVar, "final_result");
    builder->createRet(finalResult);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %result = alloca i32
  store i32 1, i32* %result
  br label %loop_header
loop_header:
  %loop_i = phi i32 [ 1, %entry ], [ %next_i, %loop_latch ]
  %loop_cond = icmp sle i32 %loop_i, 4
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %mod = srem i32 %loop_i, 2
  %is_even = icmp eq i32 %mod, 0
  br i1 %is_even, label %if_even, label %if_odd
if_even:
  %current_result1 = load i32, i32* %result
  %even_result = mul i32 %current_result1, %loop_i
  store i32 %even_result, i32* %result
  br label %loop_latch
if_odd:
  %current_result2 = load i32, i32* %result
  %tripled = mul i32 %loop_i, 3
  %odd_result = add i32 %current_result2, %tripled
  store i32 %odd_result, i32* %result
  br label %loop_latch
loop_latch:
  %next_i = add i32 %loop_i, 1
  br label %loop_header
exit:
  %final_result = load i32, i32* %result
  ret i32 %final_result
}

)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    // Should unroll to:
    // i=1 (odd): result = 1 + 1*3 = 4
    // i=2 (even): result = 4 * 2 = 8
    // i=3 (odd): result = 8 + 3*3 = 17
    // i=4 (even): result = 17 * 4 = 68
    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @main() {
entry:
  %result = alloca i32
  store i32 68, i32* %result
  br label %loop_header
loop_header:
  %loop_i = phi i32 [ 1, %entry ], [ %next_i, %loop_latch ]
  %loop_cond = icmp sle i32 %loop_i, 4
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %mod = srem i32 %loop_i, 2
  %is_even = icmp eq i32 %mod, 0
  br i1 %is_even, label %if_even, label %if_odd
if_even:
  %current_result1 = load i32, i32* %result
  %even_result = mul i32 %current_result1, %loop_i
  br label %loop_latch
if_odd:
  %current_result2 = load i32, i32* %result
  %tripled = mul i32 %loop_i, 3
  %odd_result = add i32 %current_result2, %tripled
  br label %loop_latch
loop_latch:
  %next_i = add i32 %loop_i, 1
  br label %loop_header
exit:
  ret i32 68
}

)");
}

// Test 22
TEST_F(ComptimeTest, ConstantGEPMultiType) {
    auto intType = ctx->getIntegerType(32);
    auto arrayType = ArrayType::get(ArrayType::get(intType, 3), 4);
    auto flattenArrayType = ArrayType::get(intType, 12);
    auto pointerType = PointerType::get(intType);
    auto pointerType2 = PointerType::get(ArrayType::get(intType, 3));
    auto funcType = FunctionType::get(intType, {});
    auto func = Function::Create(funcType, "main", module.get());

    auto entryBB = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entryBB);

    auto array = builder->createAlloca(arrayType, nullptr, "array");
    auto gep = builder->createGEP(arrayType, array,
                                  {builder->getInt32(2), builder->getInt32(1)});
    builder->createStore(builder->getInt32(1), gep);

    auto flattenGEP = builder->createGEP(flattenArrayType, array,
                                         {builder->getInt32(2 * 3 + 1)});

    auto load = builder->createLoad(flattenGEP, "flatten_load");

    auto pointerGEP =
        builder->createGEP(pointerType, array, {builder->getInt32(2 * 3 + 1)});

    auto load2 = builder->createLoad(pointerGEP, "pointer_load");

    auto pointerGEP2 = builder->createGEP(
        pointerType2, array, {builder->getInt32(2), builder->getInt32(1)});

    auto load3 = builder->createLoad(pointerGEP2, "pointer_load2");

    auto pointerGEP3 =
        builder->createGEP(pointerType2, array, {builder->getInt32(2)});

    auto pointerGEP4 = builder->createGEP(pointerGEP3, builder->getInt32(1));

    auto load4 = builder->createLoad(pointerGEP4, "pointer_load3");

    auto add = builder->createAdd(load, load2, "add");
    auto add2 = builder->createAdd(add, load3, "add2");
    auto res = builder->createAdd(add2, load4, "res");

    builder->createRet(res);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %array = alloca [4 x [3 x i32]]
  %0 = getelementptr [4 x [3 x i32]], [4 x [3 x i32]]* %array, i32 2, i32 1
  store i32 1, i32* %0
  %1 = getelementptr [12 x i32], [4 x [3 x i32]]* %array, i32 7
  %flatten_load = load i32, i32* %1
  %2 = getelementptr i32*, [4 x [3 x i32]]* %array, i32 7
  %pointer_load = load i32, i32* %2
  %3 = getelementptr [3 x i32]*, [4 x [3 x i32]]* %array, i32 2, i32 1
  %pointer_load2 = load i32, i32* %3
  %4 = getelementptr [3 x i32]*, [4 x [3 x i32]]* %array, i32 2
  %5 = getelementptr [3 x i32], [3 x i32]* %4, i32 1
  %pointer_load3 = load i32, i32* %5
  %add = add i32 %flatten_load, %pointer_load
  %add2 = add i32 %add, %pointer_load2
  %res = add i32 %add2, %pointer_load3
  ret i32 %res
}
)");

    ComptimePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(func), R"(define i32 @main() {
entry:
  %array = alloca [4 x [3 x i32]]
  br label %comptime.array.cond.0
comptime.array.cond.0:
  %comptime.array.i.0 = phi i32 [ 0, %entry ], [ %0, %comptime.array.body.0 ]
  %1 = icmp slt i32 %comptime.array.i.0, 12
  br i1 %1, label %comptime.array.body.0, label %entry.split.1
comptime.array.body.0:
  %2 = getelementptr [12 x i32], [4 x [3 x i32]]* %array, i32 %comptime.array.i.0
  store i32 0, i32* %2
  %0 = add i32 %comptime.array.i.0, 1
  br label %comptime.array.cond.0
entry.split.1:
  %3 = getelementptr [12 x i32], [4 x [3 x i32]]* %array, i32 7
  store i32 1, i32* %3
  %4 = getelementptr [4 x [3 x i32]], [4 x [3 x i32]]* %array, i32 2, i32 1
  %5 = getelementptr [12 x i32], [4 x [3 x i32]]* %array, i32 7
  %6 = getelementptr i32*, [4 x [3 x i32]]* %array, i32 7
  %7 = getelementptr [3 x i32]*, [4 x [3 x i32]]* %array, i32 2, i32 1
  %8 = getelementptr [3 x i32]*, [4 x [3 x i32]]* %array, i32 2
  %9 = getelementptr [3 x i32], [3 x i32]* %8, i32 1
  ret i32 4
}
)");
}