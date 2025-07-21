#include <gtest/gtest.h>

#include <memory>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instruction.h"
#include "IR/Module.h"
#include "IR/Type.h"

using namespace midend;

TEST(ArrayInitializationTest, GlobalArraysWithInitialization) {
    auto context = std::make_unique<Context>();
    auto module = std::make_unique<Module>("array_init_test", context.get());
    auto* int32Ty = context->getInt32Type();

    // Test 1: Global constants
    auto* g = GlobalVariable::Create(
        int32Ty,
        true,  // isConstant: true
        GlobalVariable::ExternalLinkage,
        ConstantInt::get(int32Ty, 14),  // Initializer: 14
        "g", module.get());

    auto* N = GlobalVariable::Create(
        int32Ty,
        true,  // isConstant: true
        GlobalVariable::ExternalLinkage,
        ConstantInt::get(int32Ty, 10000),  // Initializer: 10000
        "N", module.get());

    // Test 2: Small initialized array (demonstrating current capability)
    auto* smallArrayTy = ArrayType::get(int32Ty, 5);
    std::vector<Constant*> smallArrayInit = {
        ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 1),
        ConstantInt::get(int32Ty, 2), ConstantInt::get(int32Ty, 3),
        ConstantInt::get(int32Ty, 4)};
    auto* smallArrayInitializer =
        ConstantArray::get(smallArrayTy, smallArrayInit);

    auto* smallData = GlobalVariable::Create(
        smallArrayTy,
        false,  // isConstant: false (mutable)
        GlobalVariable::ExternalLinkage, smallArrayInitializer, "small_data",
        module.get());

    // Test 3: Zero-initialized array
    auto* zeroArrayTy = ArrayType::get(int32Ty, 10);
    std::vector<Constant*> zeroArrayInit(10, ConstantInt::get(int32Ty, 0));
    auto* zeroArrayInitializer = ConstantArray::get(zeroArrayTy, zeroArrayInit);

    auto* zeroData =
        GlobalVariable::Create(zeroArrayTy,
                               false,  // isConstant: false
                               GlobalVariable::ExternalLinkage,
                               zeroArrayInitializer, "zero_data", module.get());

    // Test 4: Partially initialized array with explicit zeros
    // This demonstrates what SHOULD be done implicitly in the future
    auto* partialArrayTy = ArrayType::get(int32Ty, 10);
    std::vector<Constant*> partialArrayInit = {
        ConstantInt::get(int32Ty, 4), ConstantInt::get(int32Ty, 3),
        ConstantInt::get(int32Ty, 2), ConstantInt::get(int32Ty, 1)};
    // Fill rest with zeros (this should be implicit in future)
    for (size_t i = 4; i < 10; ++i) {
        partialArrayInit.push_back(ConstantInt::get(int32Ty, 0));
    }
    auto* partialArrayInitializer =
        ConstantArray::get(partialArrayTy, partialArrayInit);

    auto* partialData = GlobalVariable::Create(
        partialArrayTy, false, GlobalVariable::ExternalLinkage,
        partialArrayInitializer, "partial_data", module.get());

    // Test 5: Multi-dimensional array (2x3 matrix)
    auto* matrixTy = ArrayType::get(ArrayType::get(int32Ty, 3), 2);
    std::vector<Constant*> row1 = {ConstantInt::get(int32Ty, 1),
                                   ConstantInt::get(int32Ty, 2),
                                   ConstantInt::get(int32Ty, 3)};
    std::vector<Constant*> row2 = {ConstantInt::get(int32Ty, 4),
                                   ConstantInt::get(int32Ty, 5),
                                   ConstantInt::get(int32Ty, 6)};
    auto* row1Array = ConstantArray::get(ArrayType::get(int32Ty, 3), row1);
    auto* row2Array = ConstantArray::get(ArrayType::get(int32Ty, 3), row2);
    std::vector<Constant*> matrixInit = {row1Array, row2Array};
    auto* matrixInitializer = ConstantArray::get(matrixTy, matrixInit);

    auto* matrixData =
        GlobalVariable::Create(matrixTy, false, GlobalVariable::ExternalLinkage,
                               matrixInitializer, "matrix_data", module.get());

    // Create a simple function to use these arrays
    auto* funcTy = FunctionType::get(int32Ty, {});
    auto* testFunc = Function::Create(funcTy, "test_arrays", module.get());

    auto* entryBB = BasicBlock::Create(context.get(), "entry", testFunc);
    IRBuilder builder(entryBB);

    // Load from global constant
    auto* gVal = builder.createLoad(g, "g.val");

    // Load from 1D array
    auto* idx1 = ConstantInt::get(int32Ty, 1);
    auto* smallDataPtr =
        builder.createGEP(smallArrayTy, smallData, {idx1}, "small_data.1.addr");
    auto* smallDataVal = builder.createLoad(smallDataPtr, "small_data.1");

    // Load from 2D array (matrix[1][2] = 6)
    auto* matrixIdx =
        ConstantInt::get(int32Ty, 5);  // 1*3 + 2 = 5 (flattened index)
    auto* matrixPtr =
        builder.createGEP(matrixTy, matrixData, {matrixIdx}, "matrix.1.2.addr");
    auto* matrixVal = builder.createLoad(matrixPtr, "matrix.1.2");

    // Store to array (write to small_data[2])
    auto* idx2 = ConstantInt::get(int32Ty, 2);
    auto* storePtr =
        builder.createGEP(smallArrayTy, smallData, {idx2}, "small_data.2.addr");
    auto* newVal = ConstantInt::get(int32Ty, 42);
    builder.createStore(newVal, storePtr);

    // Return sum of all loaded values
    auto* temp = builder.createAdd(gVal, smallDataVal, "temp");
    auto* result = builder.createAdd(temp, matrixVal, "result");
    builder.createRet(result);

    // Verify the module structure
    EXPECT_EQ(module->globals().size(),
              6);  // g, N, small_data, zero_data, partial_data, matrix_data
    EXPECT_EQ(module->size(), 1);  // test_arrays function

    // Verify global constants
    EXPECT_TRUE(g->isConstant());
    EXPECT_TRUE(N->isConstant());
    EXPECT_FALSE(smallData->isConstant());
    EXPECT_FALSE(zeroData->isConstant());
    EXPECT_FALSE(partialData->isConstant());
    EXPECT_FALSE(matrixData->isConstant());

    // Verify initializers exist
    EXPECT_TRUE(g->hasInitializer());
    EXPECT_TRUE(N->hasInitializer());
    EXPECT_TRUE(smallData->hasInitializer());
    EXPECT_TRUE(zeroData->hasInitializer());
    EXPECT_TRUE(partialData->hasInitializer());
    EXPECT_TRUE(matrixData->hasInitializer());

    // Verify array types
    auto* smallDataType = dyn_cast<ArrayType>(smallData->getValueType());
    ASSERT_NE(smallDataType, nullptr);
    EXPECT_EQ(smallDataType->getNumElements(), 5);
    EXPECT_EQ(smallDataType->getElementType(), int32Ty);

    // Print the module for debugging
    EXPECT_EQ(IRPrinter::toString(module.get()),
              R"(; ModuleID = 'array_init_test'

@g = external global i32 14
@N = external global i32 10000
@small_data = external global [5 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4]
@zero_data = external global [10 x i32] [...]
@partial_data = external global [10 x i32] [i32 4, i32 3, i32 2, i32 1, ...]
@matrix_data = external global [2 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 3], [3 x i32] [i32 4, i32 5, i32 6]]

define i32 @test_arrays() {
entry:
  %g.val = load i32, i32* @g
  %small_data.1.addr = getelementptr [5 x i32], [5 x i32]* @small_data, i32 1
  %small_data.1 = load i32, i32* %small_data.1.addr
  %matrix.1.2.addr = getelementptr [2 x [3 x i32]], [2 x [3 x i32]]* @matrix_data, i32 5
  %matrix.1.2 = load i32, i32* %matrix.1.2.addr
  %small_data.2.addr = getelementptr [5 x i32], [5 x i32]* @small_data, i32 2
  store i32 42, i32* %small_data.2.addr
  %temp = add i32 %g.val, %small_data.1
  %result = add i32 %temp, %matrix.1.2
  ret i32 %result
}

)");
}
