set_project("eventide")

add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_allowedplats("windows", "linux", "macosx")

option("dev", { default = true })
option("test", { default = true })
option("async", { default = true })
option("ztest", { default = true })
option("serde", { default = true })
option("option", { default = true })
option("deco", { default = true })
option("serde_simdjson", { default = false })
option("serde_yyjson", { default = false })
option("serde_flatbuffers", { default = false })

if has_config("serde_yyjson") and not has_config("serde_simdjson") then
	raise("serde_yyjson requires serde_simdjson")
end

if has_config("dev") then
	-- Don't fetch system package
	set_policy("package.install_only", true)
	set_policy("build.ccache", true)
	-- Keep Windows toolchains aligned with third-party prebuilt packages (e.g. cpptrace),
	-- otherwise ASan-specific /failifmismatch metadata can break linking.
	if is_mode("debug") and not is_plat("windows") then
		set_policy("build.sanitizer.address", true)
	end

	add_rules("plugin.compile_commands.autoupdate", { outputdir = "build", lsp = "clangd" })

	if is_plat("windows") then
		set_runtimes("MD")

		local toolchain = get_config("toolchain")
		if toolchain == "clang" then
			add_ldflags("-fuse-ld=lld-link")
			add_shflags("-fuse-ld=lld-link")
		elseif toolchain == "clang-cl" then
			set_toolset("ld", "lld-link")
			set_toolset("sh", "lld-link")
		end
	elseif is_plat("macosx") then
		-- https://conda-forge.org/docs/maintainer/knowledge_base/#newer-c-features-with-old-sdk
		add_defines("_LIBCPP_DISABLE_AVAILABILITY=1")
		add_ldflags("-fuse-ld=lld")
		add_shflags("-fuse-ld=lld")

		add_requireconfs("**|cmake", {
			configs = {
				ldflags = "-fuse-ld=lld",
				shflags = "-fuse-ld=lld",
				cxflags = "-D_LIBCPP_DISABLE_AVAILABILITY=1",
			},
		})
	end
end

set_languages("c++23")

if has_config("async") then
	add_requires("libuv v1.52.0")
end
if has_config("ztest") then
	add_requires("cpptrace v1.0.4")
end
if has_config("serde") and has_config("serde_simdjson") then
	add_requires("simdjson v4.2.4")
	add_requires("yyjson 0.12.0")
end
if has_config("serde") and has_config("serde_flatbuffers") then
	add_requires("flatbuffers v25.2.10")
end
if has_config("test") and is_plat("windows") then
	add_requires("unistd_h")
end

target("common", function()
	set_kind("headeronly")
	add_includedirs("include", { public = true })
	add_headerfiles("include/(eventide/common/*)")
end)

target("reflection", function()
	set_kind("headeronly")
	add_includedirs("include", { public = true })
	add_headerfiles("include/(eventide/reflection/*)")
	add_deps("common")
end)

if has_config("serde") and has_config("serde_simdjson") then
	target("serde_json", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/serde/json/*)")
		add_deps("reflection")
		add_packages("simdjson", { public = true })
		add_packages("yyjson", { public = true })
	end)
end

if has_config("serde") and has_config("serde_flatbuffers") then
	target("serde_flatbuffers", function()
		set_kind("$(kind)")
		add_files("src/serde/flatbuffers/flex/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles(
			"include/(eventide/serde/flatbuffers/*)",
			"include/(eventide/serde/flatbuffers/flex/*)",
			"include/(eventide/serde/flatbuffers/schema/*)"
		)
		add_deps("reflection")
		add_packages("flatbuffers", { public = true })
	end)
end

if has_config("serde") then
	target("serde", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/serde/*)", "include/(eventide/serde/attrs/*)")
		add_deps("common", "reflection")
		if has_config("serde_simdjson") then
			add_deps("serde_json")
		end
		if has_config("serde_flatbuffers") then
			add_deps("serde_flatbuffers")
		end
	end)
end

if has_config("ztest") then
	target("ztest", function()
		set_kind("$(kind)")
		add_files("src/zest/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/zest/*)")
		add_cxflags("cl::/Zc:preprocessor", { public = true })
		add_deps("common")
		add_packages("cpptrace", { public = true })
	end)
end

if has_config("async") then
	target("async", function()
		set_kind("$(kind)")
		add_files("src/async/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/async/*)")
		add_deps("common")
		add_packages("libuv")
	end)
end

if has_config("option") then
	target("option", function()
		set_kind("$(kind)")
		add_files("src/option/*.cc")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/option/*)")
		add_deps("common")
	end)
end

if has_config("deco") and has_config("option") then
	target("deco", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_cxflags("cl::/Zc:preprocessor", { public = true })
		add_headerfiles("include/(eventide/deco/*)")
		add_deps("option")
	end)
end

if has_config("async") and has_config("serde") and has_config("serde_simdjson") then
	target("jsonrpc", function()
		set_kind("$(kind)")
		add_files("src/jsonrpc/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/jsonrpc/*)")
		add_deps("async", "serde_json")
	end)

	target("language", function()
		set_kind("$(kind)")
		add_files("src/language/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/language/*)")
		add_deps("jsonrpc")
	end)
end

if has_config("test") and has_config("ztest") then
	target("unit_tests", function()
		set_default(false)
		set_kind("binary")
		add_files("tests/main.cpp", "tests/reflection/**.cpp", "tests/zest/**.cpp")
		if has_config("async") then
			add_files("tests/eventide/**.cpp")
		end
		if has_config("option") then
			add_files("tests/option/**.cpp")
		end
		if has_config("deco") and has_config("option") then
			add_files("tests/deco/**.cc")
		end
		if has_config("serde") and has_config("serde_simdjson") then
			add_files("tests/serde/json/simdjson_*.cpp")
		end
		if has_config("serde") and has_config("serde_yyjson") then
			add_files("tests/serde/json/dom_tests.cpp", "tests/serde/json/yyjson_*.cpp")
		end
		if has_config("serde") and has_config("serde_flatbuffers") then
			add_files("tests/serde/flatbuffers/**.cpp")
		end
		if has_config("async") and has_config("serde") and has_config("serde_simdjson") then
			add_files("tests/jsonrpc/**.cpp")
			add_files("tests/language/**.cpp")
		end

		add_includedirs("include")
		add_deps("common", "reflection", "ztest")
		if has_config("async") then
			add_deps("async")
		end
		if has_config("option") then
			add_deps("option")
		end
		if has_config("deco") and has_config("option") then
			add_deps("deco")
		end
		if has_config("serde") then
			add_deps("serde")
			if has_config("serde_simdjson") then
				add_deps("serde_json")
			end
			if has_config("serde_flatbuffers") then
				add_deps("serde_flatbuffers")
			end
		end
		if has_config("async") and has_config("serde") and has_config("serde_simdjson") then
			add_deps("jsonrpc")
			add_deps("language")
		end

		if has_config("test") and is_plat("windows") then
			add_packages("unistd_h")
		end

		add_tests("default")
	end)
end
