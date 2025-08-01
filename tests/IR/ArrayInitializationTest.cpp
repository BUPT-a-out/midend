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

    // Test GEP on i32* (pointer arithmetic)
    auto* i32Ptr =
        builder.createGEP(smallData, ConstantInt::get(int32Ty, 0), "i32_ptr");
    auto* nextPtr =
        builder.createGEP(i32Ptr, ConstantInt::get(int32Ty, 1), "next_ptr");
    auto* ptrVal = builder.createLoad(nextPtr, "ptr_val");

    // Return sum of all loaded values
    auto* temp = builder.createAdd(gVal, smallDataVal, "temp");
    auto* temp2 = builder.createAdd(temp, matrixVal, "temp2");
    auto* result = builder.createAdd(temp2, ptrVal, "result");
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
  %matrix.1.2 = load [3 x i32], [3 x i32]* %matrix.1.2.addr
  %small_data.2.addr = getelementptr [5 x i32], [5 x i32]* @small_data, i32 2
  store i32 42, i32* %small_data.2.addr
  %i32_ptr = getelementptr [5 x i32], [5 x i32]* @small_data, i32 0
  %next_ptr = getelementptr i32, i32* %i32_ptr, i32 1
  %ptr_val = load i32, i32* %next_ptr
  %temp = add i32 %g.val, %small_data.1
  %temp2 = add i32 %temp, %matrix.1.2
  %result = add i32 %temp2, %ptr_val
  ret i32 %result
}

)");
}

