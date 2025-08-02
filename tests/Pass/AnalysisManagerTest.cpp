#include <gtest/gtest.h>

#include <algorithm>

#include "IR/Function.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"

using namespace midend;

namespace {

class AnalysisManagerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
        analysisManager = std::make_unique<AnalysisManager>();

        // Create a test function
        auto* voidTy = context->getVoidType();
        auto* fnTy = FunctionType::get(voidTy, {});
        function = Function::Create(fnTy, "test_function", module.get());
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    std::unique_ptr<AnalysisManager> analysisManager;
    Function* function;
};

// Test analysis result classes
class TestModuleAnalysis : public AnalysisResult {
   public:
    TestModuleAnalysis(int value) : value_(value) {}
    int getValue() const { return value_; }

   private:
    int value_;
};

class TestFunctionAnalysis : public AnalysisResult {
   public:
    TestFunctionAnalysis(const std::string& name) : name_(name) {}
    const std::string& getName() const { return name_; }

   private:
    std::string name_;
};

TEST_F(AnalysisManagerTest, ModuleAnalysisRegistration) {
    auto analysis = std::make_unique<TestModuleAnalysis>(42);
    auto* analysisPtr = analysis.get();

    analysisManager->registerAnalysis("test_module_analysis", *module,
                                      std::move(analysis));

    auto* retrieved = analysisManager->getAnalysis<TestModuleAnalysis>(
        "test_module_analysis", *module);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, analysisPtr);
    EXPECT_EQ(retrieved->getValue(), 42);
}

TEST_F(AnalysisManagerTest, FunctionAnalysisRegistration) {
    auto analysis = std::make_unique<TestFunctionAnalysis>("function_analysis");
    auto* analysisPtr = analysis.get();

    analysisManager->registerAnalysis("test_function_analysis", *function,
                                      std::move(analysis));

    auto* retrieved = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "test_function_analysis", *function);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, analysisPtr);
    EXPECT_EQ(retrieved->getName(), "function_analysis");
}

TEST_F(AnalysisManagerTest, NonExistentAnalysis) {
    auto* retrieved = analysisManager->getAnalysis<TestModuleAnalysis>(
        "nonexistent", *module);
    EXPECT_EQ(retrieved, nullptr);

    auto* retrievedFunc = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "nonexistent", *function);
    EXPECT_EQ(retrievedFunc, nullptr);
}

TEST_F(AnalysisManagerTest, WrongAnalysisType) {
    auto analysis = std::make_unique<TestModuleAnalysis>(42);
    analysisManager->registerAnalysis("test_analysis", *module,
                                      std::move(analysis));

    // Try to retrieve with wrong type
    auto* retrieved = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "test_analysis", *module);
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(AnalysisManagerTest, AnalysisInvalidation) {
    auto analysis = std::make_unique<TestModuleAnalysis>(42);
    analysisManager->registerAnalysis("test_analysis", *module,
                                      std::move(analysis));

    // Verify analysis exists
    auto* retrieved = analysisManager->getAnalysis<TestModuleAnalysis>(
        "test_analysis", *module);
    EXPECT_NE(retrieved, nullptr);

    // Invalidate the analysis
    analysisManager->invalidateAnalysis("test_analysis", *module);

    // Verify analysis no longer exists
    auto* retrievedAfter = analysisManager->getAnalysis<TestModuleAnalysis>(
        "test_analysis", *module);
    EXPECT_EQ(retrievedAfter, nullptr);
}

TEST_F(AnalysisManagerTest, FunctionAnalysisInvalidation) {
    auto analysis = std::make_unique<TestFunctionAnalysis>("test");
    analysisManager->registerAnalysis("test_analysis", *function,
                                      std::move(analysis));

    // Verify analysis exists
    auto* retrieved = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "test_analysis", *function);
    EXPECT_NE(retrieved, nullptr);

    // Invalidate the analysis
    analysisManager->invalidateAnalysis("test_analysis", *function);

    // Verify analysis no longer exists
    auto* retrievedAfter = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "test_analysis", *function);
    EXPECT_EQ(retrievedAfter, nullptr);
}

