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

}  // namespace