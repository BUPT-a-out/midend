#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"

using namespace midend;

namespace {

class PassManagerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
};

// Test pass implementations
class CounterModulePass : public ModulePass {
   public:
    CounterModulePass()
        : ModulePass("CounterModulePass", "Counts module executions") {}

    bool runOnModule(Module& m, AnalysisManager& am) override {
        executionCount++;
        lastModule = &m;
        (void)am;
        return false;
    }

    static int executionCount;
    static Module* lastModule;
};

int CounterModulePass::executionCount = 0;
Module* CounterModulePass::lastModule = nullptr;

class CounterFunctionPass : public FunctionPass {
   public:
    CounterFunctionPass()
        : FunctionPass("CounterFunctionPass", "Counts function executions") {}

    bool runOnFunction(Function& f, AnalysisManager& am) override {
        executionCount++;
        processedFunctions.push_back(&f);
        (void)am;
        return false;
    }

    static int executionCount;
    static std::vector<Function*> processedFunctions;
};

int CounterFunctionPass::executionCount = 0;
std::vector<Function*> CounterFunctionPass::processedFunctions;

TEST_F(PassManagerTest, BasicPassManagerUsage) {
    PassManager pm;

    EXPECT_EQ(pm.getNumPasses(), 0u);

    pm.addPass(std::make_unique<CounterModulePass>());
    EXPECT_EQ(pm.getNumPasses(), 1u);

    pm.addPass(std::make_unique<CounterModulePass>());
    EXPECT_EQ(pm.getNumPasses(), 2u);
}

TEST_F(PassManagerTest, PassExecution) {
    CounterModulePass::executionCount = 0;
    CounterModulePass::lastModule = nullptr;

    PassManager pm;
    pm.addPass(std::make_unique<CounterModulePass>());
    pm.addPass(std::make_unique<CounterModulePass>());

    bool result = pm.run(*module);

    EXPECT_FALSE(result);  // Our test passes don't modify the module
    EXPECT_EQ(CounterModulePass::executionCount, 2);
    EXPECT_EQ(CounterModulePass::lastModule, module.get());
}

TEST_F(PassManagerTest, TemplateAddPass) {
    CounterModulePass::executionCount = 0;

    PassManager pm;
    pm.addPass<CounterModulePass>();
    pm.addPass<CounterModulePass>();

    EXPECT_EQ(pm.getNumPasses(), 2u);

    bool result = pm.run(*module);
    EXPECT_FALSE(result);
    EXPECT_EQ(CounterModulePass::executionCount, 2);
}

TEST_F(PassManagerTest, AnalysisManagerAccess) {
    PassManager pm;

    AnalysisManager& am = pm.getAnalysisManager();
    const AnalysisManager& constAm =
        static_cast<const PassManager&>(pm).getAnalysisManager();

    EXPECT_EQ(&am, &constAm);
}

TEST_F(PassManagerTest, PassManagerClear) {
    CounterModulePass::executionCount = 0;

    PassManager pm;
    pm.addPass<CounterModulePass>();
    pm.addPass<CounterModulePass>();

    EXPECT_EQ(pm.getNumPasses(), 2u);

    pm.clear();
    EXPECT_EQ(pm.getNumPasses(), 0u);

    bool result = pm.run(*module);
    EXPECT_FALSE(result);
    EXPECT_EQ(CounterModulePass::executionCount,
              0);  // No passes should have run
}

TEST_F(PassManagerTest, FunctionPassManager) {
    // Create a function
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    CounterFunctionPass::executionCount = 0;
    CounterFunctionPass::processedFunctions.clear();

    FunctionPassManager fpm(func);
    fpm.addPass<CounterFunctionPass>();
    fpm.addPass<CounterFunctionPass>();

    EXPECT_EQ(fpm.getNumPasses(), 2u);

    bool result = fpm.run();
    EXPECT_FALSE(result);
    EXPECT_EQ(CounterFunctionPass::executionCount, 2);
    EXPECT_EQ(CounterFunctionPass::processedFunctions.size(), 2u);
    EXPECT_EQ(CounterFunctionPass::processedFunctions[0], func);
    EXPECT_EQ(CounterFunctionPass::processedFunctions[1], func);
}

TEST_F(PassManagerTest, FunctionPassManagerWithExplicitFunction) {
    // Create two functions
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func1 = Function::Create(fnTy, "function1", module.get());
    auto* func2 = Function::Create(fnTy, "function2", module.get());

    CounterFunctionPass::executionCount = 0;
    CounterFunctionPass::processedFunctions.clear();

    FunctionPassManager fpm(func1);
    fpm.addPass<CounterFunctionPass>();

    // Run on a different function than the one used in constructor
    bool result = fpm.run(*func2);
    EXPECT_FALSE(result);
    EXPECT_EQ(CounterFunctionPass::executionCount, 1);
    EXPECT_EQ(CounterFunctionPass::processedFunctions.size(), 1u);
    EXPECT_EQ(CounterFunctionPass::processedFunctions[0], func2);
}