TEST_F(AnalysisManagerTest, InvalidateAllModuleAnalyses) {
    // Register multiple module analyses
    analysisManager->registerAnalysis("analysis1", *module,
                                      std::make_unique<TestModuleAnalysis>(1));
    analysisManager->registerAnalysis("analysis2", *module,
                                      std::make_unique<TestModuleAnalysis>(2));

    // Register function analysis
    analysisManager->registerAnalysis(
        "func_analysis", *function,
        std::make_unique<TestFunctionAnalysis>("test"));

    // Verify all analyses exist
    EXPECT_NE(
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis1", *module),
        nullptr);
    EXPECT_NE(
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis2", *module),
        nullptr);
    EXPECT_NE(analysisManager->getAnalysis<TestFunctionAnalysis>(
                  "func_analysis", *function),
              nullptr);

    // Invalidate all module analyses
    analysisManager->invalidateAllAnalyses(*module);

    // Verify module analyses are gone but function analysis remains
    EXPECT_EQ(
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis1", *module),
        nullptr);
    EXPECT_EQ(
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis2", *module),
        nullptr);
    EXPECT_EQ(analysisManager->getAnalysis<TestFunctionAnalysis>(
                  "func_analysis", *function),
              nullptr);
}

TEST_F(AnalysisManagerTest, InvalidateAllFunctionAnalyses) {
    // Create another function
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* function2 = Function::Create(fnTy, "test_function2", module.get());

    // Register analyses for both functions
    analysisManager->registerAnalysis(
        "analysis1", *function,
        std::make_unique<TestFunctionAnalysis>("func1"));
    analysisManager->registerAnalysis(
        "analysis2", *function,
        std::make_unique<TestFunctionAnalysis>("func1_2"));
    analysisManager->registerAnalysis(
        "analysis3", *function2,
        std::make_unique<TestFunctionAnalysis>("func2"));

    // Verify all analyses exist
    EXPECT_NE(analysisManager->getAnalysis<TestFunctionAnalysis>("analysis1",
                                                                 *function),
              nullptr);
    EXPECT_NE(analysisManager->getAnalysis<TestFunctionAnalysis>("analysis2",
                                                                 *function),
              nullptr);
    EXPECT_NE(analysisManager->getAnalysis<TestFunctionAnalysis>("analysis3",
                                                                 *function2),
              nullptr);

    // Invalidate analyses for first function only
    analysisManager->invalidateAllAnalyses(*function);

    // Verify function1 analyses are gone but function2 analysis remains
    EXPECT_EQ(analysisManager->getAnalysis<TestFunctionAnalysis>("analysis1",
                                                                 *function),
              nullptr);
    EXPECT_EQ(analysisManager->getAnalysis<TestFunctionAnalysis>("analysis2",
                                                                 *function),
              nullptr);
    EXPECT_NE(analysisManager->getAnalysis<TestFunctionAnalysis>("analysis3",
                                                                 *function2),
              nullptr);
}

TEST_F(AnalysisManagerTest, InvalidateAll) {
    // Register various analyses
    analysisManager->registerAnalysis("module_analysis", *module,
                                      std::make_unique<TestModuleAnalysis>(42));
    analysisManager->registerAnalysis(
        "function_analysis", *function,
        std::make_unique<TestFunctionAnalysis>("test"));

    // Verify analyses exist
    EXPECT_NE(analysisManager->getAnalysis<TestModuleAnalysis>(
                  "module_analysis", *module),
              nullptr);
    EXPECT_NE(analysisManager->getAnalysis<TestFunctionAnalysis>(
                  "function_analysis", *function),
              nullptr);

    // Invalidate everything
    analysisManager->invalidateAll();

    // Verify all analyses are gone
    EXPECT_EQ(analysisManager->getAnalysis<TestModuleAnalysis>(
                  "module_analysis", *module),
              nullptr);
    EXPECT_EQ(analysisManager->getAnalysis<TestFunctionAnalysis>(
                  "function_analysis", *function),
              nullptr);
}

TEST_F(AnalysisManagerTest, MultipleAnalysesPerUnit) {
    // Register multiple analyses for the same module
    analysisManager->registerAnalysis("analysis1", *module,
                                      std::make_unique<TestModuleAnalysis>(1));
    analysisManager->registerAnalysis("analysis2", *module,
                                      std::make_unique<TestModuleAnalysis>(2));
    analysisManager->registerAnalysis("analysis3", *module,
                                      std::make_unique<TestModuleAnalysis>(3));

    // Verify all can be retrieved independently
    auto* analysis1 =
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis1", *module);
    auto* analysis2 =
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis2", *module);
    auto* analysis3 =
        analysisManager->getAnalysis<TestModuleAnalysis>("analysis3", *module);

    EXPECT_NE(analysis1, nullptr);
    EXPECT_NE(analysis2, nullptr);
    EXPECT_NE(analysis3, nullptr);
    EXPECT_EQ(analysis1->getValue(), 1);
    EXPECT_EQ(analysis2->getValue(), 2);
    EXPECT_EQ(analysis3->getValue(), 3);

    // Verify they're different objects
    EXPECT_NE(analysis1, analysis2);
    EXPECT_NE(analysis2, analysis3);
    EXPECT_NE(analysis1, analysis3);
}

