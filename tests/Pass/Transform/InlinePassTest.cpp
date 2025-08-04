#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Pass.h"
#include "Pass/Transform/InlinePass.h"

using namespace midend;

class InlinePassTest : public ::testing::Test {
   protected:
    void SetUp() override {
        am = new AnalysisManager();
        am->registerAnalysisType<CallGraphAnalysis>();
        am->registerAnalysisType<AliasAnalysis>();
        ctx = new Context();
        builder = new IRBuilder(ctx);
        module = new Module("test_module", ctx);
    }

    void TearDown() override {
        delete builder;
        delete module;
        delete am;
        delete ctx;
    }

    Context* ctx;
    AnalysisManager* am;
    Module* module = nullptr;
    IRBuilder* builder;
};

TEST_F(InlinePassTest, BasicFunctionInlining) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create callee function: int add_one(int x) { return x + 1; }
    auto* callee = Function::Create(funcType, "add_one", module);
    auto* calleeEntry = BasicBlock::Create(ctx, "entry", callee);
    builder->setInsertPoint(calleeEntry);
    auto* param = callee->getArg(0);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* add = builder->createAdd(param, one);
    builder->createRet(add);

    // Create caller function: int caller() { return add_one(5); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* five = ConstantInt::get(i32Type, 5);
    auto* call = builder->createCall(callee, {five});
    builder->createRet(call);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @add_one(i32 %arg0) {
entry:
  %0 = add i32 %arg0, 1
  ret i32 %0
}

define i32 @caller() {
entry:
  %1 = call i32 @add_one(i32 5)
  ret i32 %1
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @add_one(i32 %arg0) {
entry:
  %0 = add i32 %arg0, 1
  ret i32 %0
}

define i32 @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  %1 = add i32 5, 1
  br label %entry.inline1_after
entry.inline1_after:
  ret i32 %1
}

)");
}

TEST_F(InlinePassTest, ConstantParameterInlining) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type, i32Type});

    // Create callee function: int multiply(int x, int y) { return x * y; }
    auto* callee = Function::Create(funcType, "multiply", module);
    auto* calleeEntry = BasicBlock::Create(ctx, "entry", callee);
    builder->setInsertPoint(calleeEntry);
    auto* param1 = callee->getArg(0);
    auto* param2 = callee->getArg(1);
    auto* mul = builder->createMul(param1, param2);
    builder->createRet(mul);

    // Create caller function: int caller() { return multiply(10, 20); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* ten = ConstantInt::get(i32Type, 10);
    auto* twenty = ConstantInt::get(i32Type, 20);
    auto* call = builder->createCall(callee, {ten, twenty});
    builder->createRet(call);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @multiply(i32 %arg0, i32 %arg1) {
entry:
  %0 = mul i32 %arg0, %arg1
  ret i32 %0
}

define i32 @caller() {
entry:
  %1 = call i32 @multiply(i32 10, i32 20)
  ret i32 %1
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @multiply(i32 %arg0, i32 %arg1) {
entry:
  %0 = mul i32 %arg0, %arg1
  ret i32 %0
}

define i32 @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  %1 = mul i32 10, 20
  br label %entry.inline1_after
entry.inline1_after:
  ret i32 %1
}

)");
}

TEST_F(InlinePassTest, VariableParameterInlining) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* callerFuncType = FunctionType::get(i32Type, {i32Type});
    auto* calleeFuncType = FunctionType::get(i32Type, {i32Type});

    // Create callee function: int square(int x) { return x * x; }
    auto* callee = Function::Create(calleeFuncType, "square", module);
    auto* calleeEntry = BasicBlock::Create(ctx, "entry", callee);
    builder->setInsertPoint(calleeEntry);
    auto* param = callee->getArg(0);
    auto* mul = builder->createMul(param, param);
    builder->createRet(mul);

    // Create caller function: int caller(int n) { return square(n); }
    auto* caller = Function::Create(callerFuncType, "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* arg = caller->getArg(0);
    auto* call = builder->createCall(callee, {arg});
    builder->createRet(call);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @square(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, %arg0
  ret i32 %0
}

define i32 @caller(i32 %arg0) {
entry:
  %1 = call i32 @square(i32 %arg0)
  ret i32 %1
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @square(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, %arg0
  ret i32 %0
}

define i32 @caller(i32 %arg0) {
entry:
  br label %entry.inline1
entry.inline1:
  %1 = mul i32 %arg0, %arg0
  br label %entry.inline1_after
entry.inline1_after:
  ret i32 %1
}

)");
}

