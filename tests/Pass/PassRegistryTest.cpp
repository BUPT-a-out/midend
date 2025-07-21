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

// Test pass for registry testing
class TestPass : public ModulePass {
   public:
    TestPass() : ModulePass("TestPass", "A test pass for registry testing") {}

    bool runOnModule(Module&, AnalysisManager&) override {
        return false;  // No modification
    }
};

class AnotherTestPass : public ModulePass {
   public:
    AnotherTestPass() : ModulePass("AnotherTestPass", "Another test pass") {}

    bool runOnModule(Module&, AnalysisManager&) override {
        return true;  // Modification
    }
};

class ThirdTestPass : public ModulePass {
   public:
    ThirdTestPass()
        : ModulePass("ThirdTestPass", "Third test pass for pipeline") {}

    bool runOnModule(Module&, AnalysisManager&) override {
        return false;  // No modification
    }
};

class PassRegistryTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Note: PassRegistry is a singleton, so we need to be careful with
        // state For testing, we'll work with a fresh registry state
    }

    void TearDown() override {
        // Clean up any registered passes if needed
    }
};

TEST_F(PassRegistryTest, SingletonAccess) {
    PassRegistry& registry1 = PassRegistry::getInstance();
    PassRegistry& registry2 = PassRegistry::getInstance();

    // Should be the same instance
    EXPECT_EQ(&registry1, &registry2);
}

TEST_F(PassRegistryTest, PassRegistration) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register a pass using template method
    registry.registerPass<TestPass>("test_pass");

    // Check if pass is registered
    EXPECT_TRUE(registry.hasPass("test_pass"));
    EXPECT_FALSE(registry.hasPass("nonexistent_pass"));
}

TEST_F(PassRegistryTest, PassCreation) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register a pass
    registry.registerPass<TestPass>("test_pass");

    // Create the pass
    auto pass = registry.createPass("test_pass");
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->getName(), "TestPass");

    // Try to create non-existent pass
    auto nullPass = registry.createPass("nonexistent");
    EXPECT_EQ(nullPass, nullptr);
}

TEST_F(PassRegistryTest, ManualPassRegistration) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register pass using function constructor
    registry.registerPass("manual_test",
                          []() { return std::make_unique<AnotherTestPass>(); });

    EXPECT_TRUE(registry.hasPass("manual_test"));

    auto pass = registry.createPass("manual_test");
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->getName(), "AnotherTestPass");
}

TEST_F(PassRegistryTest, MultiplePassRegistration) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register multiple passes
    registry.registerPass<TestPass>("pass1");
    registry.registerPass<AnotherTestPass>("pass2");

    EXPECT_TRUE(registry.hasPass("pass1"));
    EXPECT_TRUE(registry.hasPass("pass2"));

    auto pass1 = registry.createPass("pass1");
    auto pass2 = registry.createPass("pass2");

    EXPECT_NE(pass1, nullptr);
    EXPECT_NE(pass2, nullptr);
    EXPECT_EQ(pass1->getName(), "TestPass");
    EXPECT_EQ(pass2->getName(), "AnotherTestPass");
}

TEST_F(PassRegistryTest, GetRegisteredPasses) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Clear any existing passes by getting current state
    auto initialPasses = registry.getRegisteredPasses();
    size_t initialCount = initialPasses.size();

    // Register some passes
    registry.registerPass<TestPass>("list_test1");
    registry.registerPass<AnotherTestPass>("list_test2");

    auto passes = registry.getRegisteredPasses();
    EXPECT_EQ(passes.size(), initialCount + 2);

    // Check that our passes are in the list
    bool foundTest1 = false, foundTest2 = false;
    for (const auto& name : passes) {
        if (name == "list_test1") foundTest1 = true;
        if (name == "list_test2") foundTest2 = true;
    }

    EXPECT_TRUE(foundTest1);
    EXPECT_TRUE(foundTest2);
}

TEST_F(PassRegistryTest, PassReplacement) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register a pass
    registry.registerPass<TestPass>("replaceable");
    auto pass1 = registry.createPass("replaceable");
    EXPECT_EQ(pass1->getName(), "TestPass");

    // Register another pass with the same name (should replace)
    registry.registerPass<AnotherTestPass>("replaceable");
    auto pass2 = registry.createPass("replaceable");
    EXPECT_EQ(pass2->getName(), "AnotherTestPass");
}