TEST_F(AnalysisManagerTest, AnalysisOverwrite) {
    // Register an analysis
    analysisManager->registerAnalysis("test_analysis", *module,
                                      std::make_unique<TestModuleAnalysis>(42));

    auto* first = analysisManager->getAnalysis<TestModuleAnalysis>(
        "test_analysis", *module);
    EXPECT_NE(first, nullptr);
    EXPECT_EQ(first->getValue(), 42);

    // Overwrite with new analysis
    analysisManager->registerAnalysis(
        "test_analysis", *module, std::make_unique<TestModuleAnalysis>(100));

    auto* second = analysisManager->getAnalysis<TestModuleAnalysis>(
        "test_analysis", *module);
    EXPECT_NE(second, nullptr);
    EXPECT_EQ(second->getValue(), 100);
    EXPECT_NE(first, second);  // Should be different objects
}

// Test Analysis classes for new system
class TestFunctionAnalysisImpl : public AnalysisBase {
   public:
    TestFunctionAnalysisImpl() {}

    static std::string getName() { return "TestFunctionAnalysis"; }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<TestFunctionAnalysis>(f.getName());
    }

    bool supportsFunction() const override { return true; }
};

class TestModuleAnalysisImpl : public AnalysisBase {
   public:
    TestModuleAnalysisImpl() {}

    static std::string getName() { return "TestModuleAnalysis"; }

    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override {
        (void)m;
        return std::make_unique<TestModuleAnalysis>(42);
    }

    bool supportsModule() const override { return true; }
};

class TestDependentAnalysis : public AnalysisBase {
   public:
    TestDependentAnalysis() {}

    static std::string getName() { return "TestDependentAnalysis"; }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<TestFunctionAnalysis>("dependent_" +
                                                      f.getName());
    }

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"TestFunctionAnalysis"};
    }
};

TEST_F(AnalysisManagerTest, AnalysisTypeRegistration) {
    // Test registering an analysis type
    analysisManager->registerAnalysisType<TestFunctionAnalysisImpl>();

    // Check that the analysis type is registered
    EXPECT_TRUE(analysisManager->isAnalysisRegistered("TestFunctionAnalysis"));

    // Check that unregistered analysis is not found
    EXPECT_FALSE(analysisManager->isAnalysisRegistered("UnregisteredAnalysis"));

    // Get registered analysis types
    auto types = analysisManager->getRegisteredAnalysisTypes();
    EXPECT_TRUE(std::find(types.begin(), types.end(), "TestFunctionAnalysis") !=
                types.end());
}

TEST_F(AnalysisManagerTest, AutomaticAnalysisComputation) {
    // Register the analysis type
    analysisManager->registerAnalysisType<TestFunctionAnalysisImpl>();

    // Request analysis that hasn't been computed yet
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestFunctionAnalysis", *function);

    // Should be automatically computed
    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getName(), function->getName());
}

TEST_F(AnalysisManagerTest, ModuleAnalysisComputation) {
    // Register the analysis type
    analysisManager->registerAnalysisType<TestModuleAnalysisImpl>();

    // Request analysis that hasn't been computed yet
    auto* analysis = analysisManager->getAnalysis<TestModuleAnalysis>(
        "TestModuleAnalysis", *module);

    // Should be automatically computed
    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getValue(), 42);
}

TEST_F(AnalysisManagerTest, AnalysisDependencyResolution) {
    // Register both analyses
    analysisManager->registerAnalysisType<TestFunctionAnalysisImpl>();
    analysisManager->registerAnalysisType<TestDependentAnalysis>();

    // Request dependent analysis - should automatically compute dependency
    auto* dependentAnalysis =
        analysisManager->getAnalysis<TestFunctionAnalysis>(
            "TestDependentAnalysis", *function);

    EXPECT_NE(dependentAnalysis, nullptr);
    EXPECT_EQ(dependentAnalysis->getName(), "dependent_" + function->getName());

    // Verify that the dependency was also computed
    auto* baseAnalysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestFunctionAnalysis", *function);
    EXPECT_NE(baseAnalysis, nullptr);
    EXPECT_EQ(baseAnalysis->getName(), function->getName());
}

TEST_F(AnalysisManagerTest, AnalysisComputationFailure) {
    // Don't register the analysis type

    // Request analysis that cannot be computed
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "UnregisteredAnalysis", *function);

    // Should return nullptr
    EXPECT_EQ(analysis, nullptr);
}