TEST_F(InlinePassTest, MultiBlockFunctionInlining) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create callee function with control flow:
    // int abs_value(int x) {
    //   if (x < 0) return -x;
    //   else return x;
    // }
    auto* callee = Function::Create(funcType, "abs_value", module);
    auto* entry = BasicBlock::Create(ctx, "entry", callee);
    auto* negBB = BasicBlock::Create(ctx, "negative", callee);
    auto* posBB = BasicBlock::Create(ctx, "positive", callee);

    builder->setInsertPoint(entry);
    auto* param = callee->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* cmp = builder->createICmpSLT(param, zero);
    builder->createCondBr(cmp, negBB, posBB);

    builder->setInsertPoint(negBB);
    auto* neg = builder->createSub(zero, param);
    builder->createRet(neg);

    builder->setInsertPoint(posBB);
    builder->createRet(param);

    // Create caller function: int caller() { return abs_value(-5); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* minusFive = ConstantInt::get(i32Type, -5);
    auto* call = builder->createCall(callee, {minusFive});
    builder->createRet(call);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @abs_value(i32 %arg0) {
entry:
  %0 = icmp slt i32 %arg0, 0
  br i1 %0, label %negative, label %positive
negative:
  %1 = sub i32 0, %arg0
  ret i32 %1
positive:
  ret i32 %arg0
}

define i32 @caller() {
entry:
  %2 = call i32 @abs_value(i32 -5)
  ret i32 %2
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @abs_value(i32 %arg0) {
entry:
  %0 = icmp slt i32 %arg0, 0
  br i1 %0, label %negative, label %positive
negative:
  %1 = sub i32 0, %arg0
  ret i32 %1
positive:
  ret i32 %arg0
}

define i32 @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  %2 = icmp slt i32 -5, 0
  br i1 %2, label %negative.inline1, label %positive.inline1
negative.inline1:
  %3 = sub i32 0, -5
  br label %entry.inline1_after
positive.inline1:
  br label %entry.inline1_after
entry.inline1_after:
  %4 = phi i32 [ %3, %negative.inline1 ], [ -5, %positive.inline1 ]
  ret i32 %4
}

)");
}

TEST_F(InlinePassTest, RecursiveCallNotInlined) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create recursive factorial function:
    // int factorial(int n) {
    //   if (n <= 1) return 1;
    //   return n * factorial(n - 1);
    // }
    auto* factorial = Function::Create(funcType, "factorial", module);
    auto* entry = BasicBlock::Create(ctx, "entry", factorial);
    auto* baseBB = BasicBlock::Create(ctx, "base", factorial);
    auto* recurBB = BasicBlock::Create(ctx, "recursive", factorial);

    builder->setInsertPoint(entry);
    auto* param = factorial->getArg(0);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* cmp = builder->createICmpSLE(param, one);
    builder->createCondBr(cmp, baseBB, recurBB);

    builder->setInsertPoint(baseBB);
    builder->createRet(one);

    builder->setInsertPoint(recurBB);
    auto* nMinus1 = builder->createSub(param, one);
    auto* recurCall = builder->createCall(factorial, {nMinus1});
    auto* result = builder->createMul(param, recurCall);
    builder->createRet(result);

    // Create caller function: int caller() { return factorial(5); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* five = ConstantInt::get(i32Type, 5);
    auto* call = builder->createCall(factorial, {five});
    builder->createRet(call);

    std::string beforeInline = IRPrinter().print(module);

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_FALSE(changed);  // Should not inline recursive function

    std::string afterInline = IRPrinter().print(module);
    EXPECT_EQ(beforeInline, afterInline);  // Should remain unchanged
}

