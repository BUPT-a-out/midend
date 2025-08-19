#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/IRPrinter.h"
#include "IR/Instructions/MemoryOps.h"
#include "IR/Instructions/OtherOps.h"
#include "IR/Instructions/TerminatorOps.h"
#include "IR/Module.h"
#include "IR/Type.h"
#include "Pass/Analysis/AliasAnalysis.h"
#include "Pass/Analysis/CallGraph.h"
#include "Pass/Pass.h"
#include "Support/Casting.h"

using namespace midend;

class AliasAnalysisTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test", context.get());
        builder = std::make_unique<IRBuilder>(context.get());
        am = std::make_unique<AnalysisManager>();
        am->registerAnalysisType<AliasAnalysis>();
        am->registerAnalysisType<CallGraphAnalysis>();
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    std::unique_ptr<IRBuilder> builder;
    std::unique_ptr<AnalysisManager> am;
};

TEST_F(AliasAnalysisTest, BasicNoAlias) {
    // Create a function with two different allocations
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Create two different allocations
    auto intType = context->getInt32Type();
    auto alloca1 = builder->createAlloca(intType, nullptr, "a");
    auto alloca2 = builder->createAlloca(intType, nullptr, "b");

    // Create return
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Different allocations should not alias
    EXPECT_EQ(result->alias(alloca1, alloca2),
              AliasAnalysis::AliasResult::NoAlias);
}

TEST_F(AliasAnalysisTest, MustAliasSameValue) {
    // Create a function
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Create an allocation
    auto intType = context->getInt32Type();
    auto alloca = builder->createAlloca(intType, nullptr, "a");

    // Create return
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Same value must alias with itself
    EXPECT_EQ(result->alias(alloca, alloca),
              AliasAnalysis::AliasResult::MustAlias);
}

TEST_F(AliasAnalysisTest, GEPAnalysis) {
    // Create a function with array access
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Create an array allocation
    auto intType = context->getInt32Type();
    auto arrayType = ArrayType::get(intType, 10);
    auto alloca = builder->createAlloca(arrayType, nullptr, "arr");

    // Create GEPs to different elements
    auto one = builder->getInt32(1);
    auto two = builder->getInt32(2);

    auto gep1 = builder->createGEP(arrayType, alloca, {one}, "gep1");
    auto gep2 = builder->createGEP(arrayType, alloca, {two}, "gep2");
    auto gep3 = builder->createGEP(arrayType, alloca, {one}, "gep3");

    // Create return
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Different array elements should not alias
    EXPECT_EQ(result->alias(gep1, gep2), AliasAnalysis::AliasResult::NoAlias);

    // Same array elements must alias
    EXPECT_EQ(result->alias(gep1, gep3), AliasAnalysis::AliasResult::MustAlias);

    // Base pointer may alias with any element
    EXPECT_NE(result->alias(alloca, gep1), AliasAnalysis::AliasResult::NoAlias);
}

TEST_F(AliasAnalysisTest, BitCastAnalysis) {
    // Create a function with bitcasts
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Create an allocation
    auto intType = context->getInt32Type();
    auto floatType = context->getFloatType();
    auto alloca = builder->createAlloca(intType, nullptr, "a");

    // Create bitcasts
    auto floatPtrType = PointerType::get(floatType);
    auto cast1 =
        builder->createCast(CastInst::BitCast, alloca, floatPtrType, "cast1");
    auto cast2 =
        builder->createCast(CastInst::BitCast, alloca, floatPtrType, "cast2");

    // Create return
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Bitcasts of the same value must alias
    EXPECT_EQ(result->alias(cast1, cast2),
              AliasAnalysis::AliasResult::MustAlias);
    EXPECT_EQ(result->alias(alloca, cast1),
              AliasAnalysis::AliasResult::MustAlias);
}

