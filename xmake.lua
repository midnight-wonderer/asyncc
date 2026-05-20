-- xmake.lua
set_project("asyncc")
set_version("0.0.1")

-- Add include directory to all targets
add_includedirs(".")

-- Build the original example
target("example1")
    set_kind("binary")
    add_files("examples/example1.c")

-- Build the new preprocessed async example
target("example_async")
    set_kind("binary")
    
    -- Run preprocessor during loading to generate the C files and register them
    on_load(function (target)
        local projectdir = os.projectdir()
        local gendir = path.join(projectdir, "build", "generated")
        os.mkdir(gendir)
        
        -- Find all .async files in examples/
        local async_files = os.files(path.join(projectdir, "examples", "*.async"))
        for _, async_file in ipairs(async_files) do
            local filename = path.filename(async_file)
            local c_file = path.join(gendir, (filename:gsub("%.async$", ".c")))
            
            -- Run the Python preprocessor
            os.runv("python3", {
                path.join(projectdir, "asyncc_preprocess.py"), 
                async_file, 
                c_file
            })
            
            -- Dynamically add the generated C file to the compilation list
            target:add("files", c_file)
        end
    end)