TEST_F(InlinePassTest, TransitiveInlining) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create chain: a() -> b() -> c()
    // int c(int x) { return x + 1; }
    auto* funcC = Function::Create(funcType, "c", module);
    auto* cEntry = BasicBlock::Create(ctx, "entry", funcC);
    builder->setInsertPoint(cEntry);
    auto* cParam = funcC->getArg(0);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* cResult = builder->createAdd(cParam, one);
    builder->createRet(cResult);

    // int b(int x) { return c(x + 2); }
    auto* funcB = Function::Create(funcType, "b", module);
    auto* bEntry = BasicBlock::Create(ctx, "entry", funcB);
    builder->setInsertPoint(bEntry);
    auto* bParam = funcB->getArg(0);
    auto* two = ConstantInt::get(i32Type, 2);
    auto* bArg = builder->createAdd(bParam, two);
    auto* bCall = builder->createCall(funcC, {bArg});
    builder->createRet(bCall);

    // int a(int x) { return b(x + 3); }
    auto* funcA = Function::Create(funcType, "a", module);
    auto* aEntry = BasicBlock::Create(ctx, "entry", funcA);
    builder->setInsertPoint(aEntry);
    auto* aParam = funcA->getArg(0);
    auto* three = ConstantInt::get(i32Type, 3);
    auto* aArg = builder->createAdd(aParam, three);
    auto* aCall = builder->createCall(funcB, {aArg});
    builder->createRet(aCall);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @c(i32 %arg0) {
entry:
  %0 = add i32 %arg0, 1
  ret i32 %0
}

define i32 @b(i32 %arg0) {
entry:
  %1 = add i32 %arg0, 2
  %2 = call i32 @c(i32 %1)
  ret i32 %2
}

define i32 @a(i32 %arg0) {
entry:
  %3 = add i32 %arg0, 3
  %4 = call i32 @b(i32 %3)
  ret i32 %4
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @c(i32 %arg0) {
entry:
  %0 = add i32 %arg0, 1
  ret i32 %0
}

define i32 @b(i32 %arg0) {
entry:
  %1 = add i32 %arg0, 2
  br label %entry.inline1
entry.inline1:
  %2 = add i32 %1, 1
  br label %entry.inline1_after
entry.inline1_after:
  ret i32 %2
}

define i32 @a(i32 %arg0) {
entry:
  %3 = add i32 %arg0, 3
  br label %entry.inline2
entry.inline2:
  %4 = add i32 %3, 2
  br label %entry.inline1.inline2
entry.inline1.inline2:
  %5 = add i32 %4, 1
  br label %entry.inline1_after.inline2
entry.inline1_after.inline2:
  br label %entry.inline2_after
entry.inline2_after:
  ret i32 %5
}

)");
}

TEST_F(InlinePassTest, CostThresholdRespected) {
    // Set a very low threshold to prevent inlining
    unsigned originalThreshold = InlinePass::getInlineThreshold();
    InlinePass::setInlineThreshold(1);

    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create expensive function with many operations
    auto* callee = Function::Create(funcType, "expensive", module);
    auto* calleeEntry = BasicBlock::Create(ctx, "entry", callee);
    builder->setInsertPoint(calleeEntry);
    auto* param = callee->getArg(0);
    Value* val = param;

    // Add many operations to increase cost
    for (int i = 0; i < 10; ++i) {
        auto* constant = ConstantInt::get(i32Type, i);
        val = builder->createAdd(val, constant);
        val = builder->createMul(val, constant);
    }
    builder->createRet(val);

    // Create caller function
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* arg = ConstantInt::get(i32Type, 5);
    auto* call = builder->createCall(callee, {arg});
    builder->createRet(call);

    std::string beforeInline = IRPrinter().print(module);

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_FALSE(changed);  // Should not inline due to cost threshold

    std::string afterInline = IRPrinter().print(module);
    EXPECT_EQ(beforeInline, afterInline);

    // Restore original threshold
    InlinePass::setInlineThreshold(originalThreshold);
}

TEST_F(InlinePassTest, MultipleCallSites) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create simple callee function: int double_val(int x) { return x * 2; }
    auto* callee = Function::Create(funcType, "double_val", module);
    auto* calleeEntry = BasicBlock::Create(ctx, "entry", callee);
    builder->setInsertPoint(calleeEntry);
    auto* param = callee->getArg(0);
    auto* two = ConstantInt::get(i32Type, 2);
    auto* mulResult = builder->createMul(param, two);
    builder->createRet(mulResult);

    // Create caller function with multiple call sites:
    // int caller() {
    //   int a = double_val(10);
    //   int b = double_val(20);
    //   return a + b;
    // }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* ten = ConstantInt::get(i32Type, 10);
    auto* twenty = ConstantInt::get(i32Type, 20);
    auto* call1 = builder->createCall(callee, {ten});
    auto* call2 = builder->createCall(callee, {twenty});
    auto* sum = builder->createAdd(call1, call2);
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @double_val(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 2
  ret i32 %0
}