TEST(ArrayInitializationTest, ArrayParameterFunction) {
    auto context = std::make_unique<Context>();
    auto module = std::make_unique<Module>("array_param_test", context.get());
    auto* int32Ty = context->getInt32Type();

    // Create sum function: int sum(int arr[], int arr2[][5])
    // In C, arr[] becomes int*, arr2[][5] becomes int(*)[5]
    auto* int32PtrTy = int32Ty->getPointerTo();
    auto* arrayOf5Ty = ArrayType::get(int32Ty, 5);
    auto* arrayOf5PtrTy = PointerType::get(arrayOf5Ty);  // int(*)[5]

    std::vector<Type*> paramTypes = {int32PtrTy, arrayOf5PtrTy};
    auto* sumFuncTy = FunctionType::get(int32Ty, paramTypes);
    auto* sumFunc = Function::Create(sumFuncTy, "sum", module.get());

    // Set parameter names
    auto* arrParam = sumFunc->getArg(0);
    auto* arr2Param = sumFunc->getArg(1);
    arrParam->setName("arr");
    arr2Param->setName("arr2");

    // Create function body
    auto* entryBB = BasicBlock::Create(context.get(), "entry", sumFunc);
    IRBuilder builder(entryBB);

    auto arrTy = arrParam->getType();
    auto arr2Ty = arr2Param->getType();

    // arr[0]
    auto* arr0Ptr = builder.createGEP(
        arrTy, arrParam, {ConstantInt::get(int32Ty, 0)}, "arr.0.ptr");
    auto* arr0Val = builder.createLoad(arr0Ptr, "arr.0");

    // arr[1]
    auto* arr1Ptr = builder.createGEP(
        arrTy, arrParam, {ConstantInt::get(int32Ty, 1)}, "arr.1.ptr");
    auto* arr1Val = builder.createLoad(arr1Ptr, "arr.1");

    // arr2[0][0] - first get arr2[0], then [0]
    auto* arr2Row0Ptr = builder.createGEP(
        arr2Ty, arr2Param, {ConstantInt::get(int32Ty, 0)}, "arr2.0.ptr");
    auto* arr2_0_0_Ptr =
        builder.createGEP(arrayOf5Ty, arr2Row0Ptr,
                          {ConstantInt::get(int32Ty, 0)}, "arr2.0.0.ptr");
    auto* arr2_0_0_Val = builder.createLoad(arr2_0_0_Ptr, "arr2.0.0");

    // arr2[0][1]
    auto* arr2_0_1_Ptr =
        builder.createGEP(arrayOf5Ty, arr2Row0Ptr,
                          {ConstantInt::get(int32Ty, 1)}, "arr2.0.1.ptr");
    auto* arr2_0_1_Val = builder.createLoad(arr2_0_1_Ptr, "arr2.0.1");

    // arr2[0][1]

    auto* arr2_0_1_Ptr2 = builder.createGEP(
        arrayOf5Ty, arr2Row0Ptr,
        {ConstantInt::get(int32Ty, 1), ConstantInt::get(int32Ty, 2)},
        "arr2.0.1.ptr_2");
    builder.createLoad(arr2_0_1_Ptr2, "arr2.0.1_2");

    // return arr[0] + arr[1] + arr2[0][0] + arr2[0][1]
    auto* sum1 = builder.createAdd(arr0Val, arr1Val, "sum1");
    auto* sum2 = builder.createAdd(sum1, arr2_0_0_Val, "sum2");
    auto* result = builder.createAdd(sum2, arr2_0_1_Val, "result");
    builder.createRet(result);

    // Create test arrays
    auto* arr1DTy = ArrayType::get(int32Ty, 10);
    std::vector<Constant*> arr1DInit;
    for (int i = 0; i < 10; i++) {
        arr1DInit.push_back(ConstantInt::get(int32Ty, i));
    }
    auto* arr1DInitializer = ConstantArray::get(arr1DTy, arr1DInit);
    auto* arr1D =
        GlobalVariable::Create(arr1DTy, false, GlobalVariable::ExternalLinkage,
                               arr1DInitializer, "test_arr1d", module.get());

    // Create 2D array [3][5]
    auto* arr2DTy = ArrayType::get(arrayOf5Ty, 3);
    std::vector<Constant*> arr2DInit;
    for (int i = 0; i < 3; i++) {
        std::vector<Constant*> rowInit;
        for (int j = 0; j < 5; j++) {
            rowInit.push_back(ConstantInt::get(int32Ty, i * 5 + j));
        }
        arr2DInit.push_back(ConstantArray::get(arrayOf5Ty, rowInit));
    }
    auto* arr2DInitializer = ConstantArray::get(arr2DTy, arr2DInit);
    auto* arr2D =
        GlobalVariable::Create(arr2DTy, false, GlobalVariable::ExternalLinkage,
                               arr2DInitializer, "test_arr2d", module.get());

    // Create main function that calls sum
    auto* mainFuncTy = FunctionType::get(int32Ty, {});
    auto* mainFunc = Function::Create(mainFuncTy, "main", module.get());
    auto* mainBB = BasicBlock::Create(context.get(), "entry", mainFunc);
    IRBuilder mainBuilder(mainBB);

    // Call sum function
    auto* callResult =
        mainBuilder.createCall(sumFunc, {arr1D, arr2D}, "sum_result");
    mainBuilder.createRet(callResult);

    EXPECT_EQ(IRPrinter::toString(module.get()),
              R"(; ModuleID = 'array_param_test'

@test_arr1d = external global [10 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9]
@test_arr2d = external global [3 x [5 x i32]] [[5 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4], [5 x i32] [i32 5, i32 6, i32 7, i32 8, i32 9], [5 x i32] [i32 10, i32 11, i32 12, i32 13, i32 14]]

define i32 @sum(i32* %arr, [5 x i32]* %arr2) {
entry:
  %arr.0.ptr = getelementptr i32*, i32* %arr, i32 0
  %arr.0 = load i32, i32* %arr.0.ptr
  %arr.1.ptr = getelementptr i32*, i32* %arr, i32 1
  %arr.1 = load i32, i32* %arr.1.ptr
  %arr2.0.ptr = getelementptr [5 x i32]*, [5 x i32]* %arr2, i32 0
  %arr2.0.0.ptr = getelementptr [5 x i32], [5 x i32]* %arr2.0.ptr, i32 0
  %arr2.0.0 = load i32, i32* %arr2.0.0.ptr
  %arr2.0.1.ptr = getelementptr [5 x i32], [5 x i32]* %arr2.0.ptr, i32 1
  %arr2.0.1 = load i32, i32* %arr2.0.1.ptr
  %arr2.0.1.ptr_2 = getelementptr [5 x i32], [5 x i32]* %arr2.0.ptr, i32 1, i32 2
  %arr2.0.1_2 = load i32, i32* %arr2.0.1.ptr_2
  %sum1 = add i32 %arr.0, %arr.1
  %sum2 = add i32 %sum1, %arr2.0.0
  %result = add i32 %sum2, %arr2.0.1
  ret i32 %result
}

define i32 @main() {
entry:
  %sum_result = call i32 @sum([10 x i32]* @test_arr1d, [3 x [5 x i32]]* @test_arr2d)
  ret i32 %sum_result
}

)");
}

