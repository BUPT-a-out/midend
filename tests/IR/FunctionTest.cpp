#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/Module.h"
#include "IR/Type.h"

using namespace midend;

namespace {

class FunctionTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
};

TEST_F(FunctionTest, BasicFunctionCreation) {
    auto* int32Ty = context->getInt32Type();
    auto* voidTy = context->getVoidType();

    std::vector<Type*> params = {int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);

    auto* func = Function::Create(fnTy, "test_function", module.get());

    EXPECT_EQ(func->getName(), "test_function");
    EXPECT_EQ(func->getFunctionType(), fnTy);
    EXPECT_EQ(func->getReturnType(), int32Ty);
    EXPECT_EQ(func->getParent(), module.get());
    EXPECT_EQ(func->getNumArgs(), 2u);
    EXPECT_TRUE(func->isDeclaration());
    EXPECT_FALSE(func->isDefinition());
    EXPECT_TRUE(func->empty());

    // Test void function creation
    auto* voidFnTy = FunctionType::get(voidTy, {});
    auto* voidFunc = Function::Create(voidFnTy, "void_function", module.get());
    EXPECT_EQ(voidFunc->getReturnType(), voidTy);
    EXPECT_EQ(voidFunc->getNumArgs(), 0u);
    EXPECT_TRUE(voidFunc->getReturnType()->isVoidType());
}

TEST_F(FunctionTest, FunctionArguments) {
    auto* int32Ty = context->getInt32Type();
    std::vector<Type*> params = {int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);

    auto* func = Function::Create(fnTy, "test_function", module.get());

    EXPECT_EQ(func->getNumArgs(), 2u);

    auto* arg0 = func->getArg(0);
    auto* arg1 = func->getArg(1);

    EXPECT_NE(arg0, nullptr);
    EXPECT_NE(arg1, nullptr);
    EXPECT_EQ(arg0->getType(), int32Ty);
    EXPECT_EQ(arg1->getType(), int32Ty);
    EXPECT_EQ(arg0->getParent(), func);
    EXPECT_EQ(arg1->getParent(), func);
    EXPECT_EQ(arg0->getArgNo(), 0u);
    EXPECT_EQ(arg1->getArgNo(), 1u);
}

TEST_F(FunctionTest, BasicBlockManagement) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    EXPECT_TRUE(func->empty());
    EXPECT_EQ(func->size(), 0u);

    auto* bb1 = BasicBlock::Create(context.get(), "entry", func);
    auto* bb2 = BasicBlock::Create(context.get(), "exit", func);

    EXPECT_FALSE(func->empty());
    EXPECT_EQ(func->size(), 2u);

    EXPECT_EQ(&func->front(), bb1);
    EXPECT_EQ(&func->back(), bb2);
    EXPECT_EQ(&func->getEntryBlock(), bb1);

    // Test iteration
    size_t count = 0;
    for (auto* bb : *func) {
        EXPECT_TRUE(bb == bb1 || bb == bb2);
        count++;
    }
    EXPECT_EQ(count, 2u);
}

TEST_F(FunctionTest, FunctionInModule) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    EXPECT_EQ(func->getParent(), module.get());

    // Check that function is in module
    bool found = false;
    for (auto* f : *module) {
        if (f == func) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(FunctionTest, ValueKindAndCasting) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    Value* val = func;
    EXPECT_TRUE(isa<Function>(*val));
    EXPECT_TRUE(isa<Constant>(*val));
    EXPECT_TRUE(isa<User>(*val));
    EXPECT_TRUE(isa<Value>(*val));

    auto* function = dyn_cast<Function>(val);
    EXPECT_NE(function, nullptr);
    EXPECT_EQ(function, func);
}

