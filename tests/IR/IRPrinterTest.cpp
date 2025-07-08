#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Module.h"
#include "IR/Type.h"

using namespace midend;

class IRPrinterTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
};

TEST_F(IRPrinterTest, PrintEmptyModule) {
    std::string output = IRPrinter::toString(module.get());
    std::string expected = "; ModuleID = 'test_module'\n\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintFunctionDeclaration) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* func = Function::Create(fnTy, "add", module.get());

    std::string output = IRPrinter::toString(func);
    std::string expected = "define i32 @add(i32 %0, i32 %1)\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintSimpleFunction) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* func = Function::Create(fnTy, "add", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);

    auto* sum = builder.createAdd(func->getArg(0), func->getArg(1), "sum");
    builder.createRet(sum);

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define i32 @add(i32 %0, i32 %1) {\n"
        "entry:\n"
        "  %sum = add i32 %0, %1\n"
        "  ret i32 %sum\n"
        "}\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintArithmeticOperations) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* func = Function::Create(fnTy, "arithmetic", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);

    auto* a = builder.getInt32(10);
    auto* b = builder.getInt32(20);

    auto* add = builder.createAdd(a, b, "add");
    builder.createSub(a, b, "sub");
    builder.createMul(a, b, "mul");
    builder.createDiv(a, b, "div");
    builder.createRem(a, b, "rem");

    builder.createRet(add);

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define i32 @arithmetic() {\n"
        "entry:\n"
        "  %add = add i32 10, 20\n"
        "  %sub = sub i32 10, 20\n"
        "  %mul = mul i32 10, 20\n"
        "  %div = sdiv i32 10, 20\n"
        "  %rem = srem i32 10, 20\n"
        "  ret i32 %add\n"
        "}\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintComparisonOperations) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(int32Ty, {});
    auto* func = Function::Create(fnTy, "compare", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);

    auto* a = builder.getInt32(10);
    auto* b = builder.getInt32(20);

    builder.createICmpEQ(a, b, "eq");
    builder.createICmpNE(a, b, "ne");
    builder.createICmpSLT(a, b, "lt");
    builder.createICmpSLE(a, b, "le");
    builder.createICmpSGT(a, b, "gt");
    builder.createICmpSGE(a, b, "ge");

    builder.createRet(builder.getInt32(0));

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define i32 @compare() {\n"
        "entry:\n"
        "  %eq = icmp eq i32 10, 20\n"
        "  %ne = icmp ne i32 10, 20\n"
        "  %lt = icmp slt i32 10, 20\n"
        "  %le = icmp sle i32 10, 20\n"
        "  %gt = icmp sgt i32 10, 20\n"
        "  %ge = icmp sge i32 10, 20\n"
        "  ret i32 0\n"
        "}\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintMemoryOperations) {
    auto* int32Ty = context->getInt32Type();
    auto* fnTy = FunctionType::get(context->getVoidType(), {});
    auto* func = Function::Create(fnTy, "memory", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);

    auto* alloca = builder.createAlloca(int32Ty, nullptr, "x");
    auto* val = builder.getInt32(42);
    builder.createStore(val, alloca);
    builder.createLoad(alloca, "loaded");

    builder.createRetVoid();

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define void @memory() {\n"
        "entry:\n"
        "  %x = alloca i32\n"
        "  store i32 42, i32* %x\n"
        "  %loaded = load i32, i32* %x\n"
        "  ret void\n"
        "}\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintControlFlow) {
    auto* int32Ty = context->getInt32Type();
    auto* boolTy = context->getInt1Type();
    auto* fnTy = FunctionType::get(int32Ty, {boolTy});
    auto* func = Function::Create(fnTy, "control_flow", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    auto* thenBB = BasicBlock::Create(context.get(), "then", func);
    auto* elseBB = BasicBlock::Create(context.get(), "else", func);
    auto* mergeBB = BasicBlock::Create(context.get(), "merge", func);

    IRBuilder builder(entry);
    builder.createCondBr(func->getArg(0), thenBB, elseBB);

    builder.setInsertPoint(thenBB);
    auto* thenVal = builder.getInt32(1);
    builder.createBr(mergeBB);

    builder.setInsertPoint(elseBB);
    auto* elseVal = builder.getInt32(2);
    builder.createBr(mergeBB);

    builder.setInsertPoint(mergeBB);
    auto* phi = builder.createPHI(int32Ty, "result");
    phi->addIncoming(thenVal, thenBB);
    phi->addIncoming(elseVal, elseBB);
    builder.createRet(phi);

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define i32 @control_flow(i1 %0) {\n"
        "entry:\n"
        "  br i1 %0, label %then, label %else\n"
        "then:\n"
        "  br label %merge\n"
        "else:\n"
        "  br label %merge\n"
        "merge:\n"
        "  %result = phi i32 [ 1, %then ], [ 2, %else ]\n"
        "  ret i32 %result\n"
        "}\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintGlobalVariables) {
    auto* int32Ty = context->getInt32Type();
    GlobalVariable::Create(int32Ty, false, GlobalVariable::ExternalLinkage,
                           nullptr, "global_var", module.get());

    GlobalVariable::Create(int32Ty, false, GlobalVariable::InternalLinkage,
                           ConstantInt::get(int32Ty, 42), "initialized_var",
                           module.get());

    std::string output = IRPrinter::toString(module.get());

    // Verify exact module IR structure
    std::string expected =
        "; ModuleID = 'test_module'\n\n"
        "@global_var = external global i32\n"
        "@initialized_var = internal global i32 42\n\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintCompleteModule) {
    // Create a simple main function
    auto* int32Ty = context->getInt32Type();
    auto* mainTy = FunctionType::get(int32Ty, {});
    auto* main = Function::Create(mainTy, "main", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", main);
    IRBuilder builder(entry);
    builder.createRet(builder.getInt32(0));

    // Create a helper function
    auto* addTy = FunctionType::get(int32Ty, {int32Ty, int32Ty});
    auto* add = Function::Create(addTy, "add", module.get());

    auto* addEntry = BasicBlock::Create(context.get(), "entry", add);
    builder.setInsertPoint(addEntry);
    auto* sum = builder.createAdd(add->getArg(0), add->getArg(1), "sum");
    builder.createRet(sum);

    std::string output = IRPrinter::toString(module.get());

    // Verify exact module IR structure
    std::string expected =
        "; ModuleID = 'test_module'\n\n"
        "define i32 @main() {\n"
        "entry:\n"
        "  ret i32 0\n"
        "}\n\n"
        "define i32 @add(i32 %0, i32 %1) {\n"
        "entry:\n"
        "  %sum = add i32 %0, %1\n"
        "  ret i32 %sum\n"
        "}\n\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintTypes) {
    auto* voidTy = context->getVoidType();
    auto* int1Ty = context->getInt1Type();
    auto* int32Ty = context->getInt32Type();
    auto* floatTy = context->getFloatType();

    // Create a function that uses various types
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_types", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);

    // Test various type allocations
    builder.createAlloca(int1Ty, nullptr, "bool_var");
    builder.createAlloca(int32Ty, nullptr, "int_var");
    builder.createAlloca(floatTy, nullptr, "float_var");

    // Test pointer types
    auto* int32PtrTy = int32Ty->getPointerTo();
    builder.createAlloca(int32PtrTy, nullptr, "ptr_var");

    // Test array types
    auto* arrayTy = ArrayType::get(int32Ty, 10);
    builder.createAlloca(arrayTy, nullptr, "array_var");

    builder.createRetVoid();

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define void @test_types() {\n"
        "entry:\n"
        "  %bool_var = alloca i1\n"
        "  %int_var = alloca i32\n"
        "  %float_var = alloca float\n"
        "  %ptr_var = alloca i32*\n"
        "  %array_var = alloca [10 x i32]\n"
        "  ret void\n"
        "}\n";
    EXPECT_EQ(output, expected);
}

TEST_F(IRPrinterTest, PrintConstants) {
    auto* floatTy = context->getFloatType();
    auto* voidTy = context->getVoidType();
    auto* fnTy = FunctionType::get(voidTy, {});
    auto* func = Function::Create(fnTy, "test_constants", module.get());

    auto* entry = BasicBlock::Create(context.get(), "entry", func);
    IRBuilder builder(entry);

    // Test integer constants
    auto* c1 = builder.getInt32(42);
    auto* c2 = builder.getInt32(-100);
    auto* c3 = builder.getInt32(1000000);

    // Test floating point constants
    auto* f1 = ConstantFP::get(floatTy, 3.14159f);
    auto* f2 = ConstantFP::get(floatTy, -2.71828f);

    // Use them in operations
    auto* sum = builder.createAdd(c1, c2, "sum");
    builder.createMul(sum, c3, "product");
    auto* x = builder.createAlloca(floatTy, nullptr, "x");
    builder.createStore(f1, x);
    auto* y = builder.createAlloca(floatTy, nullptr, "y");
    builder.createStore(f2, y);
    builder.createLoad(x, "loaded_x");
    builder.createLoad(y, "loaded_y");

    builder.createRetVoid();

    std::string output = IRPrinter::toString(func);

    // Verify exact IR structure
    std::string expected =
        "define void @test_constants() {\n"
        "entry:\n"
        "  %sum = add i32 42, 4294967196\n"
        "  %product = mul i32 %sum, 1000000\n"
        "  %x = alloca float\n"
        "  store float 3.141590e+00, float* %x\n"
        "  %y = alloca float\n"
        "  store float -2.718280e+00, float* %y\n"
        "  %loaded_x = load float, float* %x\n"
        "  %loaded_y = load float, float* %y\n"
        "  ret void\n"
        "}\n";
    EXPECT_EQ(output, expected);
}