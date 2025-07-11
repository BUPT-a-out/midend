#include <gtest/gtest.h>

#include "IR/Type.h"
#include "Support/Casting.h"

using namespace midend;

namespace {

class TypeTest : public ::testing::Test {
   protected:
    void SetUp() override { context = std::make_unique<Context>(); }

    std::unique_ptr<Context> context;
};

TEST_F(TypeTest, BasicTypes) {
    auto* voidTy = context->getVoidType();
    EXPECT_TRUE(voidTy->isVoidType());
    EXPECT_EQ(voidTy->toString(), "void");

    auto* int32Ty = context->getInt32Type();
    EXPECT_TRUE(int32Ty->isIntegerType());
    EXPECT_EQ(int32Ty->getBitWidth(), 32u);
    EXPECT_EQ(int32Ty->toString(), "i32");

    auto* floatTy = context->getFloatType();
    EXPECT_TRUE(floatTy->isFloatType());
    EXPECT_EQ(floatTy->getBitWidth(), 32u);
    EXPECT_EQ(floatTy->toString(), "float");
}

TEST_F(TypeTest, IntegerTypes) {
    auto* int1 = context->getInt1Type();

    EXPECT_EQ(int1->getBitWidth(), 1u);
    EXPECT_TRUE(int1->isBool());

    // Test that only int1 and int32 are supported
    auto* int32 = context->getInt32Type();
    EXPECT_EQ(int32->getBitWidth(), 32u);
    EXPECT_FALSE(int32->isBool());

    // Test that unsupported types return nullptr
    auto* int8 = context->getIntegerType(8);
    EXPECT_EQ(int8, nullptr);

    auto* int16 = context->getIntegerType(16);
    EXPECT_EQ(int16, nullptr);

    auto* int64 = context->getIntegerType(64);
    EXPECT_EQ(int64, nullptr);
}

TEST_F(TypeTest, PointerTypes) {
    auto* int32Ty = context->getInt32Type();
    auto* ptrTy = PointerType::get(int32Ty);

    EXPECT_TRUE(ptrTy->isPointerType());
    EXPECT_EQ(ptrTy->getElementType(), int32Ty);
    EXPECT_EQ(ptrTy->toString(), "i32*");
}

TEST_F(TypeTest, ArrayTypes) {
    auto* int32Ty = context->getInt32Type();
    auto* arrayTy = ArrayType::get(int32Ty, 10);

    EXPECT_TRUE(arrayTy->isArrayType());
    EXPECT_EQ(arrayTy->getElementType(), int32Ty);
    EXPECT_EQ(arrayTy->getNumElements(), 10u);
    EXPECT_EQ(arrayTy->toString(), "[10 x i32]");
}

TEST_F(TypeTest, FunctionTypes) {
    auto* int32Ty = context->getInt32Type();
    auto* voidTy = context->getVoidType();

    std::vector<Type*> params = {int32Ty, int32Ty};
    auto* fnTy = FunctionType::get(int32Ty, params);

    EXPECT_TRUE(fnTy->isFunctionType());
    EXPECT_EQ(fnTy->getReturnType(), int32Ty);
    EXPECT_EQ(fnTy->getParamTypes().size(), 2u);
    EXPECT_EQ(fnTy->getParamTypes()[0], int32Ty);
    EXPECT_FALSE(fnTy->isVarArg());

    auto* varArgFnTy = FunctionType::get(voidTy, params, true);
    EXPECT_TRUE(varArgFnTy->isVarArg());
}

TEST_F(TypeTest, TypeCasting) {
    auto* int32Ty = context->getInt32Type();
    Type* ty = int32Ty;

    EXPECT_TRUE(midend::isa<IntegerType>(*ty));
    EXPECT_FALSE(midend::isa<FloatType>(*ty));

    auto* intTy = midend::dyn_cast<IntegerType>(ty);
    EXPECT_NE(intTy, nullptr);
    EXPECT_EQ(intTy->getBitWidth(), 32u);

    auto* floatTy = midend::dyn_cast<FloatType>(ty);
    EXPECT_EQ(floatTy, nullptr);
}

}  // namespace