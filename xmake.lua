add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

target("midend")
    set_kind("static")
    set_languages("c++17")
    
    add_files("src/IR/*.cpp")
    add_files("src/IR/**/*.cpp")
    add_files("src/Pass/*.cpp")
    add_files("src/Pass/**/*.cpp")
    
    add_includedirs("include", {public = true})
    
    add_headerfiles("include/(IR/*.h)")
    add_headerfiles("include/(IR/Instructions/*.h)")
    add_headerfiles("include/(Pass/**/*.h)")
    add_headerfiles("include/(Support/*.h)")
    
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
    end

if os.isdir(path.join(os.scriptdir(), "tests")) then
    includes("tests/xmake.lua")
end

task("test")
    set_menu {
        usage = "xmake test",
        description = "Run midend tests",
        options = {}
    }
    on_run(function ()
        import("core.project.project")
        import("core.base.task")
        import("lib.detect.find_tool")
        local python3 = find_tool("python3")
        if not python3 then
            raise("Python3 is required to run tests")
        end
        task.run("build", {}, "midend_tests")
        local gtest_parallel = path.join(os.scriptdir(), "scripts", "gtest_parallel.py")
        if not os.isfile(gtest_parallel) then
            cprint("${yellow}Warning: gtest_parallel.py not found, running tests sequentially")
            os.exec("xmake run midend_tests")
        else
            local target = project.target("midend_tests")
            local target_executable = target:targetfile()
            os.exec(python3.program .. " " .. gtest_parallel .. " -r 10 " .. target_executable)
        end
    end)

task("format")
    set_menu {
        usage = "xmake format",
        description = "Check code formatting with clang-format",
        options = {}
    }
    on_run(function ()
        import("lib.detect.find_tool")
        local clang_format = find_tool("clang-format-15") or find_tool("clang-format")
        if not clang_format then
            raise("clang-format-15 or clang-format is required for formatting")
        end
        
        local cmd = "find . -name '*.cpp' -o -name '*.h' | grep -v build | grep -v googletest | grep -v _deps | xargs " .. clang_format.program .. " -i"
        local ok, outdata, errdata = os.iorunv("sh", {"-c", cmd})
        
        if not ok then
            cprint("${red}Code formatting check failed:")
            if errdata and #errdata > 0 then
                print(errdata)
            end
            os.exit(1)
        else
            cprint("${green}All files are properly formatted!")
        end
    end)