TEST_F(PassRegistryTest, MultiplePassPipeline) {
    PassRegistry& registry = PassRegistry::getInstance();

    // Register multiple passes
    registry.registerPass<TestPass>("pipeline_pass1");
    registry.registerPass<AnotherTestPass>("pipeline_pass2");
    registry.registerPass<ThirdTestPass>("pipeline_pass3");

    // Verify passes are registered
    EXPECT_TRUE(registry.hasPass("pipeline_pass1"));
    EXPECT_TRUE(registry.hasPass("pipeline_pass2"));
    EXPECT_TRUE(registry.hasPass("pipeline_pass3"));

    // Create a module for testing
    Context context;
    Module module("test_module", &context);

    // Create a PassManager and add passes from registry
    PassManager passManager;
    auto pass1 = registry.createPass("pipeline_pass1");
    auto pass2 = registry.createPass("pipeline_pass2");
    auto pass3 = registry.createPass("pipeline_pass3");

    EXPECT_NE(pass1, nullptr);
    EXPECT_NE(pass2, nullptr);
    EXPECT_NE(pass3, nullptr);

    passManager.addPass(std::move(pass1));
    passManager.addPass(std::move(pass2));
    passManager.addPass(std::move(pass3));

    // Verify the correct number of passes were added
    EXPECT_EQ(passManager.getNumPasses(), 3);

    // Run the pass pipeline on the module
    bool result = passManager.run(module);

    // The pipeline should indicate modification since AnotherTestPass returns
    // true
    EXPECT_TRUE(result);
}

