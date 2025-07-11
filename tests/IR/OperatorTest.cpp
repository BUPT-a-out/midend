#include <gtest/gtest.h>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Instructions/BinaryOps.h"
#include "IR/Module.h"

using namespace midend;

namespace {

class OperatorTest : public ::testing::Test {
   protected:
    void SetUp() override {
        context = std::make_unique<Context>();
        module = std::make_unique<Module>("test_module", context.get());

        auto* voidTy = context->getVoidType();
        auto* fnTy = FunctionType::get(voidTy, {});
        function = Function::Create(fnTy, "test_function", module.get());
        entryBB = BasicBlock::Create(context.get(), "entry", function);

        builder = std::make_unique<IRBuilder>(entryBB);
    }

    std::unique_ptr<Context> context;
    std::unique_ptr<Module> module;
    Function* function;
    BasicBlock* entryBB;
    std::unique_ptr<IRBuilder> builder;
};

TEST_F(OperatorTest, UnaryOperators) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 42);

    // Test unary plus (+)
    auto* uaddInst = builder->createUAdd(val, "uadd_result");
    EXPECT_EQ(uaddInst->getOpcode(), Opcode::UAdd);
    EXPECT_EQ(uaddInst->getOperand(), val);
    EXPECT_EQ(uaddInst->getType(), int32Ty);
    EXPECT_EQ(uaddInst->getName(), "uadd_result");

    // Test unary minus (-)
    auto* usubInst = builder->createUSub(val, "usub_result");
    EXPECT_EQ(usubInst->getOpcode(), Opcode::USub);
    EXPECT_EQ(usubInst->getOperand(), val);
    EXPECT_EQ(usubInst->getType(), int32Ty);
    EXPECT_EQ(usubInst->getName(), "usub_result");

    // Test logical NOT (!)
    auto* boolVal = ConstantInt::getTrue(context.get());
    auto* notInst = builder->createNot(boolVal, "not_result");
    EXPECT_EQ(notInst->getOpcode(), Opcode::Not);
    EXPECT_EQ(notInst->getOperand(), boolVal);
    EXPECT_TRUE(notInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(notInst->getType())->getBitWidth(), 1u);
    EXPECT_EQ(notInst->getName(), "not_result");

    // Test UnaryOperator static creation methods
    auto* directUAdd = UnaryOperator::CreateUAdd(val, "direct_uadd");
    auto* directUSub = UnaryOperator::CreateUSub(val, "direct_usub");
    auto* directNot = UnaryOperator::CreateNot(boolVal, "direct_not");

    EXPECT_TRUE(isa<UnaryOperator>(*directUAdd));
    EXPECT_TRUE(isa<UnaryOperator>(*directUSub));
    EXPECT_TRUE(isa<UnaryOperator>(*directNot));
}

TEST_F(OperatorTest, ArithmeticOperators) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    // Test addition (+)
    auto* addInst = builder->createAdd(val1, val2, "add_result");
    EXPECT_EQ(addInst->getOpcode(), Opcode::Add);
    EXPECT_EQ(addInst->getOperand(0), val1);
    EXPECT_EQ(addInst->getOperand(1), val2);
    EXPECT_EQ(addInst->getType(), int32Ty);

    // Test subtraction (-)
    auto* subInst = builder->createSub(val1, val2, "sub_result");
    EXPECT_EQ(subInst->getOpcode(), Opcode::Sub);

    // Test multiplication (*)
    auto* mulInst = builder->createMul(val1, val2, "mul_result");
    EXPECT_EQ(mulInst->getOpcode(), Opcode::Mul);

    // Test division (/)
    auto* divInst = builder->createDiv(val1, val2, "div_result");
    EXPECT_EQ(divInst->getOpcode(), Opcode::Div);

    // Test modulo (%)
    auto* remInst = builder->createRem(val1, val2, "rem_result");
    EXPECT_EQ(remInst->getOpcode(), Opcode::Rem);

    // Test static creation methods
    auto* directAdd = BinaryOperator::CreateAdd(val1, val2, "direct_add");
    auto* directSub = BinaryOperator::CreateSub(val1, val2, "direct_sub");
    auto* directMul = BinaryOperator::CreateMul(val1, val2, "direct_mul");
    auto* directDiv = BinaryOperator::CreateDiv(val1, val2, "direct_div");
    auto* directRem = BinaryOperator::CreateRem(val1, val2, "direct_rem");

    EXPECT_TRUE(isa<BinaryOperator>(*directAdd));
    EXPECT_TRUE(isa<BinaryOperator>(*directSub));
    EXPECT_TRUE(isa<BinaryOperator>(*directMul));
    EXPECT_TRUE(isa<BinaryOperator>(*directDiv));
    EXPECT_TRUE(isa<BinaryOperator>(*directRem));
}

