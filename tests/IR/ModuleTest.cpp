#include <gtest/gtest.h>

#include <iterator>
#include <vector>

#include "IR/Function.h"
#include "IR/Module.h"
#include "IR/Type.h"

using namespace midend;

namespace {

class ModuleTest : public ::testing::Test {
   protected:
    void SetUp() override { context = std::make_unique<Context>(); }

    std::unique_ptr<Context> context;
};

TEST_F(ModuleTest, ModuleCreation) {
    Module module("test_module", context.get());

    EXPECT_EQ(module.getName(), "test_module");
    EXPECT_EQ(module.getContext(), context.get());
    EXPECT_TRUE(module.empty());
    EXPECT_EQ(module.size(), 0u);
}

TEST_F(ModuleTest, FunctionManagement) {
    Module module("test_module", context.get());

    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    auto* func1 = Function::Create(fnTy, "function1", &module);
    auto* func2 = Function::Create(fnTy, "function2", &module);

    EXPECT_FALSE(module.empty());
    EXPECT_EQ(module.size(), 2u);

    EXPECT_EQ(module.front(), func1);
    EXPECT_EQ(module.back(), func2);

    // Test iteration
    size_t count = 0;
    for (auto* func : module) {
        EXPECT_TRUE(func == func1 || func == func2);
        count++;
    }
    EXPECT_EQ(count, 2u);
}

TEST_F(ModuleTest, FunctionLookup) {
    Module module("test_module", context.get());

    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    auto* func = Function::Create(fnTy, "test_function", &module);

    auto* found = module.getFunction("test_function");
    EXPECT_EQ(found, func);

    auto* notFound = module.getFunction("nonexistent_function");
    EXPECT_EQ(notFound, nullptr);
}

TEST_F(ModuleTest, FunctionRemoval) {
    Module module("test_module", context.get());

    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    auto* func1 = Function::Create(fnTy, "function1", &module);
    auto* func2 = Function::Create(fnTy, "function2", &module);

    EXPECT_EQ(module.size(), 2u);

    func1->eraseFromParent();
    EXPECT_EQ(module.size(), 1u);
    EXPECT_EQ(module.front(), func2);

    func2->removeFromParent();
    EXPECT_EQ(module.size(), 0u);
    EXPECT_TRUE(module.empty());
}

TEST_F(ModuleTest, GlobalVariables) {
    Module module("test_module", context.get());

    auto* int32Ty = context->getInt32Type();
    auto* globalVar =
        GlobalVariable::Create(int32Ty, false, GlobalVariable::ExternalLinkage,
                               nullptr, "global_var", &module);

    EXPECT_EQ(globalVar->getName(), "global_var");
    EXPECT_EQ(globalVar->getValueType(), int32Ty);
    EXPECT_EQ(globalVar->getParent(), &module);
    EXPECT_FALSE(globalVar->isConstant());
    EXPECT_EQ(globalVar->getLinkage(), GlobalVariable::ExternalLinkage);

    // Check that global is in module
    bool found = false;
    for (auto* global : module.globals()) {
        if (global == globalVar) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ModuleTest, ModuleIterators) {
    Module module("test_module", context.get());

    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    std::vector<Function*> functions;
    for (int i = 0; i < 5; ++i) {
        auto* func =
            Function::Create(fnTy, "function" + std::to_string(i), &module);
        functions.push_back(func);
    }

    // Test forward iteration
    size_t index = 0;
    for (auto it = module.begin(); it != module.end(); ++it) {
        EXPECT_EQ(*it, functions[index]);
        index++;
    }
    EXPECT_EQ(index, 5u);

    // Test const iteration
    const Module& constModule = module;
    index = 0;
    for (auto it = constModule.begin(); it != constModule.end(); ++it) {
        EXPECT_EQ(*it, functions[index]);
        index++;
    }
    EXPECT_EQ(index, 5u);
}

TEST_F(ModuleTest, ModulePrinting) {
    Module module("test_module", context.get());

    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    Function::Create(fnTy, "test_function", &module);

    std::string moduleStr = module.toString();
    EXPECT_FALSE(moduleStr.empty());

    // Verify exact module string structure
    std::string expected =
        "; ModuleID = 'test_module'\ndeclare test_function\n";
    EXPECT_EQ(moduleStr, expected);
}

TEST_F(ModuleTest, FunctionInsertionAndRemoval) {
    Module module("test_module", context.get());

    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    // Create functions with the module as parent
    auto* func1 = Function::Create(fnTy, "function1", &module);
    auto* func2 = Function::Create(fnTy, "function2", &module);

    // Functions should be automatically added to module via constructor
    EXPECT_EQ(module.size(), 2u);
    EXPECT_EQ(func1->getParent(), &module);
    EXPECT_EQ(func2->getParent(), &module);

    // Test that functions are in the module
    EXPECT_EQ(module.front(), func1);
    EXPECT_EQ(module.back(), func2);

    // Test basic push_front operation with a new function
    auto* func3 = Function::Create(fnTy, "function3");
    module.push_front(func3);
    EXPECT_EQ(module.size(), 3u);
    EXPECT_EQ(module.front(), func3);
    EXPECT_EQ(func3->getParent(), &module);

    // All functions will be deleted by module destructor
}

TEST_F(ModuleTest, GlobalVariableManagement) {
    Module module("test_module", context.get());

    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    auto* global1 =
        GlobalVariable::Create(int32Ty, false, GlobalVariable::ExternalLinkage,
                               nullptr, "global1", &module);
    auto* global2 =
        GlobalVariable::Create(floatTy, true, GlobalVariable::InternalLinkage,
                               nullptr, "global2", &module);

    // Test getGlobalVariable
    EXPECT_EQ(module.getGlobalVariable("global1"), global1);
    EXPECT_EQ(module.getGlobalVariable("global2"), global2);
    EXPECT_EQ(module.getGlobalVariable("nonexistent"), nullptr);

    // Test removeGlobalVariable
    module.removeGlobalVariable(global1);
    EXPECT_EQ(module.getGlobalVariable("global1"), nullptr);
    EXPECT_EQ(global1->getParent(), nullptr);

    // global2 should still be there
    EXPECT_EQ(module.getGlobalVariable("global2"), global2);

    delete global1;
}

}  // namespace