TEST(ArrayInitializationTest, MultiIndexGEPTypes) {
    auto context = std::make_unique<Context>();
    auto module = std::make_unique<Module>("multi_index_test", context.get());
    auto* int32Ty = context->getInt32Type();

    // Create multi-dimensional array: [2][3][4] x i32
    auto* arr1DTy = ArrayType::get(int32Ty, 4);  // [4 x i32]
    auto* arr2DTy = ArrayType::get(arr1DTy, 3);  // [3 x [4 x i32]]
    auto* arr3DTy = ArrayType::get(arr2DTy, 2);  // [2 x [3 x [4 x i32]]]

    // Create simple initializer
    std::vector<Constant*> level1;
    for (int k = 0; k < 4; k++) {
        level1.push_back(ConstantInt::get(int32Ty, k));
    }
    auto* arr1DConst = ConstantArray::get(arr1DTy, level1);

    std::vector<Constant*> level2;
    for (int j = 0; j < 3; j++) {
        level2.push_back(arr1DConst);
    }
    auto* arr2DConst = ConstantArray::get(arr2DTy, level2);

    std::vector<Constant*> level3;
    for (int i = 0; i < 2; i++) {
        level3.push_back(arr2DConst);
    }
    auto* arr3DConst = ConstantArray::get(arr3DTy, level3);

    auto* arr3D =
        GlobalVariable::Create(arr3DTy, false, GlobalVariable::ExternalLinkage,
                               arr3DConst, "arr3d", module.get());

    // Create test function
    auto* funcTy = FunctionType::get(int32Ty, {});
    auto* testFunc = Function::Create(funcTy, "test_multi_gep", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", testFunc);
    IRBuilder builder(bb);

    // Test different numbers of indices
    // GEP with 1 index: [2 x [3 x [4 x i32]]]* → [3 x [4 x i32]]*
    auto* gep1 = builder.createGEP(arr3DTy, arr3D,
                                   {ConstantInt::get(int32Ty, 0)}, "gep1");

    builder.createLoad(gep1, "val1");
    // GEP with 2 indices: [2 x [3 x [4 x i32]]]* → [4 x i32]*
    auto* gep2 = builder.createGEP(
        arr3DTy, arr3D,
        {ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 1)}, "gep2");
    builder.createLoad(gep2, "val2");

    // GEP with 3 indices: [2 x [3 x [4 x i32]]]* → i32*
    auto* gep3 = builder.createGEP(
        arr3DTy, arr3D,
        {ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 1),
         ConstantInt::get(int32Ty, 2)},
        "gep3");

    // Load the final value
    auto* val3 = builder.createLoad(gep3, "val3");
    builder.createRet(val3);

    EXPECT_EQ(IRPrinter::toString(module.get()),
              R"(; ModuleID = 'multi_index_test'

@arr3d = external global [2 x [3 x [4 x i32]]] [[3 x [4 x i32]] [[4 x i32] [i32 0, i32 1, i32 2, i32 3], [4 x i32] [i32 0, i32 1, i32 2, i32 3], [4 x i32] [i32 0, i32 1, i32 2, i32 3]], [3 x [4 x i32]] [[4 x i32] [i32 0, i32 1, i32 2, i32 3], [4 x i32] [i32 0, i32 1, i32 2, i32 3], [4 x i32] [i32 0, i32 1, i32 2, i32 3]]]

define i32 @test_multi_gep() {
entry:
  %gep1 = getelementptr [2 x [3 x [4 x i32]]], [2 x [3 x [4 x i32]]]* @arr3d, i32 0
  %val1 = load [3 x [4 x i32]], [3 x [4 x i32]]* %gep1
  %gep2 = getelementptr [2 x [3 x [4 x i32]]], [2 x [3 x [4 x i32]]]* @arr3d, i32 0, i32 1
  %val2 = load [4 x i32], [4 x i32]* %gep2
  %gep3 = getelementptr [2 x [3 x [4 x i32]]], [2 x [3 x [4 x i32]]]* @arr3d, i32 0, i32 1, i32 2
  %val3 = load i32, i32* %gep3
  ret i32 %val3
}

)");
}

