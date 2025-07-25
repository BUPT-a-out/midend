#include <gtest/gtest.h>

#include "IR/Constant.h"
#include "IR/Type.h"

using namespace midend;

namespace {

class ConstantTest : public ::testing::Test {
   protected:
    void SetUp() override { context = std::make_unique<Context>(); }

    std::unique_ptr<Context> context;
};

TEST_F(ConstantTest, ConstantIntCreation) {
    auto* int32Ty = context->getInt32Type();

    auto* const42 = ConstantInt::get(int32Ty, 42);
    EXPECT_EQ(const42->getValue(), 42u);
    EXPECT_EQ(const42->getType(), int32Ty);
    EXPECT_FALSE(const42->isZero());
    EXPECT_FALSE(const42->isOne());
    EXPECT_FALSE(const42->isNegative());

    auto* const0 = ConstantInt::get(int32Ty, 0);
    EXPECT_TRUE(const0->isZero());
    EXPECT_FALSE(const0->isOne());

    auto* const1 = ConstantInt::get(int32Ty, 1);
    EXPECT_FALSE(const1->isZero());
    EXPECT_TRUE(const1->isOne());

    auto* constNeg = ConstantInt::get(int32Ty, -1);
    EXPECT_TRUE(constNeg->isNegative());
    EXPECT_FALSE(constNeg->isZero());
    EXPECT_FALSE(constNeg->isOne());
}

TEST_F(ConstantTest, ConstantIntSpecialValues) {
    auto* trueVal = ConstantInt::getTrue(context.get());
    auto* falseVal = ConstantInt::getFalse(context.get());

    EXPECT_TRUE(trueVal->isTrue());
    EXPECT_FALSE(trueVal->isFalse());
    EXPECT_EQ(trueVal->getValue(), 1u);
    EXPECT_TRUE(trueVal->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(trueVal->getType())->getBitWidth(), 1u);

    EXPECT_FALSE(falseVal->isTrue());
    EXPECT_TRUE(falseVal->isFalse());
    EXPECT_EQ(falseVal->getValue(), 0u);
    EXPECT_TRUE(falseVal->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(falseVal->getType())->getBitWidth(),
              1u);
}

TEST_F(ConstantTest, ConstantIntCaching) {
    auto* int32Ty = context->getInt32Type();

    // Same constants should return the same object
    auto* const1 = ConstantInt::get(int32Ty, 42);
    auto* const2 = ConstantInt::get(int32Ty, 42);
    EXPECT_EQ(const1, const2);

    // Different values should return different objects
    auto* const3 = ConstantInt::get(int32Ty, 43);
    EXPECT_NE(const1, const3);

    // Different types should return different objects
    auto* int1Ty = context->getInt1Type();
    auto* const4 = ConstantInt::get(int1Ty, 1);
    EXPECT_NE(const1, const4);
}

TEST_F(ConstantTest, ConstantIntDirectTypeGet) {
    auto* int32Ty = context->getInt32Type();
    auto* int64Ty = context->getIntegerType(64);

    // Test the direct ConstantInt::get(IntegerType*, uint64_t) overload
    auto* const42_32 = ConstantInt::get(int32Ty, 42);
    auto* const42_64 = ConstantInt::get(int64Ty, 42);

    EXPECT_EQ(const42_32->getValue(), 42u);
    EXPECT_EQ(const42_32->getType(), int32Ty);
    EXPECT_EQ(const42_64->getValue(), 42u);
    EXPECT_EQ(const42_64->getType(), int64Ty);

    // Should be different objects since different types
    EXPECT_NE(const42_32, const42_64);

    // Test caching works with this overload too
    auto* const42_32_again = ConstantInt::get(int32Ty, 42);
    EXPECT_EQ(const42_32, const42_32_again);
}

TEST_F(ConstantTest, ConstantFPCreation) {
    auto* floatTy = context->getFloatType();

    auto* constPI = ConstantFP::get(floatTy, 3.14159f);
    EXPECT_FLOAT_EQ(constPI->getValue(), 3.14159f);
    EXPECT_EQ(constPI->getType(), floatTy);
    EXPECT_FALSE(constPI->isZero());
    EXPECT_FALSE(constPI->isNegative());

    auto* const0 = ConstantFP::get(floatTy, 0.0f);
    EXPECT_TRUE(const0->isZero());
    EXPECT_FALSE(const0->isNegative());

    auto* constNeg = ConstantFP::get(floatTy, -2.5f);
    EXPECT_TRUE(constNeg->isNegative());
    EXPECT_FALSE(constNeg->isZero());
    EXPECT_FLOAT_EQ(constNeg->getValue(), -2.5f);
}

TEST_F(ConstantTest, ConstantFPCaching) {
    auto* floatTy = context->getFloatType();

    // Same constants should return the same object
    auto* const1 = ConstantFP::get(floatTy, 3.14f);
    auto* const2 = ConstantFP::get(floatTy, 3.14f);
    EXPECT_EQ(const1, const2);

    // Different values should return different objects
    auto* const3 = ConstantFP::get(floatTy, 2.71f);
    EXPECT_NE(const1, const3);
}

TEST_F(ConstantTest, ConstantArray) {
    auto* int32Ty = context->getInt32Type();
    auto* arrayTy = ArrayType::get(int32Ty, 3);

    std::vector<Constant*> elements = {ConstantInt::get(int32Ty, 1),
                                       ConstantInt::get(int32Ty, 2),
                                       ConstantInt::get(int32Ty, 3)};

    auto* constArray = ConstantArray::get(arrayTy, elements);
    EXPECT_EQ(constArray->getType(), arrayTy);
    EXPECT_EQ(constArray->getNumElements(), 3u);
    EXPECT_EQ(constArray->getElement(0), elements[0]);
    EXPECT_EQ(constArray->getElement(1), elements[1]);
    EXPECT_EQ(constArray->getElement(2), elements[2]);
}

TEST_F(ConstantTest, NullPointer) {
    auto* int32Ty = context->getInt32Type();
    auto* ptrTy = PointerType::get(int32Ty);

    auto* nullPtr = ConstantPointerNull::get(ptrTy);
    EXPECT_EQ(nullPtr->getType(), ptrTy);
    EXPECT_TRUE(nullPtr->isNullValue());

    // Test caching
    auto* nullPtr2 = ConstantPointerNull::get(ptrTy);
    EXPECT_EQ(nullPtr, nullPtr2);
}

TEST_F(ConstantTest, UndefValue) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    auto* undefInt = UndefValue::get(int32Ty);
    auto* undefFloat = UndefValue::get(floatTy);

