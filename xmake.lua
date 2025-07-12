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
    
    before_build(function (target)
        local hooks_dir = path.join(os.scriptdir(), ".git", "hooks")
        local pre_commit_path = path.join(hooks_dir, "pre-commit")
        if os.isdir(hooks_dir) then
            local expected_hook_content = [[#!/bin/sh
# Auto-generated pre-commit hook by xmake
# This hook runs tests and formatting checks before committing

echo "Running tests..."
if ! xmake test; then
    echo "Tests failed! Commit aborted."
    exit 1
fi

echo "Checking code formatting..."
if ! xmake format --check; then
    echo "Code formatting check failed! Commit aborted."
    echo "Please run 'xmake format' to fix formatting issues."
    exit 1
fi

echo "All checks passed!"
]]
            
            local should_write = false
            if not os.isfile(pre_commit_path) then
                cprint("${yellow}No git pre-commit hook found. Setting up automatically...")
                should_write = true
            else
                local current_content = io.readfile(pre_commit_path)
                if current_content ~= expected_hook_content then
                    cprint("${yellow}Existing pre-commit hook differs from expected. Updating...")
                    should_write = true
                end
            end
            
            if should_write then
                io.writefile(pre_commit_path, expected_hook_content)
                os.exec("chmod +x " .. pre_commit_path)
                cprint("${green}Git pre-commit hook has been set up automatically!")
            end
        end
    end)

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
        import("net.http")
        local python3 = find_tool("python3")
        if not python3 then
            raise("Python3 is required to run tests")
        end
        task.run("build", {target = "midend_tests"})
        local target = project.target("midend_tests")
        local target_executable = path.absolute(target:targetfile())
        local gtest_parallel = path.join(target:targetdir(), "scripts", "gtest_parallel.py")
        if not os.isfile(gtest_parallel) then
            cprint("${blue}gtest_parallel.py not found, downloading...")
            http.download("https://raw.githubusercontent.com/google/gtest-parallel/refs/heads/master/gtest_parallel.py", gtest_parallel)
        end
        os.exec(python3.program .. " " .. gtest_parallel .. " -r 10 " .. target_executable)
    end)

task("format")
    set_menu {
        usage = "xmake format",
        description = "Check code formatting with clang-format",
        options = {
            {'c', "check", "k", false, "Run clang-format in dry-run mode to check formatting without making changes."},
        }
    }
    on_run(function ()
        import("lib.detect.find_tool")
        import("core.base.option")
        local clang_format = find_tool("clang-format-15") or find_tool("clang-format")
        if not clang_format then
            raise("clang-format-15 or clang-format is required for formatting")
        end
        
        local cmd = "find . -name '*.cpp' -o -name '*.h' | grep -v build | grep -v googletest | grep -v _deps | xargs " .. clang_format.program
        if option.get("check") then
            cmd = cmd .. " --dry-run --Werror"
        else
            cmd = cmd .. " -i"
        end
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