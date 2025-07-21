#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Pass.h"
#include "Pass/Transform/ADCEPass.h"
#include "Pass/Transform/InlinePass.h"
#include "Pass/Transform/InstCombinePass.h"
#include "Pass/Transform/Mem2RegPass.h"
#include "Pass/Transform/SimplifyCFGPass.h"
#include "Pass/Transform/StrengthReductionPass.h"
#include "Pass/Transform/TailRecursionOptimizationPass.h"

using namespace midend;

namespace {

// Test pass implementations
class TestModulePass : public ModulePass {
   public:
    TestModulePass() : ModulePass("TestModulePass", "A test module pass") {}

    bool runOnModule(Module& m, AnalysisManager& am) override {
        executionCount++;
        processedModule = &m;
        (void)am;
        return false;
    }

    static int executionCount;
    static Module* processedModule;
};

int TestModulePass::executionCount = 0;
Module* TestModulePass::processedModule = nullptr;

class TestFunctionPass : public FunctionPass {
   public:
    TestFunctionPass()
        : FunctionPass("TestFunctionPass", "A test function pass") {}

    bool runOnFunction(Function& f, AnalysisManager& am) override {
        executionCount++;
        processedFunctions.push_back(&f);
        (void)am;
        return false;
    }

    static int executionCount;
    static std::vector<Function*> processedFunctions;
};

int TestFunctionPass::executionCount = 0;
std::vector<Function*> TestFunctionPass::processedFunctions;

class TestBasicBlockPass : public BasicBlockPass {
   public:
    TestBasicBlockPass()
        : BasicBlockPass("TestBasicBlockPass", "A test basic block pass") {}

    bool runOnBasicBlock(BasicBlock& bb, AnalysisManager& am) override {
        executionCount++;
        processedBlocks.push_back(&bb);
        (void)am;
        return false;
    }

    static int executionCount;
    static std::vector<BasicBlock*> processedBlocks;
};

int TestBasicBlockPass::executionCount = 0;
std::vector<BasicBlock*> TestBasicBlockPass::processedBlocks;

class PassBuilderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
        builder = std::make_unique<PassBuilder>();

        // Register test passes
        builder->registerPass<TestModulePass>("test_module");
        builder->registerPass<TestFunctionPass>("test_function");
        builder->registerPass<TestBasicBlockPass>("test_bb");
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    std::unique_ptr<PassBuilder> builder;
};

TEST_F(PassBuilderTest, ParsePassPipelinePassManager) {
    PassManager pm;

    // Test successful parsing of single pass
    bool result = builder->parsePassPipeline(pm, "test_module");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm.getNumPasses(), 1u);

    // Test successful parsing of multiple passes
    PassManager pm2;
    result = builder->parsePassPipeline(pm2, "test_module,test_function");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm2.getNumPasses(), 2u);

    // Test parsing with whitespace
    PassManager pm3;
    result = builder->parsePassPipeline(pm3, " test_module , test_function ");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm3.getNumPasses(), 2u);

    // Test parsing with tabs
    PassManager pm4;
    result =
        builder->parsePassPipeline(pm4, "\ttest_module\t,\ttest_function\t");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm4.getNumPasses(), 2u);

    // Test empty pipeline
    PassManager pm5;
    result = builder->parsePassPipeline(pm5, "");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm5.getNumPasses(), 0u);

    // Test failure with unknown pass
    PassManager pm6;
    result = builder->parsePassPipeline(pm6, "unknown_pass");
    EXPECT_FALSE(result);
    EXPECT_EQ(pm6.getNumPasses(), 0u);

    // Test failure with partial unknown pass
    PassManager pm7;
    result = builder->parsePassPipeline(pm7, "test_module,unknown_pass");
    EXPECT_FALSE(result);
    // Note: The first pass gets added before the second one fails, so count is
    // 1
    EXPECT_EQ(pm7.getNumPasses(), 1u);
}

