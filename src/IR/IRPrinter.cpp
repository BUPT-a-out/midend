#include "IR/IRPrinter.h"

#include <iomanip>
#include <iostream>

#include "IR/BasicBlock.h"
#include "IR/Constant.h"
#include "IR/Function.h"
#include "IR/Instruction.h"
#include "IR/Instructions.h"
#include "IR/Module.h"
#include "IR/Type.h"

namespace midend {

unsigned IRPrinter::getValueNumber(const Value* v) {
    auto it = valueNumbers_.find(v);
    if (it != valueNumbers_.end()) {
        return it->second;
    }

    // Don't number constants or global values
    if (isa<Constant>(*v) || isa<GlobalVariable>(*v) || isa<Function>(*v)) {
        return 0;
    }

    unsigned num = nextNumber_++;
    valueNumbers_[v] = num;
    return num;
}

std::string IRPrinter::getValueName(const Value* v) {
    if (!v) return "<null>";

    // Constants have special representations
    if (auto* constant = dyn_cast<Constant>(v)) {
        if (isa<IntegerType>(*constant->getType())) {
            auto* ci = static_cast<const ConstantInt*>(constant);
            return std::to_string(ci->getValue());
        }
        if (isa<FloatType>(*constant->getType())) {
            auto* cf = static_cast<const ConstantFP*>(constant);
            std::stringstream ss;
            ss << std::scientific << std::setprecision(6) << cf->getValue();
            return ss.str();
        }
    }
    if (isa<ConstantPointerNull>(*v)) {
        return "null";
    }
    if (isa<UndefValue>(*v)) {
        return "undef";
    }

    // Global values use @ prefix
    if (isa<GlobalVariable>(*v) || isa<Function>(*v)) {
        return "@" + v->getName();
    }

    // Basic blocks use label syntax
    if (isa<BasicBlock>(*v)) {
        return v->getName() + ":";
    }

    // Local values use % prefix
    if (auto* arg = dyn_cast<Argument>(v)) {
        if (arg->hasName()) {
            return "%" + arg->getName();
        } else {
            return "%" + std::to_string(arg->getArgNo());
        }
    }

    if (v->hasName()) {
        return "%" + v->getName();
    } else {
        return "%" + std::to_string(getValueNumber(v));
    }
}

std::string IRPrinter::printType(Type* ty) {
    if (!ty) return "<null type>";

    if (ty->isVoidType()) return "void";
    if (isa<LabelType>(*ty)) return "label";

    if (auto* intTy = dyn_cast<IntegerType>(ty)) {
        return "i" + std::to_string(intTy->getBitWidth());
    }

    if (ty->isFloatType()) return "float";

    if (auto* ptrTy = dyn_cast<PointerType>(ty)) {
        return printType(ptrTy->getElementType()) + "*";
    }

    if (auto* arrTy = dyn_cast<ArrayType>(ty)) {
        return "[" + std::to_string(arrTy->getNumElements()) + " x " +
               printType(arrTy->getElementType()) + "]";
    }

    if (auto* funcTy = dyn_cast<FunctionType>(ty)) {
        std::string result = printType(funcTy->getReturnType()) + " (";
        const auto& paramTypes = funcTy->getParamTypes();
        for (unsigned i = 0; i < paramTypes.size(); ++i) {
            if (i > 0) result += ", ";
            result += printType(paramTypes[i]);
        }
        if (funcTy->isVarArg()) {
            if (!paramTypes.empty()) result += ", ";
            result += "...";
        }
        result += ")";
        return result;
    }

    return "<unknown type>";
}

void IRPrinter::printInstruction(const Instruction* inst) {
    if (!inst) return;

    // Special handling for different instruction types
    std::string result;

    // For instructions that produce a value, print the assignment
    if (!inst->getType()->isVoidType()) {
        result = "  " + getValueName(inst) + " = ";
    } else {
        result = "  ";
    }

    // Print the instruction based on its type
    switch (inst->getOpcode()) {
        // Unary operations
        case Opcode::UAdd:
            result += "+";
            result += getValueName(inst->getOperand(0));
            break;
        case Opcode::USub:
            result += "-";
            result += getValueName(inst->getOperand(0));
            break;
        case Opcode::Not:
            result += "!";
            result += getValueName(inst->getOperand(0));
            break;

        // Binary operations
        case Opcode::Add:
            result += "add " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::Sub:
            result += "sub " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::Mul:
            result += "mul " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::Div:
            result += "sdiv " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::Rem:
            result += "srem " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;

        // Bitwise operations
        case Opcode::And:
            result += "and " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::Or:
            result += "or " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::Xor:
            result += "xor " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;

        // Logical operations (typically on i1)
        case Opcode::LAnd:
            result += "and " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::LOr:
            result += "or " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;

        // Floating point operations
        case Opcode::FAdd:
            result += "fadd " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::FSub:
            result += "fsub " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::FMul:
            result += "fmul " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::FDiv:
            result += "fdiv " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;

        // Comparison operations
        case Opcode::ICmpEQ:
            result +=
                "icmp eq " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::ICmpNE:
            result +=
                "icmp ne " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::ICmpSLT:
            result +=
                "icmp slt " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::ICmpSLE:
            result +=
                "icmp sle " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::ICmpSGT:
            result +=
                "icmp sgt " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::ICmpSGE:
            result +=
                "icmp sge " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += getValueName(inst->getOperand(1));
            break;

        // Memory operations
        case Opcode::Alloca:
            result += "alloca " +
                      printType(cast<AllocaInst>(inst)->getAllocatedType());
            if (inst->getNumOperands() > 0 && inst->getOperand(0)) {
                result +=
                    ", " + printType(inst->getOperand(0)->getType()) + " ";
                result += getValueName(inst->getOperand(0));
            }
            break;
        case Opcode::Load:
            result += "load " + printType(inst->getType()) + ", ";
            result += printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0));
            break;
        case Opcode::Store:
            result +=
                "store " + printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + ", ";
            result += printType(inst->getOperand(1)->getType()) + " ";
            result += getValueName(inst->getOperand(1));
            break;
        case Opcode::GetElementPtr: {
            auto* gepInst = cast<GetElementPtrInst>(inst);
            result += "getelementptr " +
                      printType(gepInst->getSourceElementType()) + ", ";
            result += printType(inst->getOperand(0)->getType()) + " ";
            result += getValueName(inst->getOperand(0));
            // Print indices
            for (unsigned i = 1; i < inst->getNumOperands(); ++i) {
                result +=
                    ", " + printType(inst->getOperand(i)->getType()) + " ";
                result += getValueName(inst->getOperand(i));
            }
            break;
        }