TEST(ArrayInitializationTest, GEPStrideCalculation) {
    auto context = std::make_unique<Context>();
    auto module = std::make_unique<Module>("stride_test", context.get());
    auto* int32Ty = context->getInt32Type();

    // Create multi-dimensional array: [2][3][4] x i32
    auto* arr1DTy = ArrayType::get(int32Ty, 4);  // [4 x i32] = 16 bytes
    auto* arr2DTy = ArrayType::get(arr1DTy, 3);  // [3 x [4 x i32]] = 48 bytes
    auto* arr3DTy =
        ArrayType::get(arr2DTy, 2);  // [2 x [3 x [4 x i32]]] = 96 bytes

    // Create simple initializer
    std::vector<Constant*> level1;
    for (int k = 0; k < 4; k++) {
        level1.push_back(ConstantInt::get(int32Ty, k));
    }
    auto* arr1DConst = ConstantArray::get(arr1DTy, level1);

    std::vector<Constant*> level2;
    for (int j = 0; j < 3; j++) {
        level2.push_back(arr1DConst);
    }
    auto* arr2DConst = ConstantArray::get(arr2DTy, level2);

    std::vector<Constant*> level3;
    for (int i = 0; i < 2; i++) {
        level3.push_back(arr2DConst);
    }
    auto* arr3DConst = ConstantArray::get(arr3DTy, level3);

    auto* arr3D =
        GlobalVariable::Create(arr3DTy, false, GlobalVariable::ExternalLinkage,
                               arr3DConst, "arr3d", module.get());

    // Create test function
    auto* funcTy = FunctionType::get(int32Ty, {});
    auto* testFunc = Function::Create(funcTy, "test_stride", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", testFunc);
    IRBuilder builder(bb);

    // Test different numbers of indices
    // GEP with 1 index: [2 x [3 x [4 x i32]]]* → [3 x [4 x i32]]*
    auto* gep1 = builder.createGEP(arr3DTy, arr3D,
                                   {ConstantInt::get(int32Ty, 0)}, "gep1");

    // GEP with 2 indices: [2 x [3 x [4 x i32]]]* → [4 x i32]*
    auto* gep2 = builder.createGEP(
        arr3DTy, arr3D,
        {ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 1)}, "gep2");

    // GEP with 3 indices: [2 x [3 x [4 x i32]]]* → i32*
    auto* gep3 = builder.createGEP(
        arr3DTy, arr3D,
        {ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 1),
         ConstantInt::get(int32Ty, 2)},
        "gep3");

    // Load the final value
    auto* val = builder.createLoad(gep3, "val");
    builder.createRet(val);

    // Verify strides
    auto strides1 = gep1->getStrides();
    auto strides2 = gep2->getStrides();
    auto strides3 = gep3->getStrides();

    std::cout << "GEP1 strides: ";
    for (auto stride : strides1) std::cout << stride << " ";
    std::cout << std::endl;

    std::cout << "GEP2 strides: ";
    for (auto stride : strides2) std::cout << stride << " ";
    std::cout << std::endl;

    std::cout << "GEP3 strides: ";
    for (auto stride : strides3) std::cout << stride << " ";
    std::cout << std::endl;

    // Expected strides:
    // GEP1 (1 index on [2 x [3 x [4 x i32]]]): stride = size of [3 x [4 x i32]]
    // = 3 * 4 * 4 = 48 bytes GEP2 (2 indices): stride1 = 48 bytes, stride2 =
    // size of [4 x i32] = 4 * 4 = 16 bytes GEP3 (3 indices): stride1 = 48
    // bytes, stride2 = 16 bytes, stride3 = size of i32 = 4 bytes
    EXPECT_EQ(strides1.size(), 1);
    EXPECT_EQ(strides1[0], 48);  // [3 x [4 x i32]] = 3 * 4 * 4 = 48 bytes

    EXPECT_EQ(strides2.size(), 2);
    EXPECT_EQ(strides2[0], 48);  // [3 x [4 x i32]] = 48 bytes
    EXPECT_EQ(strides2[1], 16);  // [4 x i32] = 4 * 4 = 16 bytes

    EXPECT_EQ(strides3.size(), 3);
    EXPECT_EQ(strides3[0], 48);  // [3 x [4 x i32]] = 48 bytes
    EXPECT_EQ(strides3[1], 16);  // [4 x i32] = 16 bytes
    EXPECT_EQ(strides3[2], 4);   // i32 = 4 bytes
}