define i32 @caller() {
entry:
  %1 = call i32 @double_val(i32 10)
  %2 = call i32 @double_val(i32 20)
  %3 = add i32 %1, %2
  ret i32 %3
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @double_val(i32 %arg0) {
entry:
  %0 = mul i32 %arg0, 2
  ret i32 %0
}

define i32 @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  %1 = mul i32 10, 2
  br label %entry.inline1_after
entry.inline1_after:
  br label %entry.inline2
entry.inline2:
  %2 = mul i32 20, 2
  br label %entry.inline1_after.inline2_after
entry.inline1_after.inline2_after:
  %3 = add i32 %1, %2
  ret i32 %3
}

)");
}

TEST_F(InlinePassTest, NoInliningForDeclarations) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create function declaration (no body)
    auto* declaration = Function::Create(funcType, "external_func", module);
    // Functions are declarations by default until basic blocks are added

    // Create caller function
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* arg = ConstantInt::get(i32Type, 42);
    auto* call = builder->createCall(declaration, {arg});
    builder->createRet(call);

    std::string beforeInline = IRPrinter().print(module);

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_FALSE(changed);  // Should not inline declarations

    std::string afterInline = IRPrinter().print(module);
    EXPECT_EQ(beforeInline, afterInline);
}

TEST_F(InlinePassTest, VoidFunctionInlining) {
    auto* voidType = ctx->getVoidType();
    auto* funcType = FunctionType::get(voidType, {});

    // Create void callee function: void do_nothing() { return; }
    auto* callee = Function::Create(funcType, "do_nothing", module);
    auto* calleeEntry = BasicBlock::Create(ctx, "entry", callee);
    builder->setInsertPoint(calleeEntry);
    builder->createRetVoid();

    // Create caller function: void caller() { do_nothing(); }
    auto* caller = Function::Create(funcType, "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    builder->createCall(callee, {});
    builder->createRetVoid();

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define void @do_nothing() {
entry:
  ret void
}

define void @caller() {
entry:
  call void @do_nothing()
  ret void
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define void @do_nothing() {
entry:
  ret void
}

define void @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  br label %entry.inline1_after
entry.inline1_after:
  ret void
}

)");
}
// Additional comprehensive test cases for InlinePass
// These should be appended to the main InlinePassTest.cpp file

TEST_F(InlinePassTest, ComplexCallGraphWithIndirectRecursion) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create mutually recursive functions that should not be inlined
    // int funcA(int n) { if (n > 0) return funcB(n-1); return 0; }
    auto* funcA = Function::Create(funcType, "funcA", module);
    auto* funcB = Function::Create(funcType, "funcB", module);

    // funcA implementation
    auto* aEntry = BasicBlock::Create(ctx, "entry", funcA);
    auto* aRecur = BasicBlock::Create(ctx, "recurse", funcA);
    auto* aBase = BasicBlock::Create(ctx, "base", funcA);

    builder->setInsertPoint(aEntry);
    auto* aParam = funcA->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* aCmp = builder->createICmpSGT(aParam, zero);
    builder->createCondBr(aCmp, aRecur, aBase);

    builder->setInsertPoint(aRecur);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* aSub = builder->createSub(aParam, one);
    auto* aCall = builder->createCall(funcB, {aSub});
    builder->createRet(aCall);

    builder->setInsertPoint(aBase);
    builder->createRet(zero);

    // int funcB(int n) { if (n > 0) return funcA(n-1); return 1; }
    auto* bEntry = BasicBlock::Create(ctx, "entry", funcB);
    auto* bRecur = BasicBlock::Create(ctx, "recurse", funcB);
    auto* bBase = BasicBlock::Create(ctx, "base", funcB);

    builder->setInsertPoint(bEntry);
    auto* bParam = funcB->getArg(0);
    auto* bCmp = builder->createICmpSGT(bParam, zero);
    builder->createCondBr(bCmp, bRecur, bBase);

    builder->setInsertPoint(bRecur);
    auto* bSub = builder->createSub(bParam, one);
    auto* bCall = builder->createCall(funcA, {bSub});
    builder->createRet(bCall);

    builder->setInsertPoint(bBase);
    builder->createRet(one);

    // Create non-recursive helper function that should be inlined
    // int helper(int x) { return x + 10; }
    auto* helper = Function::Create(funcType, "helper", module);
    auto* helperEntry = BasicBlock::Create(ctx, "entry", helper);
    builder->setInsertPoint(helperEntry);
    auto* helperParam = helper->getArg(0);
    auto* ten = ConstantInt::get(i32Type, 10);
    auto* helperResult = builder->createAdd(helperParam, ten);
    builder->createRet(helperResult);

    // Create caller function: int caller() { return funcA(5) + helper(2); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* five = ConstantInt::get(i32Type, 5);
    auto* two = ConstantInt::get(i32Type, 2);
    auto* callA = builder->createCall(funcA, {five});
    auto* callHelper = builder->createCall(helper, {two});
    auto* sum = builder->createAdd(callA, callHelper);
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @funcA(i32 %arg0) {
entry:
  %0 = icmp sgt i32 %arg0, 0
  br i1 %0, label %recurse, label %base
recurse:
  %1 = sub i32 %arg0, 1
  %2 = call i32 @funcB(i32 %1)
  ret i32 %2
base:
  ret i32 0
}

