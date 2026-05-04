set_project("kotatsu")

add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_allowedplats("windows", "linux", "macosx")

option("dev", { default = true })
option("test", { default = true })
option("async", { default = true })
option("http", { default = true })
option("ztest", { default = true })
option("codec", { default = true })
option("option", { default = true })
option("deco", { default = true })
option("codec_simdjson", { default = false })
option("codec_flatbuffers", { default = false })
option("codec_toml", { default = false })

if has_config("ztest") and (not has_config("deco") or not has_config("option")) then
	raise("ztest requires deco and option")
end

if has_config("http") and not has_config("async") then
	raise("http requires async")
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

if is_plat("windows") then
	add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
end

if has_config("async") then
	add_requires("libuv v1.52.0")
end
if has_config("http") then
	add_requires("libcurl 8.11.0")
end
if has_config("ztest") then
	add_requires("cpptrace v1.0.4")
end
if has_config("codec") and has_config("codec_simdjson") then
	add_requires("simdjson v4.2.4")
end
if has_config("codec") and has_config("codec_flatbuffers") then
	add_requires("flatbuffers v25.2.10")
end
if has_config("codec") and has_config("codec_toml") then
	add_requires("toml++ v3.4.0")
end
if has_config("test") and is_plat("windows") then
	add_requires("unistd_h")
end

rule("cl-flags")
on_load(function(target)
	target:add("cxflags", "cl::/Zc:preprocessor", "cl::/utf-8", { public = true })
end)

target("support", function()
	set_kind("$(kind)")
	add_files("src/support/*.cpp")
	add_includedirs("include", { public = true })
	add_headerfiles("include/(kota/support/*)")
	add_rules("cl-flags")
end)

target("meta", function()
	set_kind("headeronly")
	add_includedirs("include", { public = true })
	add_headerfiles("include/(kota/meta/*)")
	add_deps("support")
	add_rules("cl-flags")
end)

if has_config("codec") and has_config("codec_simdjson") then
	target("codec_json", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles(
			"include/(kota/codec/json.h)",
			"include/(kota/codec/json/**.h)",
			"include/(kota/codec/content.h)",
			"include/(kota/codec/content/**.h)",
			"include/(kota/codec/content/**.inl)"
		)
		add_rules("cl-flags")
		add_deps("meta")
		add_packages("simdjson", { public = true })
	end)
end

