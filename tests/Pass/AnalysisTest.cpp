#include <gtest/gtest.h>

#include <algorithm>

#include "IR/Function.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"

using namespace midend;

class AnalysisTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());

        // Create a test function
        auto* voidTy = context->getVoidType();
        auto* fnTy = FunctionType::get(voidTy, {});
        function = Function::Create(fnTy, "test_function", module.get());
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    Function* function;
};

// Test analysis result class
class TestAnalysisResult : public AnalysisResult {
   public:
    TestAnalysisResult(const std::string& data) : data_(data) {}
    const std::string& getData() const { return data_; }

   private:
    std::string data_;
};

// Test analysis implementation for function level
class TestFunctionAnalysis : public AnalysisBase<TestFunctionAnalysis> {
   public:
    TestFunctionAnalysis() : AnalysisBase("TestFunctionAnalysis") {}

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<TestAnalysisResult>("function_" + f.getName());
    }

    bool supportsFunction() const override { return true; }
};

// Test analysis implementation for module level
class TestModuleAnalysis : public AnalysisBase<TestModuleAnalysis> {
   public:
    TestModuleAnalysis() : AnalysisBase("TestModuleAnalysis") {}

    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override {
        return std::make_unique<TestAnalysisResult>("module_" + m.getName());
    }

    bool supportsModule() const override { return true; }
};

// Test analysis that supports both function and module level
class TestDualAnalysis : public AnalysisBase<TestDualAnalysis> {
   public:
    TestDualAnalysis() : AnalysisBase("TestDualAnalysis") {}

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<TestAnalysisResult>("dual_function_" +
                                                    f.getName());
    }

    std::unique_ptr<AnalysisResult> runOnModule(Module& m) override {
        return std::make_unique<TestAnalysisResult>("dual_module_" +
                                                    m.getName());
    }

    bool supportsFunction() const override { return true; }
    bool supportsModule() const override { return true; }
};

// Test analysis with dependencies
class TestDependentAnalysis : public AnalysisBase<TestDependentAnalysis> {
   public:
    TestDependentAnalysis() : AnalysisBase("TestDependentAnalysis") {}

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<TestAnalysisResult>("dependent_" + f.getName());
    }

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"TestFunctionAnalysis"};
    }
};

// Test analysis with multiple dependencies
class TestMultiDependentAnalysis
    : public AnalysisBase<TestMultiDependentAnalysis> {
   public:
    TestMultiDependentAnalysis() : AnalysisBase("TestMultiDependentAnalysis") {}

    std::unique_ptr<AnalysisResult> runOnFunction(Function& f) override {
        return std::make_unique<TestAnalysisResult>("multi_dependent_" +
                                                    f.getName());
    }

    bool supportsFunction() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"TestFunctionAnalysis", "TestDependentAnalysis"};
    }
};

TEST_F(AnalysisTest, AnalysisBasicProperties) {
    TestFunctionAnalysis analysis;

    // Test name
    EXPECT_EQ(analysis.getName(), "TestFunctionAnalysis");

    // Test support flags
    EXPECT_TRUE(analysis.supportsFunction());
    EXPECT_FALSE(analysis.supportsModule());

    // Test dependencies (should be empty by default)
    auto deps = analysis.getDependencies();
    EXPECT_TRUE(deps.empty());
}

TEST_F(AnalysisTest, AnalysisRunOnFunction) {
    TestFunctionAnalysis analysis;

    auto result = analysis.runOnFunction(*function);
    EXPECT_NE(result, nullptr);

    auto* testResult = dynamic_cast<TestAnalysisResult*>(result.get());
    EXPECT_NE(testResult, nullptr);
    EXPECT_EQ(testResult->getData(), "function_test_function");
}

TEST_F(AnalysisTest, AnalysisRunOnModule) {
    TestModuleAnalysis analysis;

    auto result = analysis.runOnModule(*module);
    EXPECT_NE(result, nullptr);

    auto* testResult = dynamic_cast<TestAnalysisResult*>(result.get());
    EXPECT_NE(testResult, nullptr);
    EXPECT_EQ(testResult->getData(), "module_test_module");
}