TEST_F(OperatorTest, RelationalOperators) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 10);
    auto* val2 = ConstantInt::get(int32Ty, 20);

    // Test equality (==)
    auto* eqInst = builder->createICmpEQ(val1, val2, "eq_result");
    EXPECT_EQ(eqInst->getOpcode(), Opcode::ICmpEQ);
    EXPECT_EQ(eqInst->getPredicate(), CmpInst::ICMP_EQ);
    EXPECT_TRUE(eqInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(eqInst->getType())->getBitWidth(), 1u);

    // Test inequality (!=)
    auto* neInst = builder->createICmpNE(val1, val2, "ne_result");
    EXPECT_EQ(neInst->getOpcode(), Opcode::ICmpNE);
    EXPECT_EQ(neInst->getPredicate(), CmpInst::ICMP_NE);

    // Test less than (<)
    auto* ltInst = builder->createICmpSLT(val1, val2, "lt_result");
    EXPECT_EQ(ltInst->getOpcode(), Opcode::ICmpSLT);
    EXPECT_EQ(ltInst->getPredicate(), CmpInst::ICMP_SLT);

    // Test less than or equal (<=)
    auto* leInst = builder->createICmpSLE(val1, val2, "le_result");
    EXPECT_EQ(leInst->getOpcode(), Opcode::ICmpSLE);
    EXPECT_EQ(leInst->getPredicate(), CmpInst::ICMP_SLE);

    // Test greater than (>)
    auto* gtInst = builder->createICmpSGT(val1, val2, "gt_result");
    EXPECT_EQ(gtInst->getOpcode(), Opcode::ICmpSGT);
    EXPECT_EQ(gtInst->getPredicate(), CmpInst::ICMP_SGT);

    // Test greater than or equal (>=)
    auto* geInst = builder->createICmpSGE(val1, val2, "ge_result");
    EXPECT_EQ(geInst->getOpcode(), Opcode::ICmpSGE);
    EXPECT_EQ(geInst->getPredicate(), CmpInst::ICMP_SGE);

    // Test static creation methods
    auto* directEQ = CmpInst::CreateICmpEQ(val1, val2, "direct_eq");
    auto* directNE = CmpInst::CreateICmpNE(val1, val2, "direct_ne");
    auto* directLT = CmpInst::CreateICmpSLT(val1, val2, "direct_lt");
    auto* directLE = CmpInst::CreateICmpSLE(val1, val2, "direct_le");
    auto* directGT = CmpInst::CreateICmpSGT(val1, val2, "direct_gt");
    auto* directGE = CmpInst::CreateICmpSGE(val1, val2, "direct_ge");

    EXPECT_TRUE(isa<CmpInst>(*directEQ));
    EXPECT_TRUE(isa<CmpInst>(*directNE));
    EXPECT_TRUE(isa<CmpInst>(*directLT));
    EXPECT_TRUE(isa<CmpInst>(*directLE));
    EXPECT_TRUE(isa<CmpInst>(*directGT));
    EXPECT_TRUE(isa<CmpInst>(*directGE));
}