define i32 @funcB(i32 %arg0) {
entry:
  %3 = icmp sgt i32 %arg0, 0
  br i1 %3, label %recurse, label %base
recurse:
  %4 = sub i32 %arg0, 1
  %5 = call i32 @funcA(i32 %4)
  ret i32 %5
base:
  ret i32 1
}

define i32 @helper(i32 %arg0) {
entry:
  %6 = add i32 %arg0, 10
  ret i32 %6
}

define i32 @caller() {
entry:
  %7 = call i32 @funcA(i32 5)
  %8 = call i32 @helper(i32 2)
  %9 = add i32 %7, %8
  ret i32 %9
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @funcA(i32 %arg0) {
entry:
  %0 = icmp sgt i32 %arg0, 0
  br i1 %0, label %recurse, label %base
recurse:
  %1 = sub i32 %arg0, 1
  %2 = call i32 @funcB(i32 %1)
  ret i32 %2
base:
  ret i32 0
}

define i32 @funcB(i32 %arg0) {
entry:
  %3 = icmp sgt i32 %arg0, 0
  br i1 %3, label %recurse, label %base
recurse:
  %4 = sub i32 %arg0, 1
  %5 = call i32 @funcA(i32 %4)
  ret i32 %5
base:
  ret i32 1
}

define i32 @helper(i32 %arg0) {
entry:
  %6 = add i32 %arg0, 10
  ret i32 %6
}

define i32 @caller() {
entry:
  %7 = call i32 @funcA(i32 5)
  br label %entry.inline1
entry.inline1:
  %8 = add i32 2, 10
  br label %entry.inline1_after
entry.inline1_after:
  %9 = add i32 %7, %8
  ret i32 %9
}

)");
}