TEST_F(PassManagerTest, FunctionPassManagerClear) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    CounterFunctionPass::executionCount = 0;

    FunctionPassManager fpm(func);
    fpm.addPass<CounterFunctionPass>();
    fpm.addPass<CounterFunctionPass>();

    EXPECT_EQ(fpm.getNumPasses(), 2u);

    fpm.clear();
    EXPECT_EQ(fpm.getNumPasses(), 0u);

    bool result = fpm.run();
    EXPECT_FALSE(result);
    EXPECT_EQ(CounterFunctionPass::executionCount, 0);
}

TEST_F(PassManagerTest, PassBuilder) {
    PassBuilder builder;

    // Register passes
    builder.registerPass<CounterModulePass>("module_counter");
    builder.registerPass<CounterFunctionPass>("function_counter");

    // Create passes
    auto modulePass = builder.createPass("module_counter");
    auto functionPass = builder.createPass("function_counter");
    auto nonexistentPass = builder.createPass("nonexistent");

    EXPECT_NE(modulePass, nullptr);
    EXPECT_NE(functionPass, nullptr);
    EXPECT_EQ(nonexistentPass, nullptr);

    // Test pass types
    EXPECT_EQ(modulePass->getKind(), Pass::ModulePass);
    EXPECT_EQ(functionPass->getKind(), Pass::FunctionPass);
}

TEST_F(PassManagerTest, PassBuilderWithLambda) {
    PassBuilder builder;

    builder.registerPass("custom_module_pass", []() {
        return std::make_unique<CounterModulePass>();
    });

    auto pass = builder.createPass("custom_module_pass");
    EXPECT_NE(pass, nullptr);
    EXPECT_EQ(pass->getKind(), Pass::ModulePass);
}

class ModifyingPass : public ModulePass {
   public:
    ModifyingPass()
        : ModulePass("ModifyingPass", "A pass that modifies the module") {}

    bool runOnModule(Module& m, AnalysisManager& am) override {
        (void)m;
        (void)am;
        return true;  // Indicate that the module was modified
    }
};

TEST_F(PassManagerTest, PassModificationDetection) {
    PassManager pm;
    pm.addPass<ModifyingPass>();
    pm.addPass<CounterModulePass>();  // This one doesn't modify

    bool result = pm.run(*module);
    EXPECT_TRUE(result);  // At least one pass modified the module
}

// Test analysis result for testing analysis requirement checking
class TestAnalysisResult : public AnalysisResult {
   public:
    TestAnalysisResult(int value) : value_(value) {}
    int getValue() const { return value_; }

   private:
    int value_;
};

// Test pass that requires an analysis
class RequiringPass : public ModulePass {
   public:
    RequiringPass()
        : ModulePass("RequiringPass", "A pass that requires an analysis") {}

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        required.insert("TestAnalysis");
        preserved.insert("TestAnalysis");
    }

    bool runOnModule(Module& m, AnalysisManager& am) override {
        auto* analysis = am.getAnalysis<TestAnalysisResult>("TestAnalysis", m);
        if (analysis) {
            lastAnalysisValue = analysis->getValue();
            return false;
        }
        return false;
    }

    static int lastAnalysisValue;
};

int RequiringPass::lastAnalysisValue = -1;

TEST_F(PassManagerTest, AnalysisRequirementChecking) {
    RequiringPass::lastAnalysisValue = -1;

    PassManager pm;
    pm.addPass<RequiringPass>();

    // Run without providing the required analysis - should fail
    std::cout << "Running pass without required analysis..." << std::endl;
    bool result = pm.run(*module);
    EXPECT_FALSE(result);  // Pass should fail due to missing analysis
    EXPECT_EQ(RequiringPass::lastAnalysisValue,
              -1);  // Pass should not have run

    // Now provide the required analysis and run again
    pm.getAnalysisManager().registerAnalysis<TestAnalysisResult>(
        "TestAnalysis", *module, std::make_unique<TestAnalysisResult>(42));

    result = pm.run(*module);
    EXPECT_FALSE(result);  // Pass doesn't modify the module, but it should run
                           // successfully
    EXPECT_EQ(RequiringPass::lastAnalysisValue,
              42);  // Pass should have accessed the analysis
}

// Test pass that modifies the module and does not preserve all analyses
class ModifyingPassNoPreserve : public ModulePass {
   public:
    ModifyingPassNoPreserve()
        : ModulePass("ModifyingPassNoPreserve",
                     "A pass that modifies and doesn't preserve analyses") {}

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        // This pass doesn't preserve any analyses
        (void)required;
        (void)preserved;
    }

    bool runOnModule(Module& m, AnalysisManager& am) override {
        (void)m;
        (void)am;
        return true;  // Indicate that the module was modified
    }
};