TEST(ArrayInitializationTest, FourCubedArrayGEPTest) {
    auto context = std::make_unique<Context>();
    auto module = std::make_unique<Module>("4x4x4_gep_test", context.get());
    auto* int32Ty = context->getInt32Type();

    // Create 4x4x4 array: [4 x [4 x [4 x int]]]
    auto* arr1DTy = ArrayType::get(int32Ty, 4);  // [4 x i32]
    auto* arr2DTy = ArrayType::get(arr1DTy, 4);  // [4 x [4 x i32]]
    auto* arr3DTy = ArrayType::get(arr2DTy, 4);  // [4 x [4 x [4 x i32]]]

    // Create flat array type: [64 x i32] (4*4*4 = 64 elements)
    auto* flatArrayTy = ArrayType::get(int32Ty, 64);  // [64 x i32]

    // Create single global array with 3D layout
    auto* array =
        GlobalVariable::Create(arr3DTy, false, GlobalVariable::ExternalLinkage,
                               nullptr, "array", module.get());

    // Create test function
    auto* funcTy = FunctionType::get(int32Ty, {});
    auto* testFunc = Function::Create(funcTy, "test_4x4x4_gep", module.get());
    auto* bb = BasicBlock::Create(context.get(), "entry", testFunc);
    IRBuilder builder(bb);

    // Access array[3][2][1] using multi-dimensional GEP with [4 x [4 x [4 x
    // i32]]] type
    auto* aPtr = builder.createGEP(
        arr3DTy, array,
        {ConstantInt::get(int32Ty, 3), ConstantInt::get(int32Ty, 2),
         ConstantInt::get(int32Ty, 1)},
        "a.3.2.1.ptr");
    auto* aVal = builder.createLoad(aPtr, "a.3.2.1");

    // Access the same array as b[57] using single-index GEP with [64 x i32]
    // type Index 57 corresponds to [3][2][1] in flattened form: 3*16 + 2*4 + 1
    // = 57
    auto* bPtr = builder.createGEP(flatArrayTy, array,
                                   {ConstantInt::get(int32Ty, 57)}, "b.57.ptr");
    auto* bVal = builder.createLoad(bPtr, "b.57");

    // Add the two values and return
    auto* result = builder.createAdd(aVal, bVal, "result");
    builder.createRet(result);

    // Verify the module structure
    EXPECT_EQ(module->globals().size(), 1);  // single array
    EXPECT_EQ(module->size(), 1);            // test function

    // Verify sizes
    EXPECT_EQ(aPtr->getSourceElementType()->getSizeInBytes(), 256);
    EXPECT_EQ(bPtr->getSourceElementType()->getSizeInBytes(), 256);

    // Verify array type
    auto* arrayType = dyn_cast<ArrayType>(array->getValueType());
    ASSERT_NE(arrayType, nullptr);
    EXPECT_EQ(arrayType->getNumElements(), 4);

    EXPECT_EQ(IRPrinter::toString(module.get()),
              R"(; ModuleID = '4x4x4_gep_test'

@array = external global [4 x [4 x [4 x i32]]]

define i32 @test_4x4x4_gep() {
entry:
  %a.3.2.1.ptr = getelementptr [4 x [4 x [4 x i32]]], [4 x [4 x [4 x i32]]]* @array, i32 3, i32 2, i32 1
  %a.3.2.1 = load i32, i32* %a.3.2.1.ptr
  %b.57.ptr = getelementptr [64 x i32], [4 x [4 x [4 x i32]]]* @array, i32 57
  %b.57 = load i32, i32* %b.57.ptr
  %result = add i32 %a.3.2.1, %b.57
  ret i32 %result
}

)");
}