TEST_F(AnalysisTest, AnalysisUnsupportedOperation) {
    TestFunctionAnalysis analysis;

    // Should return nullptr for unsupported operations
    auto result = analysis.runOnModule(*module);
    EXPECT_EQ(result, nullptr);
}

TEST_F(AnalysisTest, AnalysisDualSupport) {
    TestDualAnalysis analysis;

    // Test function support
    EXPECT_TRUE(analysis.supportsFunction());
    EXPECT_TRUE(analysis.supportsModule());

    // Test function execution
    auto funcResult = analysis.runOnFunction(*function);
    EXPECT_NE(funcResult, nullptr);
    auto* funcTestResult = dynamic_cast<TestAnalysisResult*>(funcResult.get());
    EXPECT_NE(funcTestResult, nullptr);
    EXPECT_EQ(funcTestResult->getData(), "dual_function_test_function");

    // Test module execution
    auto modResult = analysis.runOnModule(*module);
    EXPECT_NE(modResult, nullptr);
    auto* modTestResult = dynamic_cast<TestAnalysisResult*>(modResult.get());
    EXPECT_NE(modTestResult, nullptr);
    EXPECT_EQ(modTestResult->getData(), "dual_module_test_module");
}

TEST_F(AnalysisTest, AnalysisDependencies) {
    TestDependentAnalysis analysis;

    auto deps = analysis.getDependencies();
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], "TestFunctionAnalysis");
}

TEST_F(AnalysisTest, AnalysisMultipleDependencies) {
    TestMultiDependentAnalysis analysis;

    auto deps = analysis.getDependencies();
    EXPECT_EQ(deps.size(), 2u);
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "TestFunctionAnalysis") !=
                deps.end());
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "TestDependentAnalysis") !=
                deps.end());
}

TEST_F(AnalysisTest, AnalysisBaseHelper) {
    TestFunctionAnalysis analysis;

    // Test that AnalysisBase correctly stores the name
    EXPECT_EQ(analysis.getName(), "TestFunctionAnalysis");

    // Test that the name is returned consistently
    EXPECT_EQ(analysis.getName(), analysis.getName());
}

TEST_F(AnalysisTest, AnalysisInheritance) {
    TestFunctionAnalysis analysis;

    // Test that it's properly derived from Analysis
    Analysis* basePtr = &analysis;
    EXPECT_NE(basePtr, nullptr);
    EXPECT_EQ(basePtr->getName(), "TestFunctionAnalysis");
    EXPECT_TRUE(basePtr->supportsFunction());
    EXPECT_FALSE(basePtr->supportsModule());
}

TEST_F(AnalysisTest, AnalysisPolymorphism) {
    std::unique_ptr<Analysis> analysis =
        std::make_unique<TestFunctionAnalysis>();

    // Test polymorphic behavior
    EXPECT_EQ(analysis->getName(), "TestFunctionAnalysis");
    EXPECT_TRUE(analysis->supportsFunction());
    EXPECT_FALSE(analysis->supportsModule());

    // Test virtual function calls
    auto result = analysis->runOnFunction(*function);
    EXPECT_NE(result, nullptr);

    auto* testResult = dynamic_cast<TestAnalysisResult*>(result.get());
    EXPECT_NE(testResult, nullptr);
    EXPECT_EQ(testResult->getData(), "function_test_function");
}

TEST_F(AnalysisTest, AnalysisFactoryPattern) {
    // Test creating analysis through factory-like pattern
    auto createAnalysis = []() -> std::unique_ptr<Analysis> {
        return std::make_unique<TestFunctionAnalysis>();
    };

    auto analysis = createAnalysis();
    EXPECT_NE(analysis, nullptr);
    EXPECT_EQ(analysis->getName(), "TestFunctionAnalysis");

    auto result = analysis->runOnFunction(*function);
    EXPECT_NE(result, nullptr);
}