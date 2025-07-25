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

TEST_F(TypeTest, TypeStaticGetMethods) {
    // Test FloatType::get static method
    auto* floatTy1 = FloatType::get(context.get());
    auto* floatTy2 = context->getFloatType();
    EXPECT_EQ(floatTy1, floatTy2);
    EXPECT_TRUE(floatTy1->isFloatType());

    // Test VoidType::get static method
    auto* voidTy1 = VoidType::get(context.get());
    auto* voidTy2 = context->getVoidType();
    EXPECT_EQ(voidTy1, voidTy2);
    EXPECT_TRUE(voidTy1->isVoidType());
}

TEST_F(TypeTest, FunctionTypeToString) {
    auto* int32Ty = context->getInt32Type();
    auto* voidTy = context->getVoidType();
    auto* floatTy = context->getFloatType();

    // Test function with no parameters
    auto* fnTy1 = FunctionType::get(voidTy, {});
    EXPECT_EQ(fnTy1->toString(), "void ()");

    // Test function with single parameter
    auto* fnTy2 = FunctionType::get(int32Ty, {floatTy});
    EXPECT_EQ(fnTy2->toString(), "i32 (float)");

    // Test function with multiple parameters
    std::vector<Type*> params = {int32Ty, floatTy, int32Ty};
    auto* fnTy3 = FunctionType::get(voidTy, params);
    EXPECT_EQ(fnTy3->toString(), "void (i32, float, i32)");

    // Test variadic function with no parameters
    auto* fnTy4 = FunctionType::get(int32Ty, {}, true);
    EXPECT_EQ(fnTy4->toString(), "i32 (...)");

    // Test variadic function with parameters
    auto* fnTy5 = FunctionType::get(voidTy, {int32Ty, floatTy}, true);
    EXPECT_EQ(fnTy5->toString(), "void (i32, float, ...)");
}

TEST_F(TypeTest, PointerToMethods) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();
    auto* voidTy = context->getVoidType();

    // Test IntegerType::getPointerTo
    auto* intPtrTy = int32Ty->getPointerTo();
    EXPECT_TRUE(intPtrTy->isPointerType());
    EXPECT_EQ(intPtrTy->getElementType(), int32Ty);

    // Test FloatType::getPointerTo
    auto* floatPtrTy = floatTy->getPointerTo();
    EXPECT_TRUE(floatPtrTy->isPointerType());
    EXPECT_EQ(floatPtrTy->getElementType(), floatTy);

    // Test VoidType::getPointerTo
    auto* voidPtrTy = voidTy->getPointerTo();
    EXPECT_TRUE(voidPtrTy->isPointerType());
    EXPECT_EQ(voidPtrTy->getElementType(), voidTy);
}

TEST_F(TypeTest, PointerTypeBitWidth) {
    auto* int32Ty = context->getInt32Type();
    auto* ptrTy = PointerType::get(int32Ty);

    // Test PointerType::getBitWidth - line 128 in Type.h
    EXPECT_EQ(ptrTy->getBitWidth(), 64u);
}

TEST_F(TypeTest, FunctionTypeClassof) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});

    // Test FunctionType::classof - lines 188-189 in Type.h
    EXPECT_TRUE(FunctionType::classof(fnTy));
    EXPECT_FALSE(FunctionType::classof(int32Ty));
}

TEST_F(TypeTest, LabelTypeGetAndToString) {
    // Test LabelType::get and toString - line 202 in Type.h
    auto* labelTy = LabelType::get(context.get());
    EXPECT_EQ(labelTy->getKind(), TypeKind::Label);
    EXPECT_EQ(labelTy->toString(), "label");

    // Test that the same context returns the same label type
    auto* labelTy2 = LabelType::get(context.get());
    EXPECT_EQ(labelTy, labelTy2);

    // Test LabelType::classof
    EXPECT_TRUE(LabelType::classof(labelTy));
    EXPECT_FALSE(LabelType::classof(context->getInt32Type()));
}