    EXPECT_EQ(undefInt->getType(), int32Ty);
    EXPECT_EQ(undefFloat->getType(), floatTy);
    EXPECT_NE(undefInt, undefFloat);

    // Test caching
    auto* undefInt2 = UndefValue::get(int32Ty);
    EXPECT_EQ(undefInt, undefInt2);
}

TEST_F(ConstantTest, ConstantExpr) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    // Test binary operations
    auto* addExpr = ConstantExpr::getAdd(val1, val2);
    EXPECT_EQ(addExpr->getOpcode(), Opcode::Add);
    EXPECT_EQ(addExpr->getOperand(0), val1);
    EXPECT_EQ(addExpr->getOperand(1), val2);
    EXPECT_EQ(addExpr->getType(), int32Ty);

    auto* subExpr = ConstantExpr::getSub(val1, val2);
    EXPECT_EQ(subExpr->getOpcode(), Opcode::Sub);

    auto* mulExpr = ConstantExpr::getMul(val1, val2);
    EXPECT_EQ(mulExpr->getOpcode(), Opcode::Mul);
}

TEST_F(ConstantTest, ConstantStringRepresentations) {
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();
    auto* ptrTy = PointerType::get(int32Ty);
    auto* arrayTy = ArrayType::get(int32Ty, 3);

    // Test ConstantInt toString
    auto* constInt = ConstantInt::get(int32Ty, 42);
    EXPECT_EQ(constInt->toString(), "42");

    auto* constNegInt = ConstantInt::get(int32Ty, static_cast<uint32_t>(-1));
    EXPECT_EQ(constNegInt->toString(),
              std::to_string(static_cast<uint32_t>(-1)));

    // Test ConstantFP toString
    auto* constFloat = ConstantFP::get(floatTy, 3.14159f);
    EXPECT_EQ(constFloat->toString(), std::to_string(3.14159f));

    auto* constNegFloat = ConstantFP::get(floatTy, -2.5f);
    EXPECT_EQ(constNegFloat->toString(), std::to_string(-2.5f));

    // Test ConstantPointerNull toString
    auto* nullPtr = ConstantPointerNull::get(ptrTy);
    EXPECT_EQ(nullPtr->toString(), "null");

    // Test ConstantArray toString
    std::vector<Constant*> elements = {ConstantInt::get(int32Ty, 1),
                                       ConstantInt::get(int32Ty, 2),
                                       ConstantInt::get(int32Ty, 3)};
    auto* constArray = ConstantArray::get(arrayTy, elements);
    EXPECT_EQ(constArray->toString(), "[1, 2, 3]");

    // Test empty array
    auto* emptyArrayTy = ArrayType::get(int32Ty, 0);
    auto* emptyArray = ConstantArray::get(emptyArrayTy, {});
    EXPECT_EQ(emptyArray->toString(), "[]");

    // Test ConstantExpr toString
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);
    auto* addExpr = ConstantExpr::getAdd(val1, val2);
    EXPECT_EQ(addExpr->toString(), "const_expr");

    // Test UndefValue toString
    auto* undefInt = UndefValue::get(int32Ty);
    EXPECT_EQ(undefInt->toString(), "undef");
}