TEST_F(PassManagerTest, AnalysisInvalidation) {
    PassManager pm;

    // Register an analysis
    pm.getAnalysisManager().registerAnalysis<TestAnalysisResult>(
        "TestAnalysis", *module, std::make_unique<TestAnalysisResult>(42));

    // Verify the analysis is available
    auto* analysis = pm.getAnalysisManager().getAnalysis<TestAnalysisResult>(
        "TestAnalysis", *module);
    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getValue(), 42);

    // Run a pass that modifies the module and doesn't preserve analyses
    pm.addPass<ModifyingPassNoPreserve>();
    bool result = pm.run(*module);
    EXPECT_TRUE(result);  // Pass should modify the module

    // Verify the analysis has been invalidated
    analysis = pm.getAnalysisManager().getAnalysis<TestAnalysisResult>(
        "TestAnalysis", *module);
    EXPECT_EQ(analysis, nullptr);  // Analysis should be invalidated
}

// Test function pass that requires an analysis
class RequiringFunctionPass : public FunctionPass {
   public:
    RequiringFunctionPass()
        : FunctionPass("RequiringFunctionPass",
                       "A function pass that requires an analysis") {}

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        required.insert("TestFunctionAnalysis");
        preserved.insert("TestFunctionAnalysis");
    }

    bool runOnFunction(Function& f, AnalysisManager& am) override {
        auto* analysis =
            am.getAnalysis<TestAnalysisResult>("TestFunctionAnalysis", f);
        if (analysis) {
            lastAnalysisValue = analysis->getValue();
            return false;
        }
        return false;
    }

    static int lastAnalysisValue;
};

int RequiringFunctionPass::lastAnalysisValue = -1;

TEST_F(PassManagerTest, FunctionAnalysisRequirementChecking) {
    // Create a function
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    RequiringFunctionPass::lastAnalysisValue = -1;

    FunctionPassManager fpm(func);
    fpm.addPass<RequiringFunctionPass>();

    // Run without providing the required analysis - should fail
    std::cout << "Running pass without required analysis..." << std::endl;
    bool result = fpm.run();
    EXPECT_FALSE(result);  // Pass should fail due to missing analysis
    EXPECT_EQ(RequiringFunctionPass::lastAnalysisValue,
              -1);  // Pass should not have run

    // Now provide the required analysis and run again
    fpm.getAnalysisManager().registerAnalysis<TestAnalysisResult>(
        "TestFunctionAnalysis", *func,
        std::make_unique<TestAnalysisResult>(123));

    result = fpm.run();
    EXPECT_FALSE(result);  // Pass doesn't modify the function, but it should
                           // run successfully
    EXPECT_EQ(RequiringFunctionPass::lastAnalysisValue,
              123);  // Pass should have accessed the analysis
}

// Test function pass that modifies the function and does not preserve all
// analyses
class ModifyingFunctionPassNoPreserve : public FunctionPass {
   public:
    ModifyingFunctionPassNoPreserve()
        : FunctionPass(
              "ModifyingFunctionPassNoPreserve",
              "A function pass that modifies and doesn't preserve analyses") {}

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        // This pass doesn't preserve any analyses
        (void)required;
        (void)preserved;
    }

    bool runOnFunction(Function& f, AnalysisManager& am) override {
        (void)f;
        (void)am;
        return true;  // Indicate that the function was modified
    }
};

TEST_F(PassManagerTest, FunctionAnalysisInvalidation) {
    // Create a function
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    FunctionPassManager fpm(func);

    // Register an analysis
    fpm.getAnalysisManager().registerAnalysis<TestAnalysisResult>(
        "TestFunctionAnalysis", *func,
        std::make_unique<TestAnalysisResult>(123));

    // Verify the analysis is available
    auto* analysis = fpm.getAnalysisManager().getAnalysis<TestAnalysisResult>(
        "TestFunctionAnalysis", *func);
    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getValue(), 123);

    // Run a pass that modifies the function and doesn't preserve analyses
    fpm.addPass<ModifyingFunctionPassNoPreserve>();
    bool result = fpm.run();
    EXPECT_TRUE(result);  // Pass should modify the function

    // Verify the analysis has been invalidated
    analysis = fpm.getAnalysisManager().getAnalysis<TestAnalysisResult>(
        "TestFunctionAnalysis", *func);
    EXPECT_EQ(analysis, nullptr);  // Analysis should be invalidated
}

