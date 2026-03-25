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
option("serde_toml", { default = false })

if has_config("serde_yyjson") and not has_config("serde_simdjson") then
	raise("serde_yyjson requires serde_simdjson")
end

if has_config("ztest") and (not has_config("deco") or not has_config("option")) then
	raise("ztest requires deco and option")
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
if has_config("serde") and has_config("serde_toml") then
	add_requires("toml++ v3.4.0")
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
		add_headerfiles(
			"include/(eventide/serde/json.h)",
			"include/(eventide/serde/json/**.h)",
			"include/(eventide/serde/content.h)",
			"include/(eventide/serde/content/**.h)",
			"include/(eventide/serde/content/**.inl)"
		)
		add_deps("reflection")
		add_packages("simdjson", { public = true })
		add_packages("yyjson", { public = true })
	end)
end

if has_config("serde") and has_config("serde_flatbuffers") then
	target("serde_flatbuffers", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/serde/flatbuffers.h)", "include/(eventide/serde/flatbuffers/**.h)")
		add_deps("reflection")
		add_packages("flatbuffers", { public = true })
	end)
end

if has_config("serde") and has_config("serde_toml") then
	target("serde_toml", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/serde/toml.h)", "include/(eventide/serde/toml/**.h)")
		add_deps("reflection")
		add_packages("toml++", { public = true })
	end)
end

if has_config("serde") then
	target("serde", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles(
			"include/(eventide/serde/bincode.h)",
			"include/(eventide/serde/bincode/**.h)",
			"include/(eventide/serde/serde/**.h)"
		)
		add_deps("common", "reflection")
		if has_config("serde_simdjson") then
			add_headerfiles(
				"include/(eventide/serde/json.h)",
				"include/(eventide/serde/json/**.h)",
				"include/(eventide/serde/content.h)",
				"include/(eventide/serde/content/**.h)",
				"include/(eventide/serde/content/**.inl)"
			)
			add_deps("serde_json")
		end
		if has_config("serde_flatbuffers") then
			add_headerfiles("include/(eventide/serde/flatbuffers.h)", "include/(eventide/serde/flatbuffers/**.h)")
			add_deps("serde_flatbuffers")
		end
		if has_config("serde_toml") then
			add_headerfiles("include/(eventide/serde/toml.h)", "include/(eventide/serde/toml/**.h)")
			add_deps("serde_toml")
		end
	end)
end

if has_config("ztest") then
	target("ztest", function()
		set_kind("$(kind)")
		add_files("src/zest/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/zest/**)")
		add_cxflags("cl::/Zc:preprocessor", { public = true })
		add_deps("common", "deco")
		add_packages("cpptrace", { public = true })
	end)
end

if has_config("async") then
	target("async", function()
		set_kind("$(kind)")
		add_files("src/async/**.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/async/**)")
		add_deps("common")
		add_packages("libuv")
	end)
end

if has_config("option") then
	target("option", function()
		set_kind("$(kind)")
		add_files("src/option/*.cc")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/option/option.h)", "include/(eventide/option/detail/**.h)")
		add_deps("common")
	end)
end

if has_config("deco") and has_config("option") then
	target("deco", function()
		set_kind("$(kind)")
		add_files("src/deco/*.cc")
		add_includedirs("include", { public = true })
		add_cxflags("cl::/Zc:preprocessor", { public = true })
		add_headerfiles("include/(eventide/deco/deco.h)", "include/(eventide/deco/detail/**.h)")
		add_deps("option")
	end)
end

if has_config("async") and has_config("serde") and has_config("serde_simdjson") then
	target("ipc", function()
		set_kind("$(kind)")
		add_files("src/ipc/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/ipc/*)")
		add_deps("async", "serde_json")
	end)

	target("language", function()
		set_kind("$(kind)")
		add_files("src/ipc/lsp/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(eventide/ipc/lsp/*)")
		add_deps("ipc")
	end)
end

if has_config("test") and has_config("ztest") then
	target("unit_tests", function()
		set_default(false)
		set_kind("binary")
		add_files(
			"tests/unit/main.cpp",
			"tests/unit/common/**.cpp",
			"tests/unit/reflection/**.cpp",
			"tests/unit/zest/**.cpp"
		)
		if has_config("async") then
			add_files("tests/unit/async/**.cpp")
			add_includedirs("examples/build_system")
		end
		if has_config("option") then
			add_files("tests/unit/option/**.cpp")
		end
		if has_config("deco") and has_config("option") then
			add_files("tests/unit/deco/**.cc")
		end
		if has_config("serde") and has_config("serde_simdjson") then
			add_files("tests/unit/serde/json/simdjson_*.cpp")
		end
		if has_config("serde") and has_config("serde_yyjson") then
			add_files("tests/unit/serde/content/**.cpp")
		end
		if has_config("serde") and has_config("serde_flatbuffers") then
			add_files("tests/unit/serde/flatbuffers/**.cpp")
		end
		if has_config("serde") and has_config("serde_toml") then
			add_files("tests/unit/serde/toml/**.cpp")
		end
		if has_config("serde") then
			add_files("tests/unit/serde/bincode/**.cpp")
		end
		if has_config("async") and has_config("serde") and has_config("serde_simdjson") then
			add_files("tests/unit/ipc/**.cpp")
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
			if has_config("serde_toml") then
				add_deps("serde_toml")
			end
		end
		if has_config("async") and has_config("serde") and has_config("serde_simdjson") then
			add_deps("ipc")
			add_deps("language")
		end

		if has_config("test") and is_plat("windows") then
			add_packages("unistd_h")
		end

		add_tests("default")
	end)
end
