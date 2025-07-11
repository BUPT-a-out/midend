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
        
        task.run("build", {}, "midend_tests")
        os.exec("xmake run midend_tests")
    end)