TEST_F(AnalysisManagerTest, AnalysisTypeAlreadyComputed) {
    // Register the analysis type
    analysisManager->registerAnalysisType<TestFunctionAnalysisImpl>();

    // Manually register an analysis result
    analysisManager->registerAnalysis(
        "TestFunctionAnalysis", *function,
        std::make_unique<TestFunctionAnalysis>("manual"));

    // Request the analysis - should return the manually registered one
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestFunctionAnalysis", *function);

    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getName(),
              "manual");  // Should be the manually registered one
}

TEST_F(AnalysisManagerTest, AnalysisWithCustomFactory) {
    // Register analysis with custom factory
    analysisManager->registerAnalysisType(
        "CustomAnalysis", []() -> std::unique_ptr<Analysis> {
            return std::make_unique<TestFunctionAnalysisImpl>();
        });

    EXPECT_TRUE(analysisManager->isAnalysisRegistered("CustomAnalysis"));

    // Request analysis with custom name
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "CustomAnalysis", *function);

    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getName(), function->getName());
}

TEST_F(AnalysisManagerTest, AnalysisUnsupportedLevel) {
    // Register function-level analysis
    analysisManager->registerAnalysisType<TestFunctionAnalysisImpl>();

    // Try to request it at module level - should fail
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestFunctionAnalysis", *module);

    EXPECT_EQ(analysis, nullptr);
}

TEST_F(AnalysisManagerTest, AnalysisDependencyFailure) {
    // Register dependent analysis but not its dependency
    analysisManager->registerAnalysisType<TestDependentAnalysis>();

    // Request dependent analysis - should fail due to missing dependency
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestDependentAnalysis", *function);

    EXPECT_EQ(analysis, nullptr);
}

TEST_F(AnalysisManagerTest, MultipleAnalysisTypes) {
    // Register multiple analysis types
    analysisManager->registerAnalysisType<TestFunctionAnalysisImpl>();
    analysisManager->registerAnalysisType<TestModuleAnalysisImpl>();

    // Verify both are registered
    EXPECT_TRUE(analysisManager->isAnalysisRegistered("TestFunctionAnalysis"));
    EXPECT_TRUE(analysisManager->isAnalysisRegistered("TestModuleAnalysis"));

    // Get all registered types
    auto types = analysisManager->getRegisteredAnalysisTypes();
    EXPECT_GE(types.size(), 2u);
    EXPECT_TRUE(std::find(types.begin(), types.end(), "TestFunctionAnalysis") !=
                types.end());
    EXPECT_TRUE(std::find(types.begin(), types.end(), "TestModuleAnalysis") !=
                types.end());

    // Request both analyses
    auto* funcAnalysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestFunctionAnalysis", *function);
    auto* modAnalysis = analysisManager->getAnalysis<TestModuleAnalysis>(
        "TestModuleAnalysis", *module);

    EXPECT_NE(funcAnalysis, nullptr);
    EXPECT_NE(modAnalysis, nullptr);
    EXPECT_EQ(funcAnalysis->getName(), function->getName());
    EXPECT_EQ(modAnalysis->getValue(), 42);
}

// Test for getRegisteredAnalyses function coverage (lines 244-254)
TEST_F(AnalysisManagerTest, GetRegisteredAnalyses) {
    // Create a second function
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* function2 = Function::Create(fnTy, "test_function2", module.get());

    // Test empty registered analyses for function
    auto registeredFunc = analysisManager->getRegisteredAnalyses(*function);
    EXPECT_TRUE(registeredFunc.empty());

    // Test empty registered analyses for module
    auto registeredMod = analysisManager->getRegisteredAnalyses(*module);
    EXPECT_TRUE(registeredMod.empty());

    // Register analyses for function
    analysisManager->registerAnalysis(
        "func_analysis1", *function,
        std::make_unique<TestFunctionAnalysis>("test1"));
    analysisManager->registerAnalysis(
        "func_analysis2", *function,
        std::make_unique<TestFunctionAnalysis>("test2"));

    // Register analysis for different function
    analysisManager->registerAnalysis(
        "func_analysis3", *function2,
        std::make_unique<TestFunctionAnalysis>("test3"));

    // Register analyses for module
    analysisManager->registerAnalysis("mod_analysis1", *module,
                                      std::make_unique<TestModuleAnalysis>(1));
    analysisManager->registerAnalysis("mod_analysis2", *module,
                                      std::make_unique<TestModuleAnalysis>(2));

    // Test registered analyses for first function
    auto registeredFunc1 = analysisManager->getRegisteredAnalyses(*function);
    EXPECT_EQ(registeredFunc1.size(), 2u);
    EXPECT_TRUE(std::find(registeredFunc1.begin(), registeredFunc1.end(),
                          "func_analysis1") != registeredFunc1.end());
    EXPECT_TRUE(std::find(registeredFunc1.begin(), registeredFunc1.end(),
                          "func_analysis2") != registeredFunc1.end());

    // Test registered analyses for second function
    auto registeredFunc2 = analysisManager->getRegisteredAnalyses(*function2);
    EXPECT_EQ(registeredFunc2.size(), 1u);
    EXPECT_TRUE(std::find(registeredFunc2.begin(), registeredFunc2.end(),
                          "func_analysis3") != registeredFunc2.end());

    // Test registered analyses for module
    auto registeredModule = analysisManager->getRegisteredAnalyses(*module);
    EXPECT_EQ(registeredModule.size(), 2u);
    EXPECT_TRUE(std::find(registeredModule.begin(), registeredModule.end(),
                          "mod_analysis1") != registeredModule.end());
    EXPECT_TRUE(std::find(registeredModule.begin(), registeredModule.end(),
                          "mod_analysis2") != registeredModule.end());
}