TEST_F(TypeTest, PointerTypeEquality) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Test that same element type creates same pointer type
    auto* ptr1 = PointerType::get(int32Ty);
    auto* ptr2 = PointerType::get(int32Ty);
    EXPECT_EQ(ptr1, ptr2);

    // Test that different element types create different pointer types
    auto* ptr3 = PointerType::get(floatTy);
    EXPECT_NE(ptr1, ptr3);

    // Test through Context method
    auto* ptr4 = context->getPointerType(int32Ty);
    auto* ptr5 = context->getPointerType(int32Ty);
    EXPECT_EQ(ptr4, ptr5);
    EXPECT_EQ(ptr1, ptr4);
}

TEST_F(TypeTest, ArrayTypeEquality) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Test that same element type and size creates same array type
    auto* arr1 = ArrayType::get(int32Ty, 10);
    auto* arr2 = ArrayType::get(int32Ty, 10);
    EXPECT_EQ(arr1, arr2);

    // Test that different element types create different array types
    auto* arr3 = ArrayType::get(floatTy, 10);
    EXPECT_NE(arr1, arr3);

    // Test that different sizes create different array types
    auto* arr4 = ArrayType::get(int32Ty, 20);
    EXPECT_NE(arr1, arr4);

    // Test through Context method
    auto* arr5 = context->getArrayType(int32Ty, 10);
    auto* arr6 = context->getArrayType(int32Ty, 10);
    EXPECT_EQ(arr5, arr6);
    EXPECT_EQ(arr1, arr5);
}

TEST_F(TypeTest, ComplexTypeComparison) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Test nested pointer types
    auto* ptr1 = PointerType::get(int32Ty);
    auto* ptrPtr1 = PointerType::get(ptr1);
    auto* ptrPtr2 = PointerType::get(PointerType::get(int32Ty));
    EXPECT_EQ(ptrPtr1, ptrPtr2);

    // Test array of pointers
    auto* arrPtr1 = ArrayType::get(ptr1, 5);
    auto* arrPtr2 = ArrayType::get(PointerType::get(int32Ty), 5);
    EXPECT_EQ(arrPtr1, arrPtr2);

    // Test pointer to array
    auto* arr1 = ArrayType::get(int32Ty, 10);
    auto* ptrArr1 = PointerType::get(arr1);
    auto* ptrArr2 = PointerType::get(ArrayType::get(int32Ty, 10));
    EXPECT_EQ(ptrArr1, ptrArr2);

    // Test mixed types are different
    auto* arr2 = ArrayType::get(floatTy, 10);
    auto* ptrArr3 = PointerType::get(arr2);
    EXPECT_NE(ptrArr1, ptrArr3);
}

TEST_F(TypeTest, ContextTypeUniqueness) {
    auto* int32Ty = context->getInt32Type();

    // Test that Context ensures type uniqueness
    auto* ptr1 = context->getPointerType(int32Ty);
    auto* ptr2 = context->getPointerType(int32Ty);
    auto* ptr3 = PointerType::get(int32Ty);

    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(ptr1, ptr3);

    auto* arr1 = context->getArrayType(int32Ty, 15);
    auto* arr2 = context->getArrayType(int32Ty, 15);
    auto* arr3 = ArrayType::get(int32Ty, 15);

    EXPECT_EQ(arr1, arr2);
    EXPECT_EQ(arr1, arr3);

    // Test different contexts create different types
    auto context2 = std::make_unique<Context>();
    auto* int32Ty2 = context2->getInt32Type();
    auto* ptr4 = context2->getPointerType(int32Ty2);

    // Different contexts should have different type instances
    EXPECT_NE(int32Ty, int32Ty2);
    EXPECT_NE(ptr1, ptr4);
}

TEST_F(TypeTest, TypeComparisonWithFunctionTypes) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Test function type comparison
    auto* fn1 = FunctionType::get(int32Ty, {floatTy});
    auto* fn2 = FunctionType::get(int32Ty, {floatTy});

    // Function types are currently not cached, so they create new instances
    EXPECT_NE(fn1, fn2);

    // Test pointer to function types
    auto* ptrFn1 = PointerType::get(fn1);
    auto* ptrFn2 = PointerType::get(fn1);
    EXPECT_EQ(ptrFn1, ptrFn2);

    // Different function types create different pointer types
    auto* ptrFn3 = PointerType::get(fn2);
    EXPECT_NE(ptrFn1, ptrFn3);
}

}  // namespace