TEST_F(ConstantTest, ConstantFPDirectTypeGet) {
    auto* floatTy = context->getFloatType();

    // Test the direct ConstantFP::get(FloatType*, float) overload
    auto* constPi = ConstantFP::get(floatTy, 3.14159f);
    auto* constE = ConstantFP::get(floatTy, 2.71828f);

    EXPECT_FLOAT_EQ(constPi->getValue(), 3.14159f);
    EXPECT_EQ(constPi->getType(), floatTy);
    EXPECT_FLOAT_EQ(constE->getValue(), 2.71828f);
    EXPECT_EQ(constE->getType(), floatTy);

    // Should be different objects since different values
    EXPECT_NE(constPi, constE);

    // Test caching works with this overload too
    auto* constPiAgain = ConstantFP::get(floatTy, 3.14159f);
    EXPECT_EQ(constPi, constPiAgain);

    // Test the Context-based overload
    auto* constFromCtx = ConstantFP::get(context.get(), 3.14159f);
    EXPECT_EQ(constPi, constFromCtx);  // Should be the same cached object
}

TEST_F(ConstantTest, ConstantArrayElementAccess) {
    auto* int32Ty = context->getInt32Type();
    auto* arrayTy = ArrayType::get(int32Ty, 3);

    std::vector<Constant*> elements = {ConstantInt::get(int32Ty, 10),
                                       ConstantInt::get(int32Ty, 20),
                                       ConstantInt::get(int32Ty, 30)};

    auto* constArray = ConstantArray::get(arrayTy, elements);

    // Test getNumElements
    EXPECT_EQ(constArray->getNumElements(), 3u);

    // Test getElement with valid indices
    EXPECT_EQ(constArray->getElement(0), elements[0]);
    EXPECT_EQ(constArray->getElement(1), elements[1]);
    EXPECT_EQ(constArray->getElement(2), elements[2]);

    // Test getElement with invalid index (should return nullptr)
    EXPECT_EQ(constArray->getElement(3), nullptr);
    EXPECT_EQ(constArray->getElement(100), nullptr);

    // Test getType returns correct ArrayType
    EXPECT_EQ(constArray->getType(), arrayTy);
}

TEST_F(ConstantTest, ConstantExprOperandAccess) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 15);
    auto* val2 = ConstantInt::get(int32Ty, 25);

    auto* addExpr = ConstantExpr::getAdd(val1, val2);

    // Test getNumOperands
    EXPECT_EQ(addExpr->getNumOperands(), 2u);

    // Test operand access
    EXPECT_EQ(addExpr->getOperand(0), val1);
    EXPECT_EQ(addExpr->getOperand(1), val2);
}

TEST_F(ConstantTest, ConstantIntContextBitWidthCreation) {
    // Test ConstantInt::get(Context*, unsigned, uint64_t) - lines 24-26
    auto* const1 = ConstantInt::get(context.get(), 1, 1);
    EXPECT_EQ(const1->getValue(), 1u);
    EXPECT_TRUE(const1->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(const1->getType())->getBitWidth(), 1u);

    auto* const32 = ConstantInt::get(context.get(), 32, 65535);
    EXPECT_EQ(const32->getValue(), 65535u);
    EXPECT_TRUE(const32->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(const32->getType())->getBitWidth(),
              32u);

    // Test caching works with this overload
    auto* const1Again = ConstantInt::get(context.get(), 1, 1);
    EXPECT_EQ(const1, const1Again);
}

TEST_F(ConstantTest, ConstantExprTypeMismatchAdd) {
    // Test ConstantExpr::getAdd type mismatch - lines 68-70
    auto* int32Ty = context->getInt32Type();
    auto* int1Ty = context->getInt1Type();
    auto* val32 = ConstantInt::get(int32Ty, 10);
    auto* val1 = ConstantInt::get(int1Ty, 1);

    EXPECT_THROW(ConstantExpr::getAdd(val32, val1), std::runtime_error);
}

TEST_F(ConstantTest, ConstantExprTypeMismatchSub) {
    // Test ConstantExpr::getSub type mismatch - lines 76-78
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();
    auto* val32 = ConstantInt::get(int32Ty, 10);
    auto* valFloat = ConstantFP::get(floatTy, 20.5f);

    EXPECT_THROW(ConstantExpr::getSub(val32, valFloat), std::runtime_error);
}

TEST_F(ConstantTest, ConstantExprTypeMismatchMul) {
    // Test ConstantExpr::getMul type mismatch - lines 84-86
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();
    auto* val32 = ConstantInt::get(int32Ty, 10);
    auto* valFloat = ConstantFP::get(floatTy, 20.0f);

    EXPECT_THROW(ConstantExpr::getMul(val32, valFloat), std::runtime_error);
}

}  // namespace