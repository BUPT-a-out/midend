#include <gtest/gtest.h>

#include <memory>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Pass.h"
#include "Pass/Transform/InstCombinePass.h"

using namespace midend;

class InstCombinePassStoreLoadTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ctx = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", ctx.get());
        builder = std::make_unique<IRBuilder>(ctx.get());
        am = std::make_unique<AnalysisManager>();
    }

    std::unique_ptr<Context> ctx;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

TEST_F(InstCombinePassStoreLoadTest, BasicStoreToLoadForwarding) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType = FunctionType::get(i32Type, {ptrType, i32Type});
    auto* func =
        Function::Create(funcType, "test_basic_forwarding", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr = func->getArg(0);
    auto* val = func->getArg(1);

    // Store followed immediately by load from same address
    builder->createStore(val, ptr);
    auto* load = builder->createLoad(ptr, "loaded");
    builder->createRet(load);

    // Before pass - expect store and load
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_basic_forwarding(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  %loaded = load i32, i32* %arg0
  ret i32 %loaded
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect load to be replaced with stored value
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_basic_forwarding(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  ret i32 %arg1
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, MultipleLoadsFromSameStore) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType = FunctionType::get(i32Type, {ptrType, i32Type});
    auto* func =
        Function::Create(funcType, "test_multiple_loads", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr = func->getArg(0);
    auto* val = func->getArg(1);

    // Store followed by multiple loads from same address
    builder->createStore(val, ptr);
    auto* load1 = builder->createLoad(ptr, "load1");
    auto* load2 = builder->createLoad(ptr, "load2");
    auto* load3 = builder->createLoad(ptr, "load3");
    auto* result = builder->createAdd(load1, load2);
    result = builder->createAdd(result, load3);
    builder->createRet(result);

    // Before pass - expect store and three loads
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_multiple_loads(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  %load1 = load i32, i32* %arg0
  %load2 = load i32, i32* %arg0
  %load3 = load i32, i32* %arg0
  %0 = add i32 %load1, %load2
  %1 = add i32 %0, %load3
  ret i32 %1
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect all loads to be replaced with stored value
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_multiple_loads(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  %0 = add i32 %arg1, %arg1
  %1 = add i32 %0, %arg1
  ret i32 %1
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, InterveningNonModifyingInstructions) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType = FunctionType::get(i32Type, {ptrType, i32Type, i32Type});
    auto* func =
        Function::Create(funcType, "test_intervening_safe", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr = func->getArg(0);
    auto* val1 = func->getArg(1);
    auto* val2 = func->getArg(2);

    // Store, then some non-memory-modifying instructions, then load
    builder->createStore(val1, ptr);
    auto* add = builder->createAdd(val1, val2, "temp");
    auto* mul = builder->createMul(add, val2, "temp2");
    auto* load = builder->createLoad(ptr, "loaded");
    auto* result = builder->createAdd(load, mul);
    builder->createRet(result);

    // Before pass - expect store, arithmetic ops, and load
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_intervening_safe(i32* %arg0, i32 %arg1, i32 %arg2) {
entry:
  store i32 %arg1, i32* %arg0
  %temp = add i32 %arg1, %arg2
  %temp2 = mul i32 %temp, %arg2
  %loaded = load i32, i32* %arg0
  %0 = add i32 %loaded, %temp2
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect load to be replaced with stored value
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_intervening_safe(i32* %arg0, i32 %arg1, i32 %arg2) {
entry:
  store i32 %arg1, i32* %arg0
  %temp = add i32 %arg1, %arg2
  %temp2 = mul i32 %temp, %arg2
  %0 = add i32 %arg1, %temp2
  ret i32 %0
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, InterveningCallInstruction) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* voidType = ctx->getVoidType();

    // Create a dummy external function to call
    auto* calleeType = FunctionType::get(voidType, {});
    auto* callee = Function::Create(calleeType, "external_func", module.get());

    auto* funcType = FunctionType::get(i32Type, {ptrType, i32Type});
    auto* func =
        Function::Create(funcType, "test_intervening_call", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr = func->getArg(0);
    auto* val = func->getArg(1);

    // Store, then call (potentially memory-modifying), then load
    builder->createStore(val, ptr);
    builder->createCall(callee, {});
    auto* load = builder->createLoad(ptr, "loaded");
    builder->createRet(load);

    // Before pass - expect store, call, and load
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_intervening_call(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  call void @external_func()
  %loaded = load i32, i32* %arg0
  ret i32 %loaded
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Expect NO change because call could modify memory
    EXPECT_FALSE(changed);

    // After pass - expect no changes (load should remain)
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_intervening_call(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  call void @external_func()
  %loaded = load i32, i32* %arg0
  ret i32 %loaded
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, DifferentAddresses) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType = FunctionType::get(i32Type, {ptrType, ptrType, i32Type});
    auto* func =
        Function::Create(funcType, "test_different_addresses", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr1 = func->getArg(0);
    auto* ptr2 = func->getArg(1);
    auto* val = func->getArg(2);

    // Store to one address, load from a different address
    builder->createStore(val, ptr1);
    auto* load = builder->createLoad(ptr2, "loaded");
    builder->createRet(load);

    // Before pass - expect store and load with different pointers
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_different_addresses(i32* %arg0, i32* %arg1, i32 %arg2) {
entry:
  store i32 %arg2, i32* %arg0
  %loaded = load i32, i32* %arg1
  ret i32 %loaded
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Expect NO change because addresses are different
    EXPECT_FALSE(changed);

    // After pass - expect no changes
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_different_addresses(i32* %arg0, i32* %arg1, i32 %arg2) {
entry:
  store i32 %arg2, i32* %arg0
  %loaded = load i32, i32* %arg1
  ret i32 %loaded
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, ChainOfStores) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType =
        FunctionType::get(i32Type, {ptrType, i32Type, i32Type, i32Type});
    auto* func =
        Function::Create(funcType, "test_chain_of_stores", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr = func->getArg(0);
    auto* val1 = func->getArg(1);
    auto* val2 = func->getArg(2);
    auto* val3 = func->getArg(3);

    // Chain of stores to same address, then load - only most recent should be
    // forwarded
    builder->createStore(val1, ptr);
    builder->createStore(val2, ptr);
    builder->createStore(val3, ptr);
    auto* load = builder->createLoad(ptr, "loaded");
    builder->createRet(load);

    // Before pass - expect three stores and one load
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_chain_of_stores(i32* %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  store i32 %arg1, i32* %arg0
  store i32 %arg2, i32* %arg0
  store i32 %arg3, i32* %arg0
  %loaded = load i32, i32* %arg0
  ret i32 %loaded
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect load to be replaced with the most recent stored value
    // (val3)
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_chain_of_stores(i32* %arg0, i32 %arg1, i32 %arg2, i32 %arg3) {
entry:
  store i32 %arg1, i32* %arg0
  store i32 %arg2, i32* %arg0
  store i32 %arg3, i32* %arg0
  ret i32 %arg3
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, StoreInDifferentBlock) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType = FunctionType::get(i32Type, {ptrType, i32Type});
    auto* func =
        Function::Create(funcType, "test_different_blocks", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    auto* loadBlock = BasicBlock::Create(ctx.get(), "load_block", func);

    builder->setInsertPoint(entry);
    auto* ptr = func->getArg(0);
    auto* val = func->getArg(1);

    // Store in first block
    builder->createStore(val, ptr);
    builder->createBr(loadBlock);

    // Load in second block
    builder->setInsertPoint(loadBlock);
    auto* load = builder->createLoad(ptr, "loaded");
    builder->createRet(load);

    // Before pass - expect store in entry block, load in load_block
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_different_blocks(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  br label %load_block
load_block:
  %loaded = load i32, i32* %arg0
  ret i32 %loaded
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    // Expect NO change because store and load are in different blocks
    EXPECT_FALSE(changed);

    // After pass - expect no changes
    EXPECT_EQ(IRPrinter().print(func),
              R"(define i32 @test_different_blocks(i32* %arg0, i32 %arg1) {
entry:
  store i32 %arg1, i32* %arg0
  br label %load_block
load_block:
  %loaded = load i32, i32* %arg0
  ret i32 %loaded
}
)");
}

TEST_F(InstCombinePassStoreLoadTest, ComplexScenarioWithMultipleOptimizations) {
    auto* i32Type = IntegerType::get(ctx.get(), 32);
    auto* ptrType = PointerType::get(i32Type);
    auto* funcType = FunctionType::get(i32Type, {ptrType, ptrType, i32Type});
    auto* func =
        Function::Create(funcType, "test_complex_scenario", module.get());

    auto* entry = BasicBlock::Create(ctx.get(), "entry", func);
    builder->setInsertPoint(entry);

    auto* ptr1 = func->getArg(0);
    auto* ptr2 = func->getArg(1);
    auto* val = func->getArg(2);
    auto* zero = ConstantInt::get(i32Type, 0);
    auto* one = ConstantInt::get(i32Type, 1);

    // Complex scenario: store-to-load forwarding combined with arithmetic
    // optimizations
    builder->createStore(val, ptr1);
    auto* load1 = builder->createLoad(ptr1, "load1");  // Should be forwarded
    auto* add_zero = builder->createAdd(
        load1, zero, "add_zero");  // Should be optimized to load1
    auto* mul_one = builder->createMul(
        add_zero, one, "mul_one");  // Should be optimized to add_zero

    // Store to different pointer, then load - should NOT be forwarded
    builder->createStore(mul_one, ptr2);
    auto* load2 =
        builder->createLoad(ptr1, "load2");  // Should be forwarded to val

    auto* result = builder->createAdd(mul_one, load2);
    builder->createRet(result);

    // Before pass - expect multiple operations
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_complex_scenario(i32* %arg0, i32* %arg1, i32 %arg2) {
entry:
  store i32 %arg2, i32* %arg0
  %load1 = load i32, i32* %arg0
  %add_zero = add i32 %load1, 0
  %mul_one = mul i32 %add_zero, 1
  store i32 %mul_one, i32* %arg1
  %load2 = load i32, i32* %arg0
  %0 = add i32 %mul_one, %load2
  ret i32 %0
}
)");

    InstCombinePass pass;
    bool changed = pass.runOnFunction(*func, *am);

    EXPECT_TRUE(changed);

    // After pass - expect all optimizations:
    // 1. load1 -> val
    // 2. add_zero (val + 0) -> val
    // 3. mul_one (val * 1) -> val
    // 4. load2 -> val
    // Final result: val + val
    EXPECT_EQ(
        IRPrinter().print(func),
        R"(define i32 @test_complex_scenario(i32* %arg0, i32* %arg1, i32 %arg2) {
entry:
  store i32 %arg2, i32* %arg0
  store i32 %arg2, i32* %arg1
  %0 = add i32 %arg2, %arg2
  ret i32 %0
}
)");
}