TEST_F(PassRegistryTest, PassPipelineExecution_QuickPow) {
    Context context;
    Module module("qpow_module", &context);

    // Create qpow function: i32 qpow(i32 base, i32 exp)
    auto* int32Ty = context.getInt32Type();
    auto* qpowFnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* qpowFn = Function::Create(qpowFnTy, "qpow", &module);
    auto* qpowArg0 = qpowFn->getArg(0);
    auto* qpowArg1 = qpowFn->getArg(1);
    qpowArg0->setName("base");
    qpowArg1->setName("exp");

    // Create qpow function body
    auto* qpowEntry = BasicBlock::Create(&context, "entry", qpowFn);
    auto* qpowLoop = BasicBlock::Create(&context, "loop", qpowFn);
    auto* qpowLoopBody = BasicBlock::Create(&context, "loop_body", qpowFn);
    auto* qpowOddCase = BasicBlock::Create(&context, "odd_case", qpowFn);
    auto* qpowEvenCase = BasicBlock::Create(&context, "even_case", qpowFn);
    auto* qpowLoopEnd = BasicBlock::Create(&context, "loop_end", qpowFn);
    auto* qpowExit = BasicBlock::Create(&context, "exit", qpowFn);

    IRBuilder qpowBuilder(qpowEntry);

    // Entry block: allocate variables and initialize
    auto* resultAlloca =
        qpowBuilder.createAlloca(int32Ty, nullptr, "result_ptr");
    auto* baseAlloca = qpowBuilder.createAlloca(int32Ty, nullptr, "base_ptr");
    auto* expAlloca = qpowBuilder.createAlloca(int32Ty, nullptr, "exp_ptr");
    auto* deadVar =
        qpowBuilder.createAlloca(int32Ty, nullptr, "dead_var");  // Dead code

    // Initialize result = 1
    auto* one = ConstantInt::get(int32Ty, 1);
    auto* zero = ConstantInt::get(int32Ty, 0);
    auto* constTwo = ConstantInt::get(int32Ty, 2);
    auto* constThree = ConstantInt::get(int32Ty, 3);

    // Unfolded constant: 1 + 0 instead of direct 1
    auto* initOne = qpowBuilder.createAdd(one, zero, "init_one");
    qpowBuilder.createStore(initOne, resultAlloca);
    qpowBuilder.createStore(qpowArg0, baseAlloca);
    qpowBuilder.createStore(qpowArg1, expAlloca);

    // Dead code: store unused value
    auto* deadCalc = qpowBuilder.createMul(constTwo, constThree, "dead_calc");
    qpowBuilder.createStore(deadCalc, deadVar);

    qpowBuilder.createBr(qpowLoop);

    // Loop condition: while (exp > 0)
    qpowBuilder.setInsertPoint(qpowLoop);
    auto* expLoad = qpowBuilder.createLoad(expAlloca, "exp_val");
    auto* cond = qpowBuilder.createICmpSGT(expLoad, zero, "loop_cond");
    qpowBuilder.createCondBr(cond, qpowLoopBody, qpowExit);

    // Loop body
    qpowBuilder.setInsertPoint(qpowLoopBody);
    auto* expLoadBody = qpowBuilder.createLoad(expAlloca, "exp_body");
    auto* resultLoadBody = qpowBuilder.createLoad(resultAlloca, "result_body");
    auto* baseLoadBody = qpowBuilder.createLoad(baseAlloca, "base_body");

    // Check if exp is odd: if (exp & 1)
    // Using modulo instead of bitwise for optimization opportunity
    auto* expMod = qpowBuilder.createRem(expLoadBody, constTwo, "exp_mod");
    auto* isOdd = qpowBuilder.createICmpEQ(expMod, one, "is_odd");
    qpowBuilder.createCondBr(isOdd, qpowOddCase, qpowEvenCase);

    // Odd case: result *= base
    qpowBuilder.setInsertPoint(qpowOddCase);
    auto* mulResult =
        qpowBuilder.createMul(resultLoadBody, baseLoadBody, "mul_result");
    qpowBuilder.createStore(mulResult, resultAlloca);
    qpowBuilder.createBr(qpowLoopEnd);

    // Even case: do nothing with result
    qpowBuilder.setInsertPoint(qpowEvenCase);
    // Dead code: unnecessary addition
    qpowBuilder.createAdd(zero, zero, "dead_add");
    qpowBuilder.createBr(qpowLoopEnd);

    // Loop end: square base and divide exp
    qpowBuilder.setInsertPoint(qpowLoopEnd);
    auto* baseLoadEnd = qpowBuilder.createLoad(baseAlloca, "base_end");
    auto* expLoadEnd = qpowBuilder.createLoad(expAlloca, "exp_end");

    // base *= base (square the base)
    auto* newBase = qpowBuilder.createMul(baseLoadEnd, baseLoadEnd, "new_base");
    qpowBuilder.createStore(newBase, baseAlloca);

    // exp /= 2 (using division instead of shift for optimization)
    auto* newExp = qpowBuilder.createDiv(expLoadEnd, constTwo, "new_exp");
    qpowBuilder.createStore(newExp, expAlloca);

    qpowBuilder.createBr(qpowLoop);

    // Exit block
    qpowBuilder.setInsertPoint(qpowExit);
    auto* finalResult = qpowBuilder.createLoad(resultAlloca, "final_result");
    qpowBuilder.createRet(finalResult);

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
    auto* baseVal =
        mainBuilder.createAdd(constTwo, one, "base_val");  // 2 + 1 = 3
    auto* expVal =
        mainBuilder.createMul(constTwo, constTwo, "exp_val");  // 2 * 2 = 4

    mainBuilder.createStore(baseVal, basePtr);
    mainBuilder.createStore(expVal, expPtr);

    // Load values for function call
    auto* baseArg = mainBuilder.createLoad(basePtr, "base_arg");
    auto* expArg = mainBuilder.createLoad(expPtr, "exp_arg");

    // Call qpow function
    auto* callResult =
        mainBuilder.createCall(qpowFn, {baseArg, expArg}, "qpow_result");
    mainBuilder.createStore(callResult, resultPtr);

    // Dead code: unused calculation
    mainBuilder.createSub(callResult, callResult, "dead_main");

    // Return result
    auto* retVal = mainBuilder.createLoad(resultPtr, "ret_val");
    mainBuilder.createRet(retVal);

    EXPECT_EQ(IRPrinter().print(&module), R"(; ModuleID = 'qpow_module'

define i32 @qpow(i32 %base, i32 %exp) {
entry:
  %result_ptr = alloca i32
  %base_ptr = alloca i32
  %exp_ptr = alloca i32
  %dead_var = alloca i32
  %init_one = add i32 1, 0
  store i32 %init_one, i32* %result_ptr
  store i32 %base, i32* %base_ptr
  store i32 %exp, i32* %exp_ptr
  %dead_calc = mul i32 2, 3
  store i32 %dead_calc, i32* %dead_var
  br label %loop
loop:
  %exp_val = load i32, i32* %exp_ptr
  %loop_cond = icmp sgt i32 %exp_val, 0
  br i1 %loop_cond, label %loop_body, label %exit
loop_body:
  %exp_body = load i32, i32* %exp_ptr
  %result_body = load i32, i32* %result_ptr
  %base_body = load i32, i32* %base_ptr
  %exp_mod = srem i32 %exp_body, 2
  %is_odd = icmp eq i32 %exp_mod, 1
  br i1 %is_odd, label %odd_case, label %even_case
odd_case:
  %mul_result = mul i32 %result_body, %base_body
  store i32 %mul_result, i32* %result_ptr
  br label %loop_end
even_case:
  %dead_add = add i32 0, 0
  br label %loop_end
loop_end:
  %base_end = load i32, i32* %base_ptr
  %exp_end = load i32, i32* %exp_ptr
  %new_base = mul i32 %base_end, %base_end
  store i32 %new_base, i32* %base_ptr
  %new_exp = sdiv i32 %exp_end, 2
  store i32 %new_exp, i32* %exp_ptr
  br label %loop
exit:
  %final_result = load i32, i32* %result_ptr
  ret i32 %final_result
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
  %qpow_result = call i32 @qpow(i32 %base_arg, i32 %exp_arg)
  store i32 %qpow_result, i32* %result_main
  %dead_main = sub i32 %qpow_result, %qpow_result
  %ret_val = load i32, i32* %result_main
  ret i32 %ret_val
}

)");

    // Set up pass pipeline
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
        "mem2reg,adce,inline,adce,simplifycfg,adce,"
        "instcombine,tailrec,strengthreduce,adce,simplifycfg");
    bool modified = passManager.run(module);
    EXPECT_TRUE(modified);
    std::cout << IRPrinter().print(&module) << std::endl;
}
}  // namespace