// Helper function to create a function with basic blocks
Function* createFunctionWithBlocks(Context* context, Module* module,
                                   const std::string& name) {
    auto* int32Ty = context->getInt32Type();
    auto* funcType = FunctionType::get(int32Ty, {int32Ty});
    auto* func = Function::Create(funcType, name, module);

    auto* bb1 = BasicBlock::Create(context, "entry", func);
    auto* bb2 = BasicBlock::Create(context, "middle", func);
    auto* bb3 = BasicBlock::Create(context, "exit", func);

    IRBuilder builder(bb1);
    auto* val1 =
        builder.createAdd(func->getArg(0), builder.getInt32(1), "val1");
    builder.createBr(bb2);

    builder.setInsertPoint(bb2);
    auto* val2 = builder.createMul(val1, builder.getInt32(2), "val2");
    builder.createBr(bb3);

    builder.setInsertPoint(bb3);
    builder.createRet(val2);

    return func;
}

class UnknownAnalysisRequiringPass : public FunctionPass {
   public:
    UnknownAnalysisRequiringPass()
        : FunctionPass("UnknownAnalysisRequiringPass",
                       "A pass that requires unknown analysis") {}

    void getAnalysisUsage(
        std::unordered_set<std::string>& required,
        std::unordered_set<std::string>& preserved) const override {
        (void)preserved;
        required.insert("UnknownAnalysis");
    }

    bool runOnFunction(Function& f, AnalysisManager& am) override {
        (void)f;
        (void)am;
        executed = true;
        return false;
    }

    static bool executed;
};

bool UnknownAnalysisRequiringPass::executed = false;

TEST_F(PassManagerTest, UnknownAnalysisRequirementFails) {
    auto* func =
        createFunctionWithBlocks(context.get(), module.get(), "test_func");

    UnknownAnalysisRequiringPass::executed = false;

    FunctionPassManager fpm(func);
    fpm.addPass<UnknownAnalysisRequiringPass>();

    // Run with unknown analysis requirement - should fail
    std::cout << "Running pass with unknown analysis requirement..."
              << std::endl;
    bool result = fpm.run();
    EXPECT_FALSE(result);
    EXPECT_FALSE(UnknownAnalysisRequiringPass::executed);
}

// Test for FunctionPassManager with null function (run without explicit
// function)
TEST_F(PassManagerTest, FunctionPassManagerWithNullFunction) {
    FunctionPassManager fpm(nullptr);
    fpm.addPass<CounterFunctionPass>();

    CounterFunctionPass::executionCount = 0;
    CounterFunctionPass::processedFunctions.clear();

    // Run without explicit function - should return false
    bool result = fpm.run();
    EXPECT_FALSE(result);
    EXPECT_EQ(CounterFunctionPass::executionCount, 0);
}

// Test for Module pass in FunctionPassManager (edge case for lines 87-91)
class TestModulePassInFunctionManager : public ModulePass {
   public:
    TestModulePassInFunctionManager()
        : ModulePass("TestModulePassInFunctionManager",
                     "Test module pass in function manager") {}

    bool runOnModule(Module& m, AnalysisManager& am) override {
        (void)m;
        (void)am;
        executed = true;
        return false;
    }

    static bool executed;
};

bool TestModulePassInFunctionManager::executed = false;

TEST_F(PassManagerTest, ModulePassInFunctionPassManager) {
    auto* func =
        createFunctionWithBlocks(context.get(), module.get(), "test_func");

    TestModulePassInFunctionManager::executed = false;

    FunctionPassManager fpm(func);
    fpm.addPass<TestModulePassInFunctionManager>();

    // Run with module pass in function pass manager - should handle gracefully
    bool result = fpm.run();
    EXPECT_FALSE(result);
    EXPECT_FALSE(TestModulePassInFunctionManager::executed);
}

// Test for BasicBlock pass run in PassManager (lines 54-58 coverage)
class TestBasicBlockPassInPassManager : public BasicBlockPass {
   public:
    TestBasicBlockPassInPassManager()
        : BasicBlockPass("TestBasicBlockPassInPassManager",
                         "Test BB pass in pass manager") {}

    bool runOnBasicBlock(BasicBlock& bb, AnalysisManager& am) override {
        (void)am;
        processedBlocks.push_back(&bb);
        return false;
    }

    static std::vector<BasicBlock*> processedBlocks;
};

std::vector<BasicBlock*> TestBasicBlockPassInPassManager::processedBlocks;

TEST_F(PassManagerTest, BasicBlockPassInPassManager) {
    createFunctionWithBlocks(context.get(), module.get(), "test_func");

    TestBasicBlockPassInPassManager::processedBlocks.clear();

    PassManager pm;
    pm.addPass<TestBasicBlockPassInPassManager>();

    // Run BasicBlock pass in PassManager - should run on all blocks in all
    // functions
    bool result = pm.run(*module);
    EXPECT_FALSE(result);
    EXPECT_EQ(TestBasicBlockPassInPassManager::processedBlocks.size(),
              3u);  // 3 blocks in the function
}

}  // namespace