if has_config("codec") and has_config("codec_flatbuffers") then
	target("codec_flatbuffers", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/codec/flatbuffers.h)", "include/(kota/codec/flatbuffers/**.h)")
		add_rules("cl-flags")
		add_deps("meta")
		add_packages("flatbuffers", { public = true })
	end)
end

if has_config("codec") and has_config("codec_toml") then
	target("codec_toml", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/codec/toml.h)", "include/(kota/codec/toml/**.h)")
		add_rules("cl-flags")

		add_deps("meta")
		add_packages("toml++", { public = true })
	end)
end

if has_config("codec") then
	target("codec", function()
		set_kind("headeronly")
		add_includedirs("include", { public = true })
		add_headerfiles(
			"include/(kota/codec/bincode.h)",
			"include/(kota/codec/bincode/**.h)",
			"include/(kota/codec/*.h)",
			"include/(kota/codec/detail/**.h)"
		)
		add_rules("cl-flags")
		add_deps("support", "meta")
		if has_config("codec_simdjson") then
			add_headerfiles(
				"include/(kota/codec/json.h)",
				"include/(kota/codec/json/**.h)",
				"include/(kota/codec/content.h)",
				"include/(kota/codec/content/**.h)",
				"include/(kota/codec/content/**.inl)"
			)
			add_deps("codec_json")
		end
		if has_config("codec_flatbuffers") then
			add_headerfiles("include/(kota/codec/flatbuffers.h)", "include/(kota/codec/flatbuffers/**.h)")
			add_deps("codec_flatbuffers")
		end
		if has_config("codec_toml") then
			add_headerfiles("include/(kota/codec/toml.h)", "include/(kota/codec/toml/**.h)")
			add_deps("codec_toml")
		end
	end)
end

if has_config("ztest") then
	target("ztest", function()
		set_kind("$(kind)")
		add_files("src/zest/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/zest/**)")
		add_rules("cl-flags")
		add_deps("support", "deco")
		add_packages("cpptrace", { public = true })
		add_defines('KOTA_ZEST_BUILD_ROOT="' .. os.projectdir():gsub("\\", "/") .. '"')
	end)
end

if has_config("async") then
	target("async", function()
		set_kind("$(kind)")
		add_rules("cl-flags")
		add_files("src/async/**.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/async/**)")
		add_deps("support")
		add_packages("libuv", { public = true })
		if is_plat("macosx") then
			add_frameworks("CoreServices")
		end
	end)
end

if has_config("http") then
	target("http", function()
		set_kind("$(kind)")
		add_rules("cl-flags")
		add_files("src/http/**.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/http/**)")
		add_deps("async")
		if has_config("codec") and has_config("codec_simdjson") then
			add_deps("codec_json")
		end
		add_packages("libcurl", { public = true })
	end)
end

if has_config("option") then
	target("option", function()
		set_kind("$(kind)")
		add_rules("cl-flags")
		add_files("src/option/*.cc")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/option/option.h)", "include/(kota/option/detail/**.h)")
		add_deps("support")
	end)
end

if has_config("deco") and has_config("option") then
	target("deco", function()
		set_kind("$(kind)")
		add_files("src/deco/*.cc")
		add_includedirs("include", { public = true })
		add_rules("cl-flags")
		add_headerfiles("include/(kota/deco/deco.h)", "include/(kota/deco/detail/**.h)")
		add_deps("option")
	end)
end

if has_config("async") then
	target("ipc", function()
		set_kind("$(kind)")
		add_rules("cl-flags")
		add_files("src/ipc/*.cpp")
		add_files("src/ipc/codec/bincode.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/ipc/*)")
		if has_config("codec") and has_config("codec_simdjson") then
			add_files("src/ipc/codec/json.cpp")
			add_deps("codec_json")
		end
		add_deps("async")
	end)

	target("language", function()
		set_kind("$(kind)")
		add_rules("cl-flags")
		add_files("src/ipc/lsp/*.cpp")
		add_includedirs("include", { public = true })
		add_headerfiles("include/(kota/ipc/lsp/*)")
		add_deps("ipc")
	end)
end

target("kotatsu", function()
	set_default(false)
	set_kind("static")
	add_rules("cl-flags")
	add_includedirs("include", { public = true })
	add_headerfiles("include/(kota/**)")

	add_deps("support", "meta", { public = true })

	if has_config("codec") then
		add_deps("codec", { public = true })
		if has_config("codec_simdjson") then
			add_deps("codec_json", { public = true })
			add_packages("simdjson", { public = true })
		end
		if has_config("codec_flatbuffers") then
			add_deps("codec_flatbuffers", { public = true })
			add_packages("flatbuffers", { public = true })
		end
		if has_config("codec_toml") then
			add_deps("codec_toml", { public = true })
			add_packages("toml++", { public = true })
		end
	end

	if has_config("option") then
		add_deps("option", { public = true })
	end
	if has_config("deco") and has_config("option") then
		add_deps("deco", { public = true })
	end
	if has_config("ztest") then
		add_deps("ztest", { public = true })
		add_packages("cpptrace", { public = true })
	end
	if has_config("async") then
		add_deps("async", { public = true })
		add_packages("libuv", { public = true })
	end
	if has_config("http") then
		add_deps("http", { public = true })
		add_packages("libcurl", { public = true })
	end
	if has_config("async") then
		add_deps("ipc", "language", { public = true })
	end

	after_link(function(target, opt)
		import("utils.archive.merge_staticlib")
		import("core.project.depend")
		import("utils.progress")
		local libraryfiles = {}

		for _, dep in ipairs(target:orderdeps()) do
			if dep:is_static() then
				table.insert(libraryfiles, dep:targetfile())
			end
		end

		depend.on_changed(function()
			progress.show(opt.progress, "${color.build.target}merging.$(mode) %s", path.filename(target:targetfile()))
			if #libraryfiles > 0 then
				local tmpfile = os.tmpfile() .. path.extension(target:targetfile())
				merge_staticlib(target, tmpfile, libraryfiles)
				os.cp(tmpfile, target:targetfile())
				os.rm(tmpfile)
			end
		end, {
			dependfile = target:dependfile(target:targetfile() .. ".merge_archive"),
			files = libraryfiles,
			changed = target:is_rebuilt(),
		})
	end)
end)

if has_config("test") and has_config("ztest") then
	target("unit_tests", function()
		set_default(false)
		set_kind("binary")
		add_rules("cl-flags")
		add_includedirs("tests")
		add_files(
			"tests/unit/main.cpp",
			"tests/unit/support/**.cpp",
			"tests/unit/meta/**.cpp",
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
		if has_config("codec") and has_config("codec_simdjson") then
			add_files("tests/unit/codec/json/**.cpp")
		end
		if has_config("codec") then
			add_files("tests/unit/codec/content/**.cpp")
		end
		if has_config("codec") and has_config("codec_flatbuffers") then
			add_files("tests/unit/codec/flatbuffers/**.cpp")
		end
		if has_config("codec") and has_config("codec_toml") then
			add_files("tests/unit/codec/toml/**.cpp")
		end
		if has_config("codec") then
			add_files("tests/unit/codec/bincode/**.cpp")
		end
		if has_config("async") and has_config("codec") and has_config("codec_simdjson") then
			add_files("tests/unit/ipc/**.cpp")
		end
		if has_config("http") then
			add_files("tests/unit/http/**.cpp")
		end

		add_deps("kotatsu")

		add_tests("default")
	end)
end