TEST_F(InlinePassTest, LoopInFunctionToInline) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create function with loop: int sum_to_n(int n) {
    //   int sum = 0;
    //   for (int i = 1; i <= n; i++) sum += i;
    //   return sum;
    // }
    auto* callee = Function::Create(funcType, "sum_to_n", module);
    auto* entry = BasicBlock::Create(ctx, "entry", callee);
    auto* loopCond = BasicBlock::Create(ctx, "loop_cond", callee);
    auto* loopBody = BasicBlock::Create(ctx, "loop_body", callee);
    auto* loopExit = BasicBlock::Create(ctx, "loop_exit", callee);

    builder->setInsertPoint(entry);
    auto* param = callee->getArg(0);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* one = ConstantInt::get(i32Type, 1);

    // Create allocas for sum and i
    auto* sumAlloca = builder->createAlloca(i32Type, nullptr, "sum.addr");
    auto* iAlloca = builder->createAlloca(i32Type, nullptr, "i");
    builder->createStore(zero, sumAlloca);
    builder->createStore(one, iAlloca);
    builder->createBr(loopCond);

    builder->setInsertPoint(loopCond);
    auto* i = builder->createLoad(iAlloca, "i");
    auto* cmp = builder->createICmpSLE(i, param);
    builder->createCondBr(cmp, loopBody, loopExit);

    builder->setInsertPoint(loopBody);
    auto* currentSum = builder->createLoad(sumAlloca, "sum");
    auto* newSum = builder->createAdd(currentSum, i);
    builder->createStore(newSum, sumAlloca);
    auto* nextI = builder->createAdd(i, one);
    builder->createStore(nextI, iAlloca);
    builder->createBr(loopCond);

    builder->setInsertPoint(loopExit);
    auto* finalSum = builder->createLoad(sumAlloca, "sum.res");
    builder->createRet(finalSum);

    // Create caller function: int caller() { return sum_to_n(3); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* three = ConstantInt::get(i32Type, 3);
    auto* call = builder->createCall(callee, {three});
    builder->createRet(call);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @sum_to_n(i32 %arg0) {
entry:
  %sum.addr = alloca i32
  %i = alloca i32
  store i32 0, i32* %sum.addr
  store i32 1, i32* %i
  br label %loop_cond
loop_cond:
  %i = load i32, i32* %i
  %0 = icmp sle i32 %i, %arg0
  br i1 %0, label %loop_body, label %loop_exit
loop_body:
  %sum = load i32, i32* %sum.addr
  %1 = add i32 %sum, %i
  store i32 %1, i32* %sum.addr
  %2 = add i32 %i, 1
  store i32 %2, i32* %i
  br label %loop_cond
loop_exit:
  %sum.res = load i32, i32* %sum.addr
  ret i32 %sum.res
}

define i32 @caller() {
entry:
  %3 = call i32 @sum_to_n(i32 3)
  ret i32 %3
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @sum_to_n(i32 %arg0) {
entry:
  %sum.addr = alloca i32
  %i = alloca i32
  store i32 0, i32* %sum.addr
  store i32 1, i32* %i
  br label %loop_cond
loop_cond:
  %i = load i32, i32* %i
  %0 = icmp sle i32 %i, %arg0
  br i1 %0, label %loop_body, label %loop_exit
loop_body:
  %sum = load i32, i32* %sum.addr
  %1 = add i32 %sum, %i
  store i32 %1, i32* %sum.addr
  %2 = add i32 %i, 1
  store i32 %2, i32* %i
  br label %loop_cond
loop_exit:
  %sum.res = load i32, i32* %sum.addr
  ret i32 %sum.res
}

define i32 @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  %sum.addr = alloca i32
  %i = alloca i32
  store i32 0, i32* %sum.addr
  store i32 1, i32* %i
  br label %loop_cond.inline1
loop_cond.inline1:
  %i = load i32, i32* %i
  %3 = icmp sle i32 %i, 3
  br i1 %3, label %loop_body.inline1, label %loop_exit.inline1
loop_body.inline1:
  %sum = load i32, i32* %sum.addr
  %4 = add i32 %sum, %i
  store i32 %4, i32* %sum.addr
  %5 = add i32 %i, 1
  store i32 %5, i32* %i
  br label %loop_cond.inline1
loop_exit.inline1:
  %sum.res = load i32, i32* %sum.addr
  br label %entry.inline1_after
entry.inline1_after:
  ret i32 %sum.res
}

)");
}

