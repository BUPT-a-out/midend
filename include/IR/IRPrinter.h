#pragma once

#include <sstream>
#include <string>
#include <unordered_map>

namespace midend {

class Value;
class Module;
class Function;
class BasicBlock;
class Instruction;
class Type;

class IRPrinter {
   private:
    std::unordered_map<const Value*, unsigned> valueNumbers_;
    unsigned nextNumber_ = 0;
    std::stringstream output_;

    unsigned getValueNumber(const Value* v);
    void printInstruction(const Instruction* inst);
    void printBasicBlock(const BasicBlock* bb);
    void printFunction(const Function* func);

   public:
    IRPrinter() = default;

    std::string getValueName(const Value* v);
    static std::string printType(Type* ty);

    // Print individual components
    std::string print(const Module* module);
    std::string print(const Function* func);
    std::string print(const BasicBlock* bb);
    std::string print(const Instruction* inst);
    std::string print(const Value* val);

    // Static convenience methods
    static std::string toString(const Module* module);
    static std::string toString(const Function* func);
    static std::string toString(const BasicBlock* bb);
    static std::string toString(const Instruction* inst);
    static std::string toString(const Value* val);
};

}  // namespace midend