TEST_F(PassBuilderTest, ParsePassPipelineFunctionPassManager) {
    // Create a function
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    FunctionPassManager fpm(func);

    // Test successful parsing of single function pass
    bool result = builder->parsePassPipeline(fpm, "test_function");
    EXPECT_TRUE(result);
    EXPECT_EQ(fpm.getNumPasses(), 1u);

    // Test successful parsing of multiple function passes
    FunctionPassManager fpm2(func);
    result = builder->parsePassPipeline(fpm2, "test_function,test_bb");
    EXPECT_TRUE(result);
    EXPECT_EQ(fpm2.getNumPasses(), 2u);

    // Test parsing with whitespace
    FunctionPassManager fpm3(func);
    result = builder->parsePassPipeline(fpm3, " test_function , test_bb ");
    EXPECT_TRUE(result);
    EXPECT_EQ(fpm3.getNumPasses(), 2u);

    // Test parsing with tabs
    FunctionPassManager fpm4(func);
    result = builder->parsePassPipeline(fpm4, "\ttest_function\t,\ttest_bb\t");
    EXPECT_TRUE(result);
    EXPECT_EQ(fpm4.getNumPasses(), 2u);

    // Test empty pipeline
    FunctionPassManager fpm5(func);
    result = builder->parsePassPipeline(fpm5, "");
    EXPECT_TRUE(result);
    EXPECT_EQ(fpm5.getNumPasses(), 0u);

    // Test failure with unknown pass
    FunctionPassManager fpm6(func);
    result = builder->parsePassPipeline(fpm6, "unknown_pass");
    EXPECT_FALSE(result);
    EXPECT_EQ(fpm6.getNumPasses(), 0u);

    // Test failure with module pass in function pass manager
    FunctionPassManager fpm7(func);
    result = builder->parsePassPipeline(fpm7, "test_module");
    EXPECT_FALSE(result);
    EXPECT_EQ(fpm7.getNumPasses(), 0u);

    // Test failure with partial module pass in function pass manager
    FunctionPassManager fpm8(func);
    result = builder->parsePassPipeline(fpm8, "test_function,test_module");
    EXPECT_FALSE(result);
    // Note: The first pass gets added before the second one fails, so count is
    // 1
    EXPECT_EQ(fpm8.getNumPasses(), 1u);
}

TEST_F(PassBuilderTest, ParsePassPipelineExecution) {
    // Reset counters
    TestModulePass::executionCount = 0;
    TestFunctionPass::executionCount = 0;
    TestBasicBlockPass::executionCount = 0;

    // Create a function with basic blocks
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());
    BasicBlock::Create(context.get(), "entry", func);
    BasicBlock::Create(context.get(), "exit", func);

    // Test PassManager execution
    PassManager pm;
    builder->parsePassPipeline(pm, "test_module,test_function");
    bool result = pm.run(*module);
    EXPECT_FALSE(result);  // Test passes don't modify
    EXPECT_EQ(TestModulePass::executionCount, 1);
    EXPECT_EQ(TestFunctionPass::executionCount, 1);  // One function in module

    // Test FunctionPassManager execution
    TestFunctionPass::executionCount = 0;
    TestBasicBlockPass::executionCount = 0;

    FunctionPassManager fpm(func);
    builder->parsePassPipeline(fpm, "test_function,test_bb");
    result = fpm.run();
    EXPECT_FALSE(result);  // Test passes don't modify
    EXPECT_EQ(TestFunctionPass::executionCount, 1);
    EXPECT_EQ(TestBasicBlockPass::executionCount, 2);  // Two basic blocks
}

TEST_F(PassBuilderTest, ParsePassPipelineEdgeCases) {
    PassManager pm;

    // Test comma only - empty string after trimming will fail
    bool result = builder->parsePassPipeline(pm, ",");
    EXPECT_FALSE(result);
    EXPECT_EQ(pm.getNumPasses(), 0u);

    // Test multiple commas - empty string in middle will fail
    PassManager pm2;
    result = builder->parsePassPipeline(pm2, "test_module,,test_function");
    EXPECT_FALSE(result);
    EXPECT_EQ(pm2.getNumPasses(), 1u);

    // Test trailing comma - empty string after comma will fail
    PassManager pm3;
    result = builder->parsePassPipeline(pm3, "test_module,");
    EXPECT_TRUE(
        result);  // Actually works because there's no content after the comma
    EXPECT_EQ(pm3.getNumPasses(), 1u);

    // Test leading comma - empty string before comma will fail
    PassManager pm4;
    result = builder->parsePassPipeline(pm4, ",test_module");
    EXPECT_FALSE(result);
    EXPECT_EQ(pm4.getNumPasses(), 0u);

    // Test only whitespace - empty string after trimming will fail
    PassManager pm5;
    result = builder->parsePassPipeline(pm5, "   ");
    EXPECT_FALSE(result);
    EXPECT_EQ(pm5.getNumPasses(), 0u);

    // Test whitespace around commas - this should work fine
    PassManager pm6;
    result = builder->parsePassPipeline(pm6, "test_module  ,  test_function");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm6.getNumPasses(), 2u);
}