TEST_F(OperatorTest, LogicalOperators) {
    auto* boolTy = context->getInt1Type();
    auto* val1 = ConstantInt::getTrue(context.get());
    auto* val2 = ConstantInt::getFalse(context.get());

    // Test logical AND (&&)
    auto* landInst = builder->createLAnd(val1, val2, "land_result");
    EXPECT_EQ(landInst->getOpcode(), Opcode::LAnd);
    EXPECT_EQ(landInst->getOperand(0), val1);
    EXPECT_EQ(landInst->getOperand(1), val2);
    EXPECT_TRUE(landInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(landInst->getType())->getBitWidth(),
              1u);
    EXPECT_EQ(landInst->getType(), boolTy);

    // Test logical OR (||)
    auto* lorInst = builder->createLOr(val1, val2, "lor_result");
    EXPECT_EQ(lorInst->getOpcode(), Opcode::LOr);
    EXPECT_EQ(lorInst->getOperand(0), val1);
    EXPECT_EQ(lorInst->getOperand(1), val2);
    EXPECT_TRUE(lorInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(lorInst->getType())->getBitWidth(), 1u);
    EXPECT_EQ(lorInst->getType(), boolTy);

    // Test static creation methods
    auto* directLAnd = BinaryOperator::CreateLAnd(val1, val2, "direct_land");
    auto* directLOr = BinaryOperator::CreateLOr(val1, val2, "direct_lor");

    EXPECT_TRUE(isa<BinaryOperator>(*directLAnd));
    EXPECT_TRUE(isa<BinaryOperator>(*directLOr));
    EXPECT_EQ(directLAnd->getType(), boolTy);
    EXPECT_EQ(directLOr->getType(), boolTy);
}

TEST_F(OperatorTest, FloatingPointComparisons) {
    auto* floatTy = context->getFloatType();
    auto* val1 = ConstantFP::get(floatTy, 3.14f);
    auto* val2 = ConstantFP::get(floatTy, 2.71f);

    // Test floating point equality
    auto* feqInst = CmpInst::CreateFCmpOEQ(val1, val2, "feq_result");
    EXPECT_EQ(feqInst->getOpcode(), Opcode::FCmpOEQ);
    EXPECT_EQ(feqInst->getPredicate(), CmpInst::FCMP_OEQ);
    EXPECT_TRUE(feqInst->isFloatComparison());

    // Test floating point inequality
    auto* fneInst = CmpInst::CreateFCmpONE(val1, val2, "fne_result");
    EXPECT_EQ(fneInst->getOpcode(), Opcode::FCmpONE);
    EXPECT_EQ(fneInst->getPredicate(), CmpInst::FCMP_ONE);

    // Test floating point less than
    auto* fltInst = CmpInst::CreateFCmpOLT(val1, val2, "flt_result");
    EXPECT_EQ(fltInst->getOpcode(), Opcode::FCmpOLT);
    EXPECT_EQ(fltInst->getPredicate(), CmpInst::FCMP_OLT);

    // Test floating point less than or equal
    auto* fleInst = CmpInst::CreateFCmpOLE(val1, val2, "fle_result");
    EXPECT_EQ(fleInst->getOpcode(), Opcode::FCmpOLE);
    EXPECT_EQ(fleInst->getPredicate(), CmpInst::FCMP_OLE);

    // Test floating point greater than
    auto* fgtInst = CmpInst::CreateFCmpOGT(val1, val2, "fgt_result");
    EXPECT_EQ(fgtInst->getOpcode(), Opcode::FCmpOGT);
    EXPECT_EQ(fgtInst->getPredicate(), CmpInst::FCMP_OGT);

    // Test floating point greater than or equal
    auto* fgeInst = CmpInst::CreateFCmpOGE(val1, val2, "fge_result");
    EXPECT_EQ(fgeInst->getOpcode(), Opcode::FCmpOGE);
    EXPECT_EQ(fgeInst->getPredicate(), CmpInst::FCMP_OGE);
}