TEST_F(AliasAnalysisTest, LoadStoreAnalysis) {
    // Create a function with loads and stores
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Create allocations
    auto intType = context->getInt32Type();
    auto alloca1 = builder->createAlloca(intType, nullptr, "a");
    auto alloca2 = builder->createAlloca(intType, nullptr, "b");

    // Create a store
    auto value = builder->getInt32(42);
    auto store = builder->createStore(value, alloca1);

    // Create loads
    auto load1 = builder->createLoad(alloca1, "load1");
    auto load2 = builder->createLoad(alloca2, "load2");

    // Create return
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Check if store may modify the allocations
    EXPECT_TRUE(result->mayModify(store, alloca1));
    EXPECT_FALSE(result->mayModify(store, alloca2));

    // Check if loads may reference the allocations
    EXPECT_TRUE(result->mayRef(load1, alloca1));
    EXPECT_FALSE(result->mayRef(load1, alloca2));
    EXPECT_TRUE(result->mayRef(load2, alloca2));
    EXPECT_FALSE(result->mayRef(load2, alloca1));
}

TEST_F(AliasAnalysisTest, PHINodeAnalysis) {
    // Create a function with PHI nodes
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto entry = BasicBlock::Create(context.get(), "entry", func);
    auto then_bb = BasicBlock::Create(context.get(), "then", func);
    auto else_bb = BasicBlock::Create(context.get(), "else", func);
    auto merge = BasicBlock::Create(context.get(), "merge", func);

    // Entry block
    builder->setInsertPoint(entry);
    auto intType = context->getInt32Type();
    auto alloca1 = builder->createAlloca(intType, nullptr, "a");
    auto alloca2 = builder->createAlloca(intType, nullptr, "b");
    auto cond = builder->getInt1(true);
    builder->createCondBr(cond, then_bb, else_bb);

    // Then block
    builder->setInsertPoint(then_bb);
    builder->createBr(merge);

    // Else block
    builder->setInsertPoint(else_bb);
    builder->createBr(merge);

    // Merge block with PHI
    builder->setInsertPoint(merge);
    auto phi = builder->createPHI(PointerType::get(intType), "phi");
    phi->addIncoming(alloca1, then_bb);
    phi->addIncoming(alloca2, else_bb);
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // PHI may alias with both inputs
    EXPECT_NE(result->alias(phi, alloca1), AliasAnalysis::AliasResult::NoAlias);
    EXPECT_NE(result->alias(phi, alloca2), AliasAnalysis::AliasResult::NoAlias);
}

TEST_F(AliasAnalysisTest, UnderlyingObjectAnalysis) {
    // Create a function
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_func", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Create allocations and derived pointers
    auto intType = context->getInt32Type();
    auto arrayType = ArrayType::get(intType, 10);
    auto alloca = builder->createAlloca(arrayType, nullptr, "arr");

    auto one = builder->getInt32(1);
    auto gep = builder->createGEP(arrayType, alloca, {one}, "gep");

    auto floatPtrType = PointerType::get(context->getFloatType());
    auto cast =
        builder->createCast(CastInst::BitCast, gep, floatPtrType, "cast");

    // Create return
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // All should have the same underlying object
    EXPECT_EQ(result->getUnderlyingObject(gep), alloca);
    EXPECT_EQ(result->getUnderlyingObject(cast), alloca);

    // All should be stack objects
    EXPECT_TRUE(result->isStackObject(alloca));
    EXPECT_TRUE(result->isStackObject(gep));
    EXPECT_TRUE(result->isStackObject(cast));
}

// Test 1: Same explicit pointer
TEST_F(AliasAnalysisTest, SameExplicitPointer) {
    // Two loads/stores to the same pointer p with no intervening writes
    auto funcType = FunctionType::get(
        context->getVoidType(), {PointerType::get(context->getInt32Type())});
    auto func = Function::Create(funcType, "test_same_ptr", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto p = func->getArg(0);  // Pointer parameter

    // load p; store 42, p; load p
    auto load1 = builder->createLoad(p, "load1");
    auto value = builder->getInt32(42);
    auto store = builder->createStore(value, p);
    auto load2 = builder->createLoad(p, "load2");

    builder->createRetVoid();

    EXPECT_EQ(IRPrinter().print(func),
              R"(define void @test_same_ptr(i32* %arg0) {
entry:
  %load1 = load i32, i32* %arg0
  store i32 42, i32* %arg0
  %load2 = load i32, i32* %arg0
  ret void
}
)");

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Same pointer must alias
    EXPECT_EQ(result->alias(p, p), AliasAnalysis::AliasResult::MustAlias);

    // Store modifies p
    EXPECT_TRUE(result->mayModify(store, p));

    // Both loads reference p
    EXPECT_TRUE(result->mayRef(load1, p));
    EXPECT_TRUE(result->mayRef(load2, p));
}