// Test Analysis classes with module dependencies
class TestModuleDependentAnalysis : public AnalysisBase {
   public:
    TestModuleDependentAnalysis() {}

    static std::string getName() { return "TestModuleDependentAnalysis"; }

    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override {
        (void)m;
        return std::make_unique<TestModuleAnalysis>(100);
    }

    bool supportsModule() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"TestModuleAnalysis"};
    }
};

class TestFailingAnalysis : public AnalysisBase {
   public:
    TestFailingAnalysis() {}

    static std::string getName() { return "TestFailingAnalysis"; }

    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override {
        (void)m;
        return nullptr;  // Simulate failure
    }

    bool supportsModule() const override { return true; }
};

// Test for Analysis dependencies in module computeAnalysis (lines 227-232)
TEST_F(AnalysisManagerTest, ModuleAnalysisDependencies) {
    // Register both analyses
    analysisManager->registerAnalysisType<TestModuleAnalysisImpl>();
    analysisManager->registerAnalysisType<TestModuleDependentAnalysis>();

    // Request dependent analysis - should automatically compute dependency
    auto* dependentAnalysis = analysisManager->getAnalysis<TestModuleAnalysis>(
        "TestModuleDependentAnalysis", *module);

    EXPECT_NE(dependentAnalysis, nullptr);
    EXPECT_EQ(dependentAnalysis->getValue(), 100);

    // Verify that the dependency was also computed
    auto* baseAnalysis = analysisManager->getAnalysis<TestModuleAnalysis>(
        "TestModuleAnalysis", *module);
    EXPECT_NE(baseAnalysis, nullptr);
    EXPECT_EQ(baseAnalysis->getValue(), 42);
}

TEST_F(AnalysisManagerTest, ModuleAnalysisDependencyFailure) {
    // Register dependent analysis but not its dependency
    analysisManager->registerAnalysisType<TestModuleDependentAnalysis>();

    // Request dependent analysis - should fail due to missing dependency
    auto* analysis = analysisManager->getAnalysis<TestModuleAnalysis>(
        "TestModuleDependentAnalysis", *module);

    EXPECT_EQ(analysis, nullptr);
}

TEST_F(AnalysisManagerTest, ModuleAnalysisComputationFailure) {
    // Register analysis that fails to compute
    analysisManager->registerAnalysisType<TestFailingAnalysis>();

    // Request analysis - should fail due to null result
    auto* analysis = analysisManager->getAnalysis<TestModuleAnalysis>(
        "TestFailingAnalysis", *module);

    EXPECT_EQ(analysis, nullptr);
}

// Test for function analysis returning null result
class TestFailingFunctionAnalysis : public AnalysisBase {
   public:
    TestFailingFunctionAnalysis() {}

    static std::string getName() { return "TestFailingFunctionAnalysis"; }

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        (void)f;
        return nullptr;  // Simulate failure
    }

    bool supportsFunction() const override { return true; }
};

TEST_F(AnalysisManagerTest, FunctionAnalysisComputationFailure) {
    // Register analysis that fails to compute
    analysisManager->registerAnalysisType<TestFailingFunctionAnalysis>();

    // Request analysis - should fail due to null result
    auto* analysis = analysisManager->getAnalysis<TestFunctionAnalysis>(
        "TestFailingFunctionAnalysis", *function);

    EXPECT_EQ(analysis, nullptr);
}

}  // namespace