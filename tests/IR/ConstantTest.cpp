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

}  // namespace