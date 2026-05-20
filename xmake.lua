-- xmake.lua
set_project("asyncc")
set_version("0.0.1")

-- Add include directory to all targets
add_includedirs(".")

-- Build asyncc static library
target("asyncc")
    set_kind("static")
    add_files("asyncc.c")
    add_headerfiles("asyncc.h")

-- Build example1 target
target("example1")
    set_kind("binary")
    add_deps("asyncc")
    add_syslinks("pthread")
    on_load(function (target)
        local projectdir = os.projectdir()
        local gendir = path.join(projectdir, "build", "generated")
        os.mkdir(gendir)
        
        local src_file = path.join(projectdir, "examples", "example1.asyncc.c")
        local dst_file = path.join(gendir, "example1.c")
        
        os.runv("python3", {
            path.join(projectdir, "asyncc_preprocess.py"), 
            src_file, 
            dst_file
        })
        target:add("files", dst_file)
    end)

-- Build example_async target
target("example_async")
    set_kind("binary")
    add_deps("asyncc")
    on_load(function (target)
        local projectdir = os.projectdir()
        local gendir = path.join(projectdir, "build", "generated")
        os.mkdir(gendir)
        
        local src_file = path.join(projectdir, "examples", "example_async.asyncc.c")
        local dst_file = path.join(gendir, "example_async.c")
        
        os.runv("python3", {
            path.join(projectdir, "asyncc_preprocess.py"), 
            src_file, 
            dst_file
        })
        target:add("files", dst_file)
    end)

