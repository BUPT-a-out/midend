#include <iostream>

#include "IR/Constant.h"
#include "IR/IRPrinter.h"
#include "IR/Type.h"

using namespace midend;

int main() {
    auto context = std::make_unique<Context>();
    auto* int32Ty = context->getInt32Type();
    auto* ptrTy = int32Ty->getPointerTo();
    auto* nullPtr = ConstantPointerNull::get(ptrTy);

    std::cout << "Type: " << nullPtr->getType()->toString() << std::endl;
    std::cout << "Is ConstantPointerNull: "
              << isa<ConstantPointerNull>(*nullPtr) << std::endl;
    std::cout << "Is ConstantArray: " << isa<ConstantArray>(*nullPtr)
              << std::endl;
    std::cout << "Result: " << IRPrinter::toString(nullPtr) << std::endl;

    return 0;
}
EOF < / dev / null