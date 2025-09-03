add_rules("mode.debug", "mode.release")

add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

set_languages("c++latest")
set_toolchains("clang")

set_policy("build.c++.modules", true)
set_policy("build.c++.modules.std", true)

add_requires("stdexec", {optional = false})
add_requires("openmp", {system = false})

set_runtimes("c++_shared")
add_cxxflags("-stdlib=libc++")

target("thread_pool")
    set_kind("static")
    add_packages("stdexec")
    add_files("src/thread_pool.cppm", {public = true})
    add_files("src/thread_pool.cpp")

target("thread_pool_test")
    set_kind("binary")
    add_packages("stdexec")
    add_packages("openmp")
    add_deps("thread_pool")
    add_files("test/thread_pool_test.cpp")