TEST_F(OperatorTest, OperatorPrecedence) {
    auto* int32Ty = context->getInt32Type();
    auto* val1 = ConstantInt::get(int32Ty, 2);
    auto* val2 = ConstantInt::get(int32Ty, 3);
    auto* val3 = ConstantInt::get(int32Ty, 4);

    // Test that we can create complex expressions: (2 + 3) * 4
    auto* addInst = builder->createAdd(val1, val2, "add_temp");
    auto* mulInst = builder->createMul(addInst, val3, "mul_result");

    EXPECT_EQ(mulInst->getOperand(0), addInst);
    EXPECT_EQ(mulInst->getOperand(1), val3);
    EXPECT_EQ(addInst->getOperand(0), val1);
    EXPECT_EQ(addInst->getOperand(1), val2);

    // Test logical expression: (a > b) && (c < d)
    auto* cmpInst1 = builder->createICmpSGT(val1, val2, "cmp1");
    auto* cmpInst2 = builder->createICmpSLT(val2, val3, "cmp2");
    auto* landInst = builder->createLAnd(cmpInst1, cmpInst2, "land_result");

    EXPECT_EQ(landInst->getOperand(0), cmpInst1);
    EXPECT_EQ(landInst->getOperand(1), cmpInst2);
    EXPECT_TRUE(landInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(landInst->getType())->getBitWidth(),
              1u);
}

TEST_F(OperatorTest, TypeSafety) {
    auto* int32Ty = context->getInt32Type();
    auto* boolTy = context->getInt1Type();
    auto* val = ConstantInt::get(int32Ty, 42);
    auto* boolVal = ConstantInt::getTrue(context.get());

    // Test unary operators preserve/change types correctly
    auto* uaddInst = UnaryOperator::CreateUAdd(val);
    EXPECT_EQ(uaddInst->getType(), int32Ty);

    auto* usubInst = UnaryOperator::CreateUSub(val);
    EXPECT_EQ(usubInst->getType(), int32Ty);

    auto* notInst = UnaryOperator::CreateNot(boolVal);
    EXPECT_TRUE(notInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(notInst->getType())->getBitWidth(), 1u);
    EXPECT_EQ(notInst->getType(), boolTy);

    // Test binary operators preserve types correctly
    auto* addInst = BinaryOperator::CreateAdd(val, val);
    EXPECT_EQ(addInst->getType(), int32Ty);

    // Test comparison operators return boolean
    auto* cmpInst = CmpInst::CreateICmpEQ(val, val);
    EXPECT_TRUE(cmpInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(cmpInst->getType())->getBitWidth(), 1u);
    EXPECT_EQ(cmpInst->getType(), boolTy);

    // Test logical operators return boolean
    auto* landInst = BinaryOperator::CreateLAnd(boolVal, boolVal);
    EXPECT_TRUE(landInst->getType()->isIntegerType());
    EXPECT_EQ(static_cast<IntegerType*>(landInst->getType())->getBitWidth(),
              1u);
    EXPECT_EQ(landInst->getType(), boolTy);
}

TEST_F(OperatorTest, InstructionClassification) {
    auto* int32Ty = context->getInt32Type();
    auto* val = ConstantInt::get(int32Ty, 42);
    auto* boolVal = ConstantInt::getTrue(context.get());

    // Test that instructions are classified correctly
    auto* unaryInst = UnaryOperator::CreateUAdd(val);
    EXPECT_TRUE(unaryInst->isUnaryOp());
    EXPECT_FALSE(unaryInst->isBinaryOp());
    EXPECT_FALSE(unaryInst->isComparison());

    auto* binaryInst = BinaryOperator::CreateAdd(val, val);
    EXPECT_FALSE(binaryInst->isUnaryOp());
    EXPECT_TRUE(binaryInst->isBinaryOp());
    EXPECT_FALSE(binaryInst->isComparison());

    auto* cmpInst = CmpInst::CreateICmpEQ(val, val);
    EXPECT_FALSE(cmpInst->isUnaryOp());
    EXPECT_FALSE(cmpInst->isBinaryOp());
    EXPECT_TRUE(cmpInst->isComparison());

    auto* logicalInst = BinaryOperator::CreateLAnd(boolVal, boolVal);
    EXPECT_FALSE(logicalInst->isUnaryOp());
    EXPECT_TRUE(
        logicalInst->isBinaryOp());  // Logical ops are classified as binary ops
    EXPECT_FALSE(logicalInst->isComparison());
}

}  // namespace