include(GoogleTest)

# Helper function to add tests
function(add_framework_test test_name test_file)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} 
        PRIVATE 
        midend
        gtest_main
        gmock_main
    )
    
    target_compile_definitions(${test_name} PRIVATE
        GTEST_HAS_PTHREAD=1
    )
    
    gtest_discover_tests(${test_name})
endfunction()

add_framework_test(TypeTest IR/TypeTest.cpp)
add_framework_test(ValueTest IR/ValueTest.cpp)
add_framework_test(InstructionTest IR/InstructionTest.cpp)
add_framework_test(BasicBlockTest IR/BasicBlockTest.cpp)
add_framework_test(FunctionTest IR/FunctionTest.cpp)
add_framework_test(ModuleTest IR/ModuleTest.cpp)
add_framework_test(IRBuilderTest IR/IRBuilderTest.cpp)
add_framework_test(OperatorTest IR/OperatorTest.cpp)
add_framework_test(ConstantTest IR/ConstantTest.cpp)
add_framework_test(IRPrinterTest IR/IRPrinterTest.cpp)
add_framework_test(MoveInstTest IR/MoveInstTest.cpp)

add_framework_test(AnalysisManagerTest Pass/AnalysisManagerTest.cpp)
add_framework_test(AnalysisTest Pass/AnalysisTest.cpp)
add_framework_test(PassManagerTest Pass/PassManagerTest.cpp)
add_framework_test(PassRegistryTest Pass/PassRegistryTest.cpp)
add_framework_test(PassTest Pass/PassTest.cpp)
add_framework_test(DominanceTest Pass/Analysis/DominanceTest.cpp)