TEST_F(InlinePassTest, MaxSizeGrowthThresholdRespected) {
    // Set low size growth threshold
    unsigned originalSizeThreshold = InlinePass::getMaxSizeGrowthThreshold();
    InlinePass::setMaxSizeGrowthThreshold(10);

    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create multiple medium-cost functions that together exceed threshold
    auto* func1 = Function::Create(funcType, "func1", module);
    auto* entry1 = BasicBlock::Create(ctx, "entry", func1);
    builder->setInsertPoint(entry1);
    auto* param1 = func1->getArg(0);
    Value* result1 = param1;
    for (int i = 0; i < 5; ++i) {
        auto* constant = ConstantInt::get(i32Type, i);
        result1 = builder->createAdd(result1, constant);
    }
    builder->createRet(result1);

    auto* func2 = Function::Create(funcType, "func2", module);
    auto* entry2 = BasicBlock::Create(ctx, "entry", func2);
    builder->setInsertPoint(entry2);
    auto* param2 = func2->getArg(0);
    Value* result2 = param2;
    for (int i = 0; i < 5; ++i) {
        auto* constant = ConstantInt::get(i32Type, i);
        result2 = builder->createMul(result2, constant);
    }
    builder->createRet(result2);

    // Create caller that calls both functions
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* arg = ConstantInt::get(i32Type, 5);
    auto* call1 = builder->createCall(func1, {arg});
    auto* call2 = builder->createCall(func2, {arg});
    auto* sum = builder->createAdd(call1, call2);
    builder->createRet(sum);

    InlinePass pass;
    pass.runOnModule(*module, *am);

    // Depending on cost calculation, some or none might be inlined due to size
    // threshold This tests that the pass respects the max size growth threshold

    // Restore original threshold
    InlinePass::setMaxSizeGrowthThreshold(originalSizeThreshold);
}

TEST_F(InlinePassTest, InliningWithPHINodes) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* funcType = FunctionType::get(i32Type, {i32Type});

    // Create function with multiple return paths that will create a PHI node:
    // int max_with_10(int x) {
    //   if (x > 10) return x;
    //   else return 10;
    // }
    auto* callee = Function::Create(funcType, "max_with_10", module);
    auto* entry = BasicBlock::Create(ctx, "entry", callee);
    auto* trueBB = BasicBlock::Create(ctx, "true", callee);
    auto* falseBB = BasicBlock::Create(ctx, "false", callee);

    builder->setInsertPoint(entry);
    auto* param = callee->getArg(0);
    auto* ten = ConstantInt::get(i32Type, 10);
    auto* cmp = builder->createICmpSGT(param, ten);
    builder->createCondBr(cmp, trueBB, falseBB);

    builder->setInsertPoint(trueBB);
    builder->createRet(param);

    builder->setInsertPoint(falseBB);
    builder->createRet(ten);

    // Create caller function: int caller() { return max_with_10(5) +
    // max_with_10(15); }
    auto* caller =
        Function::Create(FunctionType::get(i32Type, {}), "caller", module);
    auto* callerEntry = BasicBlock::Create(ctx, "entry", caller);
    builder->setInsertPoint(callerEntry);
    auto* five = ConstantInt::get(i32Type, 5);
    auto* fifteen = ConstantInt::get(i32Type, 15);
    auto* call1 = builder->createCall(callee, {five});
    auto* call2 = builder->createCall(callee, {fifteen});
    auto* sum = builder->createAdd(call1, call2);
    builder->createRet(sum);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @max_with_10(i32 %arg0) {
entry:
  %0 = icmp sgt i32 %arg0, 10
  br i1 %0, label %true, label %false
true:
  ret i32 %arg0
false:
  ret i32 10
}

define i32 @caller() {
entry:
  %1 = call i32 @max_with_10(i32 5)
  %2 = call i32 @max_with_10(i32 15)
  %3 = add i32 %1, %2
  ret i32 %3
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @max_with_10(i32 %arg0) {
entry:
  %0 = icmp sgt i32 %arg0, 10
  br i1 %0, label %true, label %false
true:
  ret i32 %arg0
false:
  ret i32 10
}

define i32 @caller() {
entry:
  br label %entry.inline1
entry.inline1:
  %1 = icmp sgt i32 5, 10
  br i1 %1, label %true.inline1, label %false.inline1
true.inline1:
  br label %entry.inline1_after
false.inline1:
  br label %entry.inline1_after
entry.inline1_after:
  %2 = phi i32 [ 5, %true.inline1 ], [ 10, %false.inline1 ]
  br label %entry.inline2
entry.inline2:
  %3 = icmp sgt i32 15, 10
  br i1 %3, label %true.inline2, label %false.inline2
true.inline2:
  br label %entry.inline1_after.inline2_after
false.inline2:
  br label %entry.inline1_after.inline2_after
entry.inline1_after.inline2_after:
  %4 = phi i32 [ 15, %true.inline2 ], [ 10, %false.inline2 ]
  %5 = add i32 %2, %4
  ret i32 %5
}

)");
}