        // Control flow
        case Opcode::Ret:
            result += "ret";
            if (inst->getNumOperands() > 0 && inst->getOperand(0)) {
                result += " " + printType(inst->getOperand(0)->getType()) + " ";
                result += getValueName(inst->getOperand(0));
            } else {
                result += " void";
            }
            break;
        case Opcode::Br:
            if (cast<BranchInst>(inst)->isUnconditional()) {
                result += "br label %" + inst->getOperand(0)->getName();
            } else {
                result += "br i1 " + getValueName(inst->getOperand(0)) + ", ";
                result += "label %" + inst->getOperand(1)->getName() + ", ";
                result += "label %" + inst->getOperand(2)->getName();
            }
            break;

        // PHI node
        case Opcode::PHI: {
            result += "phi " + printType(inst->getType());
            auto* phi = cast<PHINode>(inst);
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                if (i > 0) result += ",";
                result += " [ " + getValueName(phi->getIncomingValue(i)) + ", ";
                result += "%" + phi->getIncomingBlock(i)->getName() + " ]";
            }
            break;
        }

        // Call instruction
        case Opcode::Call:
            result += "call " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0)) + "(";
            for (unsigned i = 1; i < inst->getNumOperands(); ++i) {
                if (i > 1) result += ", ";
                result += printType(inst->getOperand(i)->getType()) + " ";
                result += getValueName(inst->getOperand(i));
            }
            result += ")";
            break;

        // Move instruction (simple assignment)
        case Opcode::Cast: {
            auto* castInst = cast<CastInst>(inst);
            switch (castInst->getCastOpcode()) {
                case CastInst::SIToFP:
                    result += "sitofp " +
                              printType(inst->getOperand(0)->getType()) + " ";
                    result += getValueName(inst->getOperand(0)) + " to " +
                              printType(inst->getType());
                    break;
                case CastInst::FPToSI:
                    result += "fptosi " +
                              printType(inst->getOperand(0)->getType()) + " ";
                    result += getValueName(inst->getOperand(0)) + " to " +
                              printType(inst->getType());
                    break;
                case CastInst::Trunc:
                    result += "trunc " +
                              printType(inst->getOperand(0)->getType()) + " ";
                    result += getValueName(inst->getOperand(0)) + " to " +
                              printType(inst->getType());
                    break;
                case CastInst::SExt:
                    result += "sext " +
                              printType(inst->getOperand(0)->getType()) + " ";
                    result += getValueName(inst->getOperand(0)) + " to " +
                              printType(inst->getType());
                    break;
                case CastInst::ZExt:
                    result += "zext " +
                              printType(inst->getOperand(0)->getType()) + " ";
                    result += getValueName(inst->getOperand(0)) + " to " +
                              printType(inst->getType());
                    break;
                default:
                    result += "<unknown cast>";
                    break;
            }
            break;
        }

        case Opcode::Move:
            // Move instruction is just an identity operation, so we don't print
            // anything special In actual IR, this might be optimized away or
            // represented differently
            result += "mov " + printType(inst->getType()) + " ";
            result += getValueName(inst->getOperand(0));
            break;

        default:
            result += "<unknown instruction>";
            break;
    }

    output_ << result << "\n";
}

