#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"

using namespace midend;

class PassTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
        analysisManager = std::make_unique<AnalysisManager>();
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    std::unique_ptr<AnalysisManager> analysisManager;
};

// Test pass implementations
class TestModulePass : public ModulePass {
   public:
    TestModulePass() : ModulePass("TestModulePass", "A test module pass") {}

    bool runOnModule(Module& m, AnalysisManager& am) override {
        wasRun = true;
        processedModule = &m;
        (void)am;
        return false;  // Don't modify the module
    }

    static bool wasRun;
    static Module* processedModule;
};

bool TestModulePass::wasRun = false;
Module* TestModulePass::processedModule = nullptr;

class TestFunctionPass : public FunctionPass {
   public:
    TestFunctionPass()
        : FunctionPass("TestFunctionPass", "A test function pass") {}

    bool runOnFunction(Function& f, AnalysisManager& am) override {
        processedFunctions.push_back(&f);
        (void)am;
        return false;
    }

    static std::vector<Function*> processedFunctions;
};

std::vector<Function*> TestFunctionPass::processedFunctions;

class TestBasicBlockPass : public BasicBlockPass {
   public:
    TestBasicBlockPass()
        : BasicBlockPass("TestBasicBlockPass", "A test basic block pass") {}

    bool runOnBasicBlock(BasicBlock& bb, AnalysisManager& am) override {
        processedBlocks.push_back(&bb);
        (void)am;
        return false;  // Don't modify the block
    }

    static std::vector<BasicBlock*> processedBlocks;
};

std::vector<BasicBlock*> TestBasicBlockPass::processedBlocks;

TEST_F(PassTest, PassInfo) {
    TestModulePass pass;

    EXPECT_EQ(pass.getName(), "TestModulePass");
    EXPECT_EQ(pass.getPassInfo().getDescription(), "A test module pass");
    EXPECT_EQ(pass.getKind(), Pass::ModulePass);
}

TEST_F(PassTest, ModulePassExecution) {
    TestModulePass::wasRun = false;
    TestModulePass::processedModule = nullptr;

    TestModulePass pass;
    bool result = pass.runOnModule(*module, *analysisManager);

    EXPECT_TRUE(TestModulePass::wasRun);
    EXPECT_EQ(TestModulePass::processedModule, module.get());
    EXPECT_FALSE(result);  // Our test pass doesn't modify the module
}

TEST_F(PassTest, FunctionPassExecution) {
    TestFunctionPass::processedFunctions.clear();

    // Create some functions
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func1 = Function::Create(fnTy, "function1", module.get());
    auto* func2 = Function::Create(fnTy, "function2", module.get());

    TestFunctionPass pass;

    // Function passes normally run on individual functions
    bool result1 = pass.runOnFunction(*func1, *analysisManager);
    bool result2 = pass.runOnFunction(*func2, *analysisManager);

    EXPECT_FALSE(result1);
    EXPECT_FALSE(result2);
    EXPECT_EQ(TestFunctionPass::processedFunctions.size(), 2u);
    EXPECT_EQ(TestFunctionPass::processedFunctions[0], func1);
    EXPECT_EQ(TestFunctionPass::processedFunctions[1], func2);
}

TEST_F(PassTest, BasicBlockPassExecution) {
    TestBasicBlockPass::processedBlocks.clear();

    // Create a function with basic blocks
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());
    auto* bb1 = BasicBlock::Create(context.get(), "entry", func);
    auto* bb2 = BasicBlock::Create(context.get(), "exit", func);

    TestBasicBlockPass pass;

    bool result1 = pass.runOnBasicBlock(*bb1, *analysisManager);
    bool result2 = pass.runOnBasicBlock(*bb2, *analysisManager);

    EXPECT_FALSE(result1);
    EXPECT_FALSE(result2);
    EXPECT_EQ(TestBasicBlockPass::processedBlocks.size(), 2u);
    EXPECT_EQ(TestBasicBlockPass::processedBlocks[0], bb1);
    EXPECT_EQ(TestBasicBlockPass::processedBlocks[1], bb2);
}

TEST_F(PassTest, PassCasting) {
    TestModulePass modulePass;
    TestFunctionPass functionPass;
    TestBasicBlockPass basicBlockPass;

    Pass* p1 = &modulePass;
    Pass* p2 = &functionPass;
    Pass* p3 = &basicBlockPass;

    EXPECT_TRUE(ModulePass::classof(p1));
    EXPECT_FALSE(ModulePass::classof(p2));
    EXPECT_FALSE(ModulePass::classof(p3));

    EXPECT_FALSE(FunctionPass::classof(p1));
    EXPECT_TRUE(FunctionPass::classof(p2));
    EXPECT_FALSE(FunctionPass::classof(p3));

    EXPECT_FALSE(BasicBlockPass::classof(p1));
    EXPECT_FALSE(BasicBlockPass::classof(p2));
    EXPECT_TRUE(BasicBlockPass::classof(p3));
}

TEST_F(PassTest, FunctionPassOnModule) {
    TestFunctionPass::processedFunctions.clear();

    // Create some functions
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func1 = Function::Create(fnTy, "function1", module.get());
    auto* func2 = Function::Create(fnTy, "function2", module.get());

    TestFunctionPass pass;

    // FunctionPass::runOnModule should run the pass on each function
    bool result = pass.runOnModule(*module, *analysisManager);

    EXPECT_FALSE(result);  // Our test pass doesn't modify anything
    EXPECT_EQ(TestFunctionPass::processedFunctions.size(), 2u);

    // Check that both functions were processed
    bool found1 = false, found2 = false;
    for (auto* f : TestFunctionPass::processedFunctions) {
        if (f == func1) found1 = true;
        if (f == func2) found2 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}

TEST_F(PassTest, BasicBlockPassOnFunction) {
    TestBasicBlockPass::processedBlocks.clear();

    // Create a function with basic blocks
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());
    auto* bb1 = BasicBlock::Create(context.get(), "entry", func);
    auto* bb2 = BasicBlock::Create(context.get(), "exit", func);

    TestBasicBlockPass pass;

    // BasicBlockPass::runOnFunction should run the pass on each basic block
    bool result = pass.runOnFunction(*func, *analysisManager);

    EXPECT_FALSE(result);  // Our test pass doesn't modify anything
    EXPECT_EQ(TestBasicBlockPass::processedBlocks.size(), 2u);

    // Check that both blocks were processed
    bool found1 = false, found2 = false;
    for (auto* bb : TestBasicBlockPass::processedBlocks) {
        if (bb == bb1) found1 = true;
        if (bb == bb2) found2 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}