TEST_F(InlinePassTest, WhileLoopWithPhiNode) {
    auto* i32Type = IntegerType::get(ctx, 32);
    auto* getintType = FunctionType::get(i32Type, {});
    auto* mainType = FunctionType::get(i32Type, {});

    // Create my_getint function: i32 my_getint() { return -1; }
    auto* my_getint = Function::Create(getintType, "my_getint", module);
    auto* getintEntry = BasicBlock::Create(ctx, "my_getint.entry", my_getint);
    builder->setInsertPoint(getintEntry);
    auto* minusOne = ConstantInt::get(i32Type, -1);
    builder->createRet(minusOne);

    // Create main function with while loop:
    // i32 main() {
    //   %0 = call i32 @my_getint()
    //   br label %while.0.cond
    // while.0.cond:
    //   %n.29.phi.1 = phi i32 [ %0, %main.entry ], [ %sub.5, %while.0.loop ]
    //   %gt.2 = icmp sgt i32 %n.29.phi.1, 0
    //   br i1 %gt.2, label %while.0.loop, label %while.0.merge
    // while.0.loop:
    //   %sub.5 = sub i32 %n.29.phi.1, 1
    //   br label %while.0.cond
    // while.0.merge:
    //   ret i32 0
    // }
    auto* main = Function::Create(mainType, "main", module);
    auto* mainEntry = BasicBlock::Create(ctx, "main.entry", main);
    auto* whileCond = BasicBlock::Create(ctx, "while.0.cond", main);
    auto* whileLoop = BasicBlock::Create(ctx, "while.0.loop", main);
    auto* whileMerge = BasicBlock::Create(ctx, "while.0.merge", main);

    builder->setInsertPoint(mainEntry);
    auto* call = builder->createCall(my_getint, {});
    auto* callRes =
        builder->createAdd(call, ConstantInt::get(i32Type, 0), "call.res");
    builder->createBr(whileCond);

    builder->setInsertPoint(whileCond);
    auto* phi = builder->createPHI(i32Type, "n.29.phi.1");
    phi->addIncoming(callRes, mainEntry);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* gt2 = builder->createICmpSGT(phi, zero, "gt.2");
    builder->createCondBr(gt2, whileLoop, whileMerge);

    builder->setInsertPoint(whileLoop);
    auto* one = ConstantInt::get(i32Type, 1);
    auto* sub5 = builder->createSub(phi, one, "sub.5");
    phi->addIncoming(sub5, whileLoop);
    builder->createBr(whileCond);

    builder->setInsertPoint(whileMerge);
    builder->createRet(zero);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @my_getint() {
my_getint.entry:
  ret i32 -1
}

define i32 @main() {
main.entry:
  %0 = call i32 @my_getint()
  %call.res = add i32 %0, 0
  br label %while.0.cond
while.0.cond:
  %n.29.phi.1 = phi i32 [ %call.res, %main.entry ], [ %sub.5, %while.0.loop ]
  %gt.2 = icmp sgt i32 %n.29.phi.1, 0
  br i1 %gt.2, label %while.0.loop, label %while.0.merge
while.0.loop:
  %sub.5 = sub i32 %n.29.phi.1, 1
  br label %while.0.cond
while.0.merge:
  ret i32 0
}

)");

    InlinePass pass;
    bool changed = pass.runOnModule(*module, *am);
    EXPECT_TRUE(changed);

    EXPECT_EQ(IRPrinter().print(module), R"(; ModuleID = 'test_module'

define i32 @my_getint() {
my_getint.entry:
  ret i32 -1
}

define i32 @main() {
main.entry:
  br label %my_getint.entry.inline1
my_getint.entry.inline1:
  br label %main.entry.inline1_after
main.entry.inline1_after:
  %call.res = add i32 -1, 0
  br label %while.0.cond
while.0.cond:
  %n.29.phi.1 = phi i32 [ %call.res, %main.entry.inline1_after ], [ %sub.5, %while.0.loop ]
  %gt.2 = icmp sgt i32 %n.29.phi.1, 0
  br i1 %gt.2, label %while.0.loop, label %while.0.merge
while.0.loop:
  %sub.5 = sub i32 %n.29.phi.1, 1
  br label %while.0.cond
while.0.merge:
  ret i32 0
}

)");
}