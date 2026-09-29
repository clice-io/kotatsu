-- Keys and trimming for the caches of the xmake legs in CI
-- (.github/workflows/xmake.yml). Run in the configured project:
--
--     xmake lua .github/scripts/xmake_cache.lua <command>

import("core.base.hashset")
import("core.base.json")
import("core.cache.localcache")
import("core.package.package")
import("core.project.config")
import("core.tool.compiler")

-- Appends `name=value` lines to the file that a GitHub Actions variable names.
function _append(variable, values)
	local file = io.open(os.getenv(variable), "a")
	for name, value in pairs(values) do
		file:write(name .. "=" .. value .. "\n")
	end
	file:close()
end

-- The install directories of the packages this configuration uses, tools
-- included, as configure records them for `xmake require --clean`.
function _used_packages()
	local dirs = {}
	for _, dir in ipairs(localcache.get("references", "packages") or {}) do
		table.insert(dirs, path.normalize(dir))
	end
	return dirs
end

-- PACKAGE_CACHE_KEY, which ends in a hash of the packages' install directories
-- (names, versions and build hashes); COMPILER_HASH, of the C++ compiler's
-- version, which xmake's compiler cache leaves out of its own keys; and
-- STARTED_AT, before the compiler cache is restored.
function _keys()
	local packages = _used_packages()
	assert(#packages > 0, "configure recorded no packages")
	table.sort(packages)
	local cxx = compiler.load("cxx"):program()
	_append("GITHUB_ENV", {
		PACKAGE_CACHE_KEY = os.getenv("PACKAGE_CACHE_PREFIX") .. "/" .. hash.strhash128(table.concat(packages, ",")),
		COMPILER_HASH = hash.strhash128((os.iorunv(cxx, { "--version" }))),
		STARTED_AT = os.time(),
	})
	-- sccache indexes its directory on its first lookup, which configure's
	-- compiles made; a new server finds what the restore adds.
	if os.getenv("COMPILER_CACHE") == "sccache" then
		os.execv("sccache", { "--stop-server" }, { try = true })
	end
end

-- Removes the packages this configuration does not use, installed in
-- <root>/<letter>/<name>/<version>/<build hash>, or in <build hash> directly
-- under the name for a package without a version.
function _trim_package_cache()
	local used = hashset.from(_used_packages())
	for _, dir in ipairs(os.dirs(path.join(package.installdir(), "*", "*", "*"))) do
		local installdirs = os.isfile(path.join(dir, "manifest.txt")) and { dir } or os.dirs(path.join(dir, "*"))
		for _, installdir in ipairs(installdirs) do
			installdir = path.normalize(installdir)
			if not used:has(installdir) then
				print("removing %s", installdir)
				os.rm(installdir)
			end
		end
	end
end

-- Removes the entries the build did not use, so that a saved entry holds one
-- build instead of growing run after run, and outputs whether it changed.
function _trim_compiler_cache()
	local pattern = path.join(os.getenv("COMPILER_CACHE_DIR"), "**")
	local started = tonumber(os.getenv("STARTED_AT"))
	local dropped = 0
	local changed
	if os.getenv("COMPILER_CACHE") == "sccache" then
		-- sccache refreshes an entry's mtime on a hit, and its server keeps
		-- count of the entries, so it stops first.
		os.execv("sccache", { "--show-stats" })
		local stats = json.decode(os.iorunv("sccache", { "--show-stats", "--stats-format=json" })).stats
		os.execv("sccache", { "--stop-server" })
		-- xmake finding the real clang-cl would otherwise go unnoticed.
		assert(stats.compile_requests > 0, "no compile went through sccache")
		for _, file in ipairs(os.files(pattern)) do
			if os.mtime(file) < started then
				os.rm(file)
				dropped = dropped + 1
			end
		end
		print("compiler cache: %d entries dropped", dropped)
		changed = stats.cache_writes + dropped > 0
	else
		-- xmake's cache leaves no trace of a hit, but each object in
		-- build/.objs, compiled or taken from the cache, is byte for byte a
		-- copy that the cache holds.
		local objects = hashset.new()
		for _, file in ipairs(os.files("build/.objs/**")) do
			objects:insert(hash.xxhash128(file))
		end
		assert(not objects:empty(), "the build left no objects")
		local added, size = 0, 0
		for _, file in ipairs(os.files(pattern)) do
			-- Next to an object, <key>.txt holds the compiler output a hit replays.
			if not file:endswith(".txt") then
				if objects:has(hash.xxhash128(file)) then
					size = size + os.filesize(file)
					if os.mtime(file) >= started then
						added = added + 1
					end
				else
					os.rm(file)
					os.tryrm(file .. ".txt")
					dropped = dropped + 1
				end
			end
		end
		print("compiler cache: %d objects added, %d dropped, %.0f MiB kept", added, dropped, size / 1024 / 1024)
		changed = added + dropped > 0
	end
	_append("GITHUB_OUTPUT", { changed = changed and "true" or "false" })
end

function main(command)
	config.load()
	if command == "keys" then
		_keys()
	elseif command == "trim-package-cache" then
		_trim_package_cache()
	elseif command == "trim-compiler-cache" then
		_trim_compiler_cache()
	else
		raise("unknown command: %s", tostring(command))
	end
end