void IRPrinter::printBasicBlock(const BasicBlock* bb) {
    if (!bb) return;

    // Print the label
    output_ << bb->getName() << ":\n";

    // Print all instructions
    for (const auto* inst : *bb) {
        printInstruction(inst);
    }
}

void IRPrinter::printFunction(const Function* func) {
    if (!func) return;

    // Print function signature
    output_ << "define " << printType(func->getReturnType()) << " @"
            << func->getName() << "(";

    // Print parameters
    auto* funcType = func->getFunctionType();
    const auto& paramTypes = funcType->getParamTypes();
    for (unsigned i = 0; i < func->getNumArgs(); ++i) {
        if (i > 0) output_ << ", ";
        output_ << printType(paramTypes[i]) << " ";
        output_ << getValueName(const_cast<Function*>(func)->getArg(i));
    }

    output_ << ")";

    if (func->isDeclaration()) {
        output_ << "\n";
        return;
    }

    output_ << " {\n";

    // Print all basic blocks
    for (const auto* bb : *func) {
        printBasicBlock(bb);
    }

    output_ << "}\n";
}

std::string IRPrinter::print(const Module* module) {
    if (!module) return "";

    output_.str("");
    output_.clear();
    valueNumbers_.clear();
    nextNumber_ = 0;

    // Print module header
    output_ << "; ModuleID = '" << module->getName() << "'\n\n";

    // Print global variables
    for (const auto* gv : module->globals()) {
        output_ << "@" << gv->getName() << " = ";

        switch (gv->getLinkage()) {
            case GlobalVariable::ExternalLinkage:
                output_ << "external ";
                break;
            case GlobalVariable::InternalLinkage:
                output_ << "internal ";
                break;
            case GlobalVariable::PrivateLinkage:
                output_ << "private ";
                break;
        }

        output_ << "global "
                << printType(const_cast<GlobalVariable*>(gv)->getValueType());

        if (const_cast<GlobalVariable*>(gv)->hasInitializer()) {
            output_ << " "
                    << getValueName(
                           const_cast<GlobalVariable*>(gv)->getInitializer());
        }

        output_ << "\n";
    }

    if (!module->globals().empty()) {
        output_ << "\n";
    }

    // Print all functions
    for (const auto* func : *module) {
        printFunction(func);
        output_ << "\n";
    }

    return output_.str();
}

std::string IRPrinter::print(const Function* func) {
    output_.str("");
    output_.clear();
    valueNumbers_.clear();
    nextNumber_ = 0;

    printFunction(func);
    return output_.str();
}

std::string IRPrinter::print(const BasicBlock* bb) {
    output_.str("");
    output_.clear();
    valueNumbers_.clear();
    nextNumber_ = 0;

    printBasicBlock(bb);
    return output_.str();
}

std::string IRPrinter::print(const Instruction* inst) {
    output_.str("");
    output_.clear();

    printInstruction(inst);
    return output_.str();
}

std::string IRPrinter::print(const Value* val) {
    if (!val) return "<null>";

    if (auto* inst = dyn_cast<Instruction>(val)) {
        return print(inst);
    }
    if (auto* bb = dyn_cast<BasicBlock>(val)) {
        return print(bb);
    }
    if (auto* func = dyn_cast<Function>(val)) {
        return print(func);
    }

    // For other values, just return their name
    return getValueName(val);
}

// Static convenience methods
std::string IRPrinter::toString(const Module* module) {
    IRPrinter printer;
    return printer.print(module);
}

std::string IRPrinter::toString(const Function* func) {
    IRPrinter printer;
    return printer.print(func);
}

std::string IRPrinter::toString(const BasicBlock* bb) {
    IRPrinter printer;
    return printer.print(bb);
}

std::string IRPrinter::toString(const Instruction* inst) {
    IRPrinter printer;
    return printer.print(inst);
}

std::string IRPrinter::toString(const Value* val) {
    IRPrinter printer;
    return printer.print(val);
}

}  // namespace midend