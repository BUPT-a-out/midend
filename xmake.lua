add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})
add_rules("mode.debug", "mode.release", "mode.coverage")

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
    elseif is_mode("coverage") then
        add_cxxflags("-g", "-O0", "-fprofile-instr-generate", "-fcoverage-mapping")
        add_ldflags("-fprofile-instr-generate")
        set_symbols("debug")
        set_optimize("none")
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
        options = {
            {'r', "repeat", "kv", "10", "Run the tests repeatedly; use a negative count to repeat forever."},
        }
    }
    on_run(function ()
        import("core.project.project")
        import("core.base.task")
        import("lib.detect.find_tool")
        import("net.http")
        import("core.base.option")

        local python3 = find_tool("python3")
        if not python3 then
            raise("Python3 is required to run tests")
        end
        task.run("build", {target = "midend_tests"})
        local target = project.target("midend_tests")
        local target_executable = path.absolute(target:targetfile())
        local repeat_count = option.get("repeat") or 10

        local gtest_parallel = path.join(target:targetdir(), "scripts", "gtest_parallel.py")
        if not os.isfile(gtest_parallel) then
            cprint("${blue}gtest_parallel.py not found, downloading...")
            http.download("https://raw.githubusercontent.com/google/gtest-parallel/refs/heads/master/gtest_parallel.py", gtest_parallel)
        end
        if tonumber(repeat_count) < 0 then
            while true do
                os.exec(python3.program .. " " .. gtest_parallel .. " -r 5 " .. target_executable)
            end
        else
            os.exec(python3.program .. " " .. gtest_parallel .. " -r " .. repeat_count .. " " .. target_executable)
        end
    end)

task("coverage")
    set_menu {
        usage = "xmake coverage",
        description = "Generate HTML coverage report",
        options = {}
    }
    on_run(function ()
        import("core.project.project")
        import("core.base.task")
        
        cprint("${blue}Building in coverage mode...")
        os.exec("xmake config -m coverage")
        
        -- Clean up old coverage files to avoid inconsistencies
        local build_dir = path.join(os.scriptdir(), "build")
        if os.isdir(build_dir) then
            cprint("${blue}Cleaning old coverage files...")
            os.exec("find " .. build_dir .. " -name '*.gcda' -delete")
        end
        
        task.run("build", {target = "midend_tests"})
        
        local target = project.target("midend_tests")
        local target_executable = path.absolute(target:targetfile())
        
        cprint("${blue}Running tests to generate coverage data...")
        os.exec(target_executable)
        
        cprint("${blue}Generating HTML coverage report...")
        local coverage_dir = path.join(os.scriptdir(), "coverage")
        local build_dir = path.join(os.scriptdir(), "build")
        os.mkdir(coverage_dir)
        
        -- Use gcov to generate coverage report from .gcda files
        local gcov_info = path.join(coverage_dir, "coverage.info")
        local filtered_info = path.join(coverage_dir, "filtered_coverage.info")
        
        os.exec("lcov --capture --directory " .. build_dir .. " --base-directory " .. os.scriptdir() .. " --output-file " .. gcov_info .. " --ignore-errors path,source,unsupported,inconsistent,format,count,version")
        
        -- Filter out standard library and gtest files
        os.exec("lcov --remove " .. gcov_info .. " '/usr/*' 'tests/*' '/Applications/*' '*/gtest/*' '*/googletest/*' '*/.xmake/*' --output-file " .. filtered_info .. " --ignore-errors source,unsupported,inconsistent,format,count,unused,version")
        
        local out, err = os.iorun("lcov --summary " .. filtered_info .. " --ignore-errors path,source,inconsistent,unsupported,category,count,version > " .. coverage_dir .. "/summary.txt")

        local summary_fd = io.open(path.join(coverage_dir, "summary.txt"), "w")
        if summary_fd then
            summary_fd:write(out)
            summary_fd:close()
        end

        os.exec("genhtml " .. filtered_info .. " --output-directory " .. coverage_dir .. " --ignore-errors source,inconsistent,unsupported,category,count,version")
        
        cprint("${green}Coverage report generated in: " .. coverage_dir)
        cprint("${green}Open coverage/index.html in your browser to view the report")
    end)

task("gtest")
    set_menu {
        usage = "xmake gtest [filter]",
        description = "Build and run midend tests with optional gtest filter",
        options = {
            {'f', "filter", "v", nil, "GTest filter pattern (e.g., 'Mem2RegTest.*')"},
            {'r', "repeat", "kv", "1", "Run the tests repeatedly; use a negative count to repeat forever."}
        }
    }
    on_run(function ()
        import("core.project.project")
        import("core.base.task")
        import("core.base.option")
        
        -- Build the test target
        cprint("${blue}Building midend_tests...")
        task.run("build", {target = "midend_tests"})
        
        -- Get the test executable path
        local target = project.target("midend_tests")
        local target_executable = path.absolute(target:targetfile())
        
        -- Construct the command
        local cmd = target_executable
        local filter = option.get("filter")
        local repeat_count = option.get("repeat")
        if filter then
            cmd = cmd .. " --gtest_filter=" .. filter .. " --gtest_repeat=" .. repeat_count
        end
        
        cprint("${blue}Running tests...")
        cprint("${dim}Command: " .. cmd)
        
        -- Run the tests
        os.exec(cmd)
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