TEST_F(FunctionTest, NamedParameters) {
    auto* int32Ty = context->getInt32Type();
    std::vector<Type*> params = {int32Ty, int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);

    // Test function with named parameters
    std::vector<std::string> paramNames = {"x", "y", "z"};
    auto* func =
        Function::Create(fnTy, "named_function", paramNames, module.get());

    EXPECT_EQ(func->getNumArgs(), 3u);

    // Test named parameter access
    auto* argX = func->getArgByName("x");
    auto* argY = func->getArgByName("y");
    auto* argZ = func->getArgByName("z");

    EXPECT_NE(argX, nullptr);
    EXPECT_NE(argY, nullptr);
    EXPECT_NE(argZ, nullptr);

    EXPECT_EQ(argX->getName(), "x");
    EXPECT_EQ(argY->getName(), "y");
    EXPECT_EQ(argZ->getName(), "z");

    EXPECT_EQ(argX->getArgNo(), 0u);
    EXPECT_EQ(argY->getArgNo(), 1u);
    EXPECT_EQ(argZ->getArgNo(), 2u);

    // Test that indexed access still works
    EXPECT_EQ(func->getArg(0), argX);
    EXPECT_EQ(func->getArg(1), argY);
    EXPECT_EQ(func->getArg(2), argZ);

    // Test non-existent parameter name
    EXPECT_EQ(func->getArgByName("nonexistent"), nullptr);
}

TEST_F(FunctionTest, PartialNamedParameters) {
    auto* int32Ty = context->getInt32Type();
    std::vector<Type*> params = {int32Ty, int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);

    // Test function with partial named parameters
    std::vector<std::string> paramNames = {"first", "", "third"};
    auto* func = Function::Create(fnTy, "partial_named_function", paramNames,
                                  module.get());

    EXPECT_EQ(func->getNumArgs(), 3u);

    auto* arg0 = func->getArg(0);
    auto* arg1 = func->getArg(1);
    auto* arg2 = func->getArg(2);

    // Check names
    EXPECT_EQ(arg0->getName(), "first");
    EXPECT_EQ(arg1->getName(), "arg1");  // Default name for empty string
    EXPECT_EQ(arg2->getName(), "third");

    // Test named access
    EXPECT_EQ(func->getArgByName("first"), arg0);
    EXPECT_EQ(func->getArgByName("arg1"), arg1);
    EXPECT_EQ(func->getArgByName("third"), arg2);
}

TEST_F(FunctionTest, SetArgumentNames) {
    auto* int32Ty = context->getInt32Type();
    std::vector<Type*> params = {int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);

    // Create function with default names
    auto* func = Function::Create(fnTy, "test_function", module.get());

    // Check default names
    EXPECT_EQ(func->getArg(0)->getName(), "arg0");
    EXPECT_EQ(func->getArg(1)->getName(), "arg1");

    // Set custom names
    func->setArgName(0, "width");
    func->setArgName(1, "height");

    // Check updated names
    EXPECT_EQ(func->getArg(0)->getName(), "width");
    EXPECT_EQ(func->getArg(1)->getName(), "height");

    // Test named access with new names
    EXPECT_EQ(func->getArgByName("width"), func->getArg(0));
    EXPECT_EQ(func->getArgByName("height"), func->getArg(1));

    // Old names should not work anymore
    EXPECT_EQ(func->getArgByName("arg0"), nullptr);
    EXPECT_EQ(func->getArgByName("arg1"), nullptr);
}

TEST_F(FunctionTest, BasicBlockPushFront) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    // Create basic blocks manually (not using constructor that auto-adds to
    // function)
    auto* bb1 = BasicBlock::Create(context.get(), "bb1");
    auto* bb2 = BasicBlock::Create(context.get(), "bb2");
    auto* bb3 = BasicBlock::Create(context.get(), "bb3");

    // Add using push_back first
    func->push_back(bb1);
    func->push_back(bb2);

    // Check initial order: bb1, bb2
    auto it = func->begin();
    EXPECT_EQ(*it, bb1);
    ++it;
    EXPECT_EQ(*it, bb2);

    // Add using push_front
    func->push_front(bb3);

    // Check new order: bb3, bb1, bb2
    it = func->begin();
    EXPECT_EQ(*it, bb3);
    ++it;
    EXPECT_EQ(*it, bb1);
    ++it;
    EXPECT_EQ(*it, bb2);

    EXPECT_EQ(func->size(), 3u);
    EXPECT_EQ(bb3->getParent(), func);
    EXPECT_FALSE(func->isDeclaration());
}