TEST_F(PassBuilderTest, ParsePassPipelineStringManipulation) {
    PassManager pm;

    // Test string with leading and trailing whitespace
    bool result = builder->parsePassPipeline(pm, "  test_module  ");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm.getNumPasses(), 1u);

    // Test string with mixed whitespace
    PassManager pm2;
    result = builder->parsePassPipeline(pm2, " \t test_module \t ");
    EXPECT_TRUE(result);
    EXPECT_EQ(pm2.getNumPasses(), 1u);

    // Test complex whitespace scenario - the trailing empty string will fail
    PassManager pm3;
    result =
        builder->parsePassPipeline(pm3, "  test_module  ,  test_function  ,  ");
    EXPECT_FALSE(result);
    EXPECT_EQ(pm3.getNumPasses(), 2u);
}

TEST_F(PassBuilderTest, PassPipelineExecution_QuickPow) {
    Context context;
    Module module("qpow_module", &context);
    auto* int32Ty = context.getInt32Type();
    auto* qpowFnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty, int32Ty});
    auto* qpowFn = Function::Create(qpowFnTy, "qpow", &module);
    auto* qpowArg0 = qpowFn->getArg(0);
    auto* qpowArg1 = qpowFn->getArg(1);
    auto* qpowArg2 = qpowFn->getArg(2);
    qpowArg0->setName("base");
    qpowArg1->setName("exp");
    qpowArg2->setName("acc");

    // Create qpow function body
    auto* qpowEntry = BasicBlock::Create(&context, "entry", qpowFn);
    auto* qpowBaseCase = BasicBlock::Create(&context, "base_case", qpowFn);
    auto* qpowCheckOdd = BasicBlock::Create(&context, "check_odd", qpowFn);
    auto* qpowOddCase = BasicBlock::Create(&context, "odd_case", qpowFn);
    auto* qpowEvenCase = BasicBlock::Create(&context, "even_case", qpowFn);

    IRBuilder qpowBuilder(qpowEntry);

    // Constants
    auto* zero = ConstantInt::get(int32Ty, 0);
    auto* one = ConstantInt::get(int32Ty, 1);
    auto* two = ConstantInt::get(int32Ty, 2);

    // Entry block: check if exp == 0
    auto* cmpExp = qpowBuilder.createICmpEQ(qpowArg1, zero, "cmp_exp_zero");
    qpowBuilder.createCondBr(cmpExp, qpowBaseCase, qpowCheckOdd);

    // Base case: return acc
    qpowBuilder.setInsertPoint(qpowBaseCase);
    qpowBuilder.createRet(qpowArg2);

    // Check if exp is odd
    qpowBuilder.setInsertPoint(qpowCheckOdd);
    auto* expMod2 = qpowBuilder.createRem(qpowArg1, two, "exp_mod_2");
    auto* isOdd = qpowBuilder.createICmpEQ(expMod2, one, "is_odd");
    qpowBuilder.createCondBr(isOdd, qpowOddCase, qpowEvenCase);

    // Odd case: qpow(base * base, exp / 2, acc * base)
    qpowBuilder.setInsertPoint(qpowOddCase);
    auto* newBase1 = qpowBuilder.createMul(qpowArg0, qpowArg0, "new_base_odd");
    auto* newExp1 = qpowBuilder.createDiv(qpowArg1, two, "new_exp_odd");
    auto* newAcc1 = qpowBuilder.createMul(qpowArg2, qpowArg0, "new_acc");
    auto* tailCall1 = qpowBuilder.createCall(
        qpowFn, {newBase1, newExp1, newAcc1}, "tail_call_odd");
    qpowBuilder.createRet(tailCall1);

    // Even case: qpow(base * base, exp / 2, acc)
    qpowBuilder.setInsertPoint(qpowEvenCase);
    auto* newBase2 = qpowBuilder.createMul(qpowArg0, qpowArg0, "new_base_even");
    auto* newExp2 = qpowBuilder.createDiv(qpowArg1, two, "new_exp_even");
    auto* tailCall2 = qpowBuilder.createCall(
        qpowFn, {newBase2, newExp2, qpowArg2}, "tail_call_even");
    qpowBuilder.createRet(tailCall2);

    // Create main function: i32 main()
    auto* mainFnTy = FunctionType::get(int32Ty, {});
    auto* mainFn = Function::Create(mainFnTy, "main", &module);
    auto* mainEntry = BasicBlock::Create(&context, "entry", mainFn);

    IRBuilder mainBuilder(mainEntry);

    // Allocate variables
    auto* basePtr = mainBuilder.createAlloca(int32Ty, nullptr, "base_main");
    auto* expPtr = mainBuilder.createAlloca(int32Ty, nullptr, "exp_main");
    auto* resultPtr = mainBuilder.createAlloca(int32Ty, nullptr, "result_main");

    // Initialize: base = 3, exp = 4 (using unfolded constants)
    auto* baseVal = mainBuilder.createAdd(two, one, "base_val");  // 2 + 1 = 3
    auto* expVal = mainBuilder.createMul(two, two, "exp_val");    // 2 * 2 = 4

    mainBuilder.createStore(baseVal, basePtr);
    mainBuilder.createStore(expVal, expPtr);

    // Load values for function call
    auto* baseArg = mainBuilder.createLoad(basePtr, "base_arg");
    auto* expArg = mainBuilder.createLoad(expPtr, "exp_arg");

    // Call qpow function
    auto* callResult = mainBuilder.createCall(
        qpowFn, {baseArg, expArg, ConstantInt::get(int32Ty, 1)}, "qpow_result");
    mainBuilder.createStore(callResult, resultPtr);

    // Dead code: unused calculation
    mainBuilder.createSub(callResult, callResult, "dead_main");

    // Return result
    auto* retVal = mainBuilder.createLoad(resultPtr, "ret_val");
    mainBuilder.createRet(retVal);

    EXPECT_EQ(IRPrinter().print(&module), R"(; ModuleID = 'qpow_module'

define i32 @qpow(i32 %base, i32 %exp, i32 %acc) {
entry:
  %cmp_exp_zero = icmp eq i32 %exp, 0
  br i1 %cmp_exp_zero, label %base_case, label %check_odd
base_case:
  ret i32 %acc
check_odd:
  %exp_mod_2 = srem i32 %exp, 2
  %is_odd = icmp eq i32 %exp_mod_2, 1
  br i1 %is_odd, label %odd_case, label %even_case
odd_case:
  %new_base_odd = mul i32 %base, %base
  %new_exp_odd = sdiv i32 %exp, 2
  %new_acc = mul i32 %acc, %base
  %tail_call_odd = call i32 @qpow(i32 %new_base_odd, i32 %new_exp_odd, i32 %new_acc)
  ret i32 %tail_call_odd
even_case:
  %new_base_even = mul i32 %base, %base
  %new_exp_even = sdiv i32 %exp, 2
  %tail_call_even = call i32 @qpow(i32 %new_base_even, i32 %new_exp_even, i32 %acc)
  ret i32 %tail_call_even
}

define i32 @main() {
entry:
  %base_main = alloca i32
  %exp_main = alloca i32
  %result_main = alloca i32
  %base_val = add i32 2, 1
  %exp_val = mul i32 2, 2
  store i32 %base_val, i32* %base_main
  store i32 %exp_val, i32* %exp_main
  %base_arg = load i32, i32* %base_main
  %exp_arg = load i32, i32* %exp_main
  %qpow_result = call i32 @qpow(i32 %base_arg, i32 %exp_arg, i32 1)
  store i32 %qpow_result, i32* %result_main
  %dead_main = sub i32 %qpow_result, %qpow_result
  %ret_val = load i32, i32* %result_main
  ret i32 %ret_val
}

)");

    PassManager passManager;
    auto& am = passManager.getAnalysisManager();
    am.registerAnalysisType<DominanceAnalysis>();
    am.registerAnalysisType<PostDominanceAnalysis>();
    am.registerAnalysisType<CallGraphAnalysis>();
    PassBuilder passBuilder;
    InlinePass::setInlineThreshold(1000);
    InlinePass::setMaxSizeGrowthThreshold(1000);
    passBuilder.registerPass<Mem2RegPass>("mem2reg");
    passBuilder.registerPass<InlinePass>("inline");
    passBuilder.registerPass<ADCEPass>("adce");
    passBuilder.registerPass<InstCombinePass>("instcombine");
    passBuilder.registerPass<SimplifyCFGPass>("simplifycfg");
    passBuilder.registerPass<TailRecursionOptimizationPass>("tailrec");
    passBuilder.registerPass<StrengthReductionPass>("strengthreduce");
    passBuilder.parsePassPipeline(
        passManager,
        "mem2reg,tailrec,adce,simplifycfg,inline,"
        "instcombine,strengthreduce,adce,simplifycfg");
    bool modified = passManager.run(module);
    EXPECT_TRUE(modified);
    std::cout << IRPrinter().print(&module) << std::endl;
}

}  // namespace