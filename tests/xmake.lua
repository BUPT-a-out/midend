add_requires("gtest 1.14.0", {configs = {shared = false, main = true}, system = false})

target("midend_tests")
    set_kind("binary")
    set_languages("c++17")
    set_default(false)
    
    add_packages("gtest")
    
    add_files("IR/*.cpp")
    add_files("Pass/**/*.cpp")
    add_files("Pass/*.cpp")
    
    add_deps("midend")
    
    add_cxxflags("-DGTEST_HAS_PTHREAD=1")
    add_defines("GTEST_LINKED_AS_SHARED_LIBRARY=0")
    
    set_warnings("all")
    add_cxxflags("-Wall", "-Wextra")
    
    if is_mode("debug") then
        add_cxxflags("-g", "-O0")
        set_symbols("debug")
        set_optimize("none")
    elseif is_mode("release") then
        add_cxxflags("-O3", "-DNDEBUG")
        set_symbols("hidden")
        set_optimize("fastest")
    elseif is_mode("coverage") then
        add_cxxflags("-g", "-O0", "-fprofile-instr-generate", "-fcoverage-mapping")
        add_ldflags("-fprofile-instr-generate", "-fcoverage-mapping")
        set_symbols("debug")
        set_optimize("none")
    end
    
    add_ldflags("-pthread")
    
    after_build(function (target)
        print("Midend tests built successfully. Run with: xmake run midend_tests")
    end)