TEST_F(FunctionTest, BasicBlockErase) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_function", module.get());

    auto* bb1 = BasicBlock::Create(context.get(), "bb1", func);
    auto* bb2 = BasicBlock::Create(context.get(), "bb2", func);
    auto* bb3 = BasicBlock::Create(context.get(), "bb3", func);

    EXPECT_EQ(func->size(), 3u);

    // Get iterator to bb2
    auto it = func->begin();
    ++it;  // Point to bb2
    EXPECT_EQ(*it, bb2);

    // Erase bb2
    auto next_it = func->erase(it);

    EXPECT_EQ(func->size(), 2u);
    EXPECT_EQ(*next_it, bb3);

    // Check order: bb1, bb3
    it = func->begin();
    EXPECT_EQ(*it, bb1);
    ++it;
    EXPECT_EQ(*it, bb3);

    // bb2 should be deleted automatically by erase
}

TEST_F(FunctionTest, FunctionInsertBefore) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    // Create first function in module
    auto* func1 = Function::Create(fnTy, "func1", module.get());

    // Create standalone function
    auto* func2 = Function::Create(fnTy, "func2");
    EXPECT_EQ(func2->getParent(), nullptr);

    // Insert func2 before func1
    func2->insertBefore(func1);
    EXPECT_EQ(func2->getParent(), module.get());

    // Check order in module: func2, func1
    auto it = module->begin();
    EXPECT_EQ(*it, func2);
    ++it;
    EXPECT_EQ(*it, func1);
}

TEST_F(FunctionTest, FunctionInsertAfter) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    // Create first function in module
    auto* func1 = Function::Create(fnTy, "func1", module.get());

    // Create standalone function
    auto* func2 = Function::Create(fnTy, "func2");
    EXPECT_EQ(func2->getParent(), nullptr);

    // Insert func2 after func1
    func2->insertAfter(func1);
    EXPECT_EQ(func2->getParent(), module.get());

    // Check order in module: func1, func2
    auto it = module->begin();
    EXPECT_EQ(*it, func1);
    ++it;
    EXPECT_EQ(*it, func2);
}

TEST_F(FunctionTest, FunctionMoveBefore) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    auto* func1 = Function::Create(fnTy, "func1", module.get());
    auto* func2 = Function::Create(fnTy, "func2", module.get());
    auto* func3 = Function::Create(fnTy, "func3", module.get());

    // Initial order: func1, func2, func3
    auto it = module->begin();
    EXPECT_EQ(*it, func1);
    ++it;
    EXPECT_EQ(*it, func2);
    ++it;
    EXPECT_EQ(*it, func3);

    // Move func3 before func1
    func3->moveBefore(func1);

    // New order: func3, func1, func2
    it = module->begin();
    EXPECT_EQ(*it, func3);
    ++it;
    EXPECT_EQ(*it, func1);
    ++it;
    EXPECT_EQ(*it, func2);
}

TEST_F(FunctionTest, FunctionMoveAfter) {
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});

    auto* func1 = Function::Create(fnTy, "func1", module.get());
    auto* func2 = Function::Create(fnTy, "func2", module.get());
    auto* func3 = Function::Create(fnTy, "func3", module.get());

    // Initial order: func1, func2, func3
    auto it = module->begin();
    EXPECT_EQ(*it, func1);
    ++it;
    EXPECT_EQ(*it, func2);
    ++it;
    EXPECT_EQ(*it, func3);

    // Move func1 after func3
    func1->moveAfter(func3);

    // New order: func2, func3, func1
    it = module->begin();
    EXPECT_EQ(*it, func2);
    ++it;
    EXPECT_EQ(*it, func3);
    ++it;
    EXPECT_EQ(*it, func1);
}

}  // namespace