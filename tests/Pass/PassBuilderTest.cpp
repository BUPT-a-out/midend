#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"

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

}  // namespace