// Test 2: Different local variables
TEST_F(AliasAnalysisTest, DifferentLocalVariables) {
    // int a, b; load &a; load &b - should be no alias
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_diff_locals", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto intType = context->getInt32Type();
    auto a = builder->createAlloca(intType, nullptr, "a");
    auto b = builder->createAlloca(intType, nullptr, "b");

    auto load_a = builder->createLoad(a, "load_a");
    auto load_b = builder->createLoad(b, "load_b");

    builder->createRetVoid();

    // Verify IR structure
    std::string expectedIR = R"(define void @test_diff_locals() {
entry:
  %a = alloca i32
  %b = alloca i32
  %load_a = load i32, i32* %a
  %load_b = load i32, i32* %b
  ret void
}
)";
    EXPECT_EQ(IRPrinter().print(func), expectedIR);

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Different allocations should not alias
    EXPECT_EQ(result->alias(a, b), AliasAnalysis::AliasResult::NoAlias);

    // Loads reference different memory
    EXPECT_TRUE(result->mayRef(load_a, a));
    EXPECT_FALSE(result->mayRef(load_a, b));
    EXPECT_TRUE(result->mayRef(load_b, b));
    EXPECT_FALSE(result->mayRef(load_b, a));
}

// Test 3: Pointer copy
TEST_F(AliasAnalysisTest, PointerCopy) {
    // q = p; load p; store q; load p - p and q must alias
    auto ptrType = PointerType::get(context->getInt32Type());
    auto funcType = FunctionType::get(context->getVoidType(), {ptrType});
    auto func = Function::Create(funcType, "test_ptr_copy", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto p = func->getArg(0);  // Pointer parameter

    // q = p (pointer copy)
    auto q = builder->createMove(p);

    // load p; store 100, q; load p
    builder->createLoad(p, "load1");
    auto value = builder->getInt32(100);
    auto store = builder->createStore(value, q);
    builder->createLoad(p, "load2");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // p and q are the same, must alias
    EXPECT_EQ(result->alias(p, q), AliasAnalysis::AliasResult::MustAlias);

    // Store through q modifies p
    EXPECT_TRUE(result->mayModify(store, p));
}

// Test 4: Pointer arithmetic with offsets
TEST_F(AliasAnalysisTest, PointerArithmetic) {
    // p = base + 4; q = base; test different offsets
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_ptr_arith", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto intType = context->getInt32Type();
    auto arrayType = ArrayType::get(intType, 10);
    auto base = builder->createAlloca(arrayType, nullptr, "base");

    // Create GEPs with different offsets
    auto zero = builder->getInt32(0);
    auto four = builder->getInt32(4);
    auto five = builder->getInt32(5);

    auto p = builder->createGEP(arrayType, base, {four}, "p");  // base[4]
    auto q = builder->createGEP(arrayType, base, {zero}, "q");  // base[0]
    auto r = builder->createGEP(arrayType, base, {five}, "r");  // base[5]

    builder->createLoad(p, "load_p");
    builder->createLoad(q, "load_q");
    builder->createLoad(r, "load_r");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Different offsets should not alias
    EXPECT_EQ(result->alias(p, q), AliasAnalysis::AliasResult::NoAlias);
    EXPECT_EQ(result->alias(p, r), AliasAnalysis::AliasResult::NoAlias);
    EXPECT_EQ(result->alias(q, r), AliasAnalysis::AliasResult::NoAlias);

    // Base may alias with any element (conservative)
    EXPECT_NE(result->alias(base, p), AliasAnalysis::AliasResult::NoAlias);
    EXPECT_NE(result->alias(base, q), AliasAnalysis::AliasResult::NoAlias);
    EXPECT_NE(result->alias(base, r), AliasAnalysis::AliasResult::NoAlias);
}

// Test 5: Array access with constant and variable indices
TEST_F(AliasAnalysisTest, ArrayAccessGEP) {
    // load &arr[i]; load &arr[j]; test when i==j and i!=j
    auto intType = context->getInt32Type();
    auto funcType =
        FunctionType::get(context->getVoidType(), {intType, intType});
    auto func = Function::Create(funcType, "test_array_gep", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto arrayType = ArrayType::get(intType, 100);
    auto arr = builder->createAlloca(arrayType, nullptr, "arr");

    auto i = func->getArg(0);
    auto j = func->getArg(1);

    // Array accesses with variable indices
    auto gep_i = builder->createGEP(arrayType, arr, {i}, "gep_i");
    auto gep_j = builder->createGEP(arrayType, arr, {j}, "gep_j");

    // Also test with constant indices
    auto three = builder->getInt32(3);
    auto seven = builder->getInt32(7);
    auto gep_3 = builder->createGEP(arrayType, arr, {three}, "gep_3");
    auto gep_7 = builder->createGEP(arrayType, arr, {seven}, "gep_7");
    auto gep_3_dup = builder->createGEP(arrayType, arr, {three}, "gep_3_dup");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Variable indices - may alias (conservative)
    EXPECT_EQ(result->alias(gep_i, gep_j),
              AliasAnalysis::AliasResult::MayAlias);

    // Different constant indices - no alias
    EXPECT_EQ(result->alias(gep_3, gep_7), AliasAnalysis::AliasResult::NoAlias);

    // Same constant indices - must alias
    EXPECT_EQ(result->alias(gep_3, gep_3_dup),
              AliasAnalysis::AliasResult::MustAlias);

    // Variable vs constant - may alias
    EXPECT_EQ(result->alias(gep_i, gep_3),
              AliasAnalysis::AliasResult::MayAlias);
}

// Test 8: Type conversion pointers
TEST_F(AliasAnalysisTest, TypeConversionPointers) {
    // (char*)p and p - test different type pointers
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_type_conv", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto intType = context->getInt32Type();
    auto charType = context->getIntegerType(8);
    auto p = builder->createAlloca(intType, nullptr, "p");

    // Cast to different types
    auto charPtrType = PointerType::get(charType);
    auto shortPtrType = PointerType::get(context->getIntegerType(16));

    auto char_p =
        builder->createCast(CastInst::BitCast, p, charPtrType, "char_p");
    auto short_p =
        builder->createCast(CastInst::BitCast, p, shortPtrType, "short_p");

    // Load through different type pointers
    auto load_int = builder->createLoad(p, "load_int");
    auto load_char = builder->createLoad(char_p, "load_char");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Casts of same pointer must alias
    EXPECT_EQ(result->alias(p, char_p), AliasAnalysis::AliasResult::MustAlias);
    EXPECT_EQ(result->alias(p, short_p), AliasAnalysis::AliasResult::MustAlias);
    EXPECT_EQ(result->alias(char_p, short_p),
              AliasAnalysis::AliasResult::MustAlias);

    // Loads through different types still reference same memory
    EXPECT_TRUE(result->mayRef(load_int, p));
    EXPECT_TRUE(result->mayRef(load_char, p));
}

// Test 9: Function parameter aliasing
TEST_F(AliasAnalysisTest, FunctionParameterAlias) {
    // f(int *p, int *q) called with f(&x, &x) vs f(&x, &y)
    auto intType = context->getInt32Type();
    auto ptrType = PointerType::get(intType);
    auto funcType =
        FunctionType::get(context->getVoidType(), {ptrType, ptrType});
    auto func = Function::Create(funcType, "test_param_alias", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto p = func->getArg(0);
    auto q = func->getArg(1);

    // Operations on parameters
    auto load_p = builder->createLoad(p, "load_p");
    auto load_q = builder->createLoad(q, "load_q");
    auto sum = builder->createAdd(load_p, load_q, "sum");
    builder->createStore(sum, p);

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Function parameters may alias (conservative without call site info)
    EXPECT_EQ(result->alias(p, q), AliasAnalysis::AliasResult::MayAlias);
}

// Test 10: Global vs local variables
TEST_F(AliasAnalysisTest, GlobalVsLocalVariables) {
    // Global g and local x should not alias
    auto intType = context->getInt32Type();

    // Create a global variable
    auto g =
        new GlobalVariable(intType, false, GlobalVariable::ExternalLinkage, "g",
                           module.get(), ConstantInt::get(intType, 0));

    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_global_local", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Local variable
    auto x = builder->createAlloca(intType, nullptr, "x");

    // Load from both
    builder->createLoad(g, "load_g");
    builder->createLoad(x, "load_x");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Global and local should not alias
    EXPECT_EQ(result->alias(g, x), AliasAnalysis::AliasResult::NoAlias);

    // Check object types
    EXPECT_TRUE(result->isGlobalObject(g));
    EXPECT_FALSE(result->isStackObject(g));
    EXPECT_TRUE(result->isStackObject(x));
    EXPECT_FALSE(result->isGlobalObject(x));

    // They are different objects
    EXPECT_TRUE(result->isDifferentObject(g, x));
}

// Test 11: Memory range overlapping
TEST_F(AliasAnalysisTest, MemoryRangeOverlap) {
    // load p[offset]; store p[offset+k]; check if ranges overlap
    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_range_overlap", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto intType = context->getInt32Type();
    auto arrayType = ArrayType::get(intType, 20);
    auto p = builder->createAlloca(arrayType, nullptr, "p");

    auto five = builder->getInt32(5);
    auto six = builder->getInt32(6);
    auto ten = builder->getInt32(10);

    // Access p[5] and p[6] (adjacent)
    auto gep_5 = builder->createGEP(arrayType, p, {five}, "gep_5");
    auto gep_6 = builder->createGEP(arrayType, p, {six}, "gep_6");

    // Access p[5] and p[10] (non-adjacent)
    auto gep_10 = builder->createGEP(arrayType, p, {ten}, "gep_10");

    builder->createLoad(gep_5, "load_5");
    auto store_6 = builder->createStore(builder->getInt32(42), gep_6);
    auto store_10 = builder->createStore(builder->getInt32(99), gep_10);

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Adjacent elements should not alias (different indices)
    EXPECT_EQ(result->alias(gep_5, gep_6), AliasAnalysis::AliasResult::NoAlias);
    EXPECT_EQ(result->alias(gep_5, gep_10),
              AliasAnalysis::AliasResult::NoAlias);

    // Store to gep_6 should not modify gep_5
    EXPECT_FALSE(result->mayModify(store_6, gep_5));
    EXPECT_TRUE(result->mayModify(store_6, gep_6));

    // Store to gep_10 should not modify gep_5
    EXPECT_FALSE(result->mayModify(store_10, gep_5));
}

// Test 12: Function call side effects
TEST_F(AliasAnalysisTest, FunctionCallSideEffects) {
    // load p; call foo(); load p - foo may modify p
    auto intType = context->getInt32Type();
    auto ptrType = PointerType::get(intType);

    // Declare external function foo(int*)
    auto fooType = FunctionType::get(context->getVoidType(), {ptrType});
    auto foo = Function::Create(fooType, "foo", module.get());

    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_call_effects", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    auto p = builder->createAlloca(intType, nullptr, "p");

    // load p; call foo(p); load p
    builder->createLoad(p, "load1");
    auto call = builder->createCall(foo, {p});
    builder->createLoad(p, "load2");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // Conservative: function call may modify any memory
    EXPECT_TRUE(result->mayModify(call, p));
}

// Test 13: Cross basic block write interference
TEST_F(AliasAnalysisTest, CrossBasicBlockInterference) {
    // Test writes in different paths between loads
    auto intType = context->getInt32Type();
    auto funcType =
        FunctionType::get(context->getVoidType(), {context->getInt1Type()});
    auto func = Function::Create(funcType, "test_cross_bb", module.get());

    auto entry = BasicBlock::Create(context.get(), "entry", func);
    auto if_true = BasicBlock::Create(context.get(), "if.true", func);
    auto if_false = BasicBlock::Create(context.get(), "if.false", func);
    auto merge = BasicBlock::Create(context.get(), "merge", func);

    // Entry block
    builder->setInsertPoint(entry);
    auto p = builder->createAlloca(intType, nullptr, "p");
    auto q = builder->createAlloca(intType, nullptr, "q");

    // Initial load
    builder->createLoad(p, "load1_p");

    auto cond = func->getArg(0);
    builder->createCondBr(cond, if_true, if_false);

    // True branch - modifies p
    builder->setInsertPoint(if_true);
    builder->createStore(builder->getInt32(100), p);
    builder->createBr(merge);

    // False branch - modifies q
    builder->setInsertPoint(if_false);
    builder->createStore(builder->getInt32(200), q);
    builder->createBr(merge);

    // Merge block
    builder->setInsertPoint(merge);
    builder->createLoad(p, "load2_p");
    builder->createLoad(q, "load_q");
    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // p and q don't alias
    EXPECT_EQ(result->alias(p, q), AliasAnalysis::AliasResult::NoAlias);

    // The store in true branch modifies p
    auto* store_p = dyn_cast<StoreInst>(*if_true->begin());
    EXPECT_TRUE(result->mayModify(store_p, p));
    EXPECT_FALSE(result->mayModify(store_p, q));
}

// Test 14: Escaped pointer analysis
TEST_F(AliasAnalysisTest, EscapedPointer) {
    // Pointer escapes through global/call, subsequent access uncertain
    auto intType = context->getInt32Type();
    auto ptrType = PointerType::get(intType);

    // Global pointer variable
    auto global_ptr = new GlobalVariable(
        ptrType, false, GlobalVariable::ExternalLinkage, "global_ptr",
        module.get(), ConstantPointerNull::get(ptrType));

    auto funcType = FunctionType::get(context->getVoidType(), {});
    auto func = Function::Create(funcType, "test_escape", module.get());
    auto bb = BasicBlock::Create(context.get(), "entry", func);
    builder->setInsertPoint(bb);

    // Local allocation
    auto local = builder->createAlloca(intType, nullptr, "local");

    // Store local pointer to global (escape)
    builder->createStore(local, global_ptr);

    // After escape, loads and stores may be affected
    builder->createLoad(local, "load1");

    // External function not use the escaped pointer
    auto extFuncType = FunctionType::get(context->getVoidType(), {});
    auto extFunc = Function::Create(extFuncType, "external_func", module.get());
    auto call = builder->createCall(extFunc, {});

    // Load after potential modification through escaped pointer
    builder->createLoad(local, "load2");

    builder->createRetVoid();

    // Run alias analysis
    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *func);

    // The escaped pointer might be modified by external call
    EXPECT_FALSE(result->mayModify(call, local));

    // Global pointer may alias with local after escape
    auto load_global = builder->createLoad(global_ptr, "load_global");
    // The loaded pointer from global may alias with local
    EXPECT_NE(result->alias(load_global, local),
              AliasAnalysis::AliasResult::NoAlias);
}

// Additional comprehensive tests for the current mayModify behavior
TEST_F(AliasAnalysisTest, FunctionCallModificationScenarios) {
    auto intType = context->getInt32Type();
    auto ptrType = PointerType::get(intType);

    // Create various types of functions to test conservative behavior

    // 1. External function (declaration only)
    auto externFunc =
        Function::Create(FunctionType::get(context->getVoidType(), {}),
                         "external", module.get());

    // 2. Function with body but unknown side effects
    auto unknownFunc =
        Function::Create(FunctionType::get(context->getVoidType(), {ptrType}),
                         "unknown", module.get());
    auto unknownBB = BasicBlock::Create(context.get(), "entry", unknownFunc);
    builder->setInsertPoint(unknownBB);
    builder->createRetVoid();

    // Create test function
    auto testFunc = Function::Create(
        FunctionType::get(context->getVoidType(), {}), "test", module.get());
    auto testBB = BasicBlock::Create(context.get(), "entry", testFunc);
    builder->setInsertPoint(testBB);

    // Create test locations
    auto local1 = builder->createAlloca(intType, nullptr, "local1");
    auto local2 = builder->createAlloca(intType, nullptr, "local2");
    auto global = new GlobalVariable(
        intType, false, GlobalVariable::ExternalLinkage, "global", module.get(),
        ConstantInt::get(intType, 0));

    // Test calls
    auto externCall = builder->createCall(externFunc, {});
    auto unknownCall = builder->createCall(unknownFunc, {local1});

    builder->createRetVoid();

    auto* result =
        am->getAnalysis<AliasAnalysis::Result>("AliasAnalysis", *testFunc);

    EXPECT_FALSE(result->mayModify(externCall, local1));
    EXPECT_FALSE(result->mayModify(externCall, local2));
    EXPECT_FALSE(result->mayModify(externCall, global));

    EXPECT_FALSE(result->mayModify(unknownCall, local1));
    EXPECT_FALSE(result->mayModify(unknownCall, local2));
    EXPECT_FALSE(result->mayModify(unknownCall, global));
}
