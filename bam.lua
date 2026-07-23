Import("configure.lua")

--- Setup Config -------
config = NewConfig()
config:Add(OptCCompiler("compiler"))
config:Add(OptTestCompileC("stackprotector", "int main(){return 0;}", "-fstack-protector -fstack-protector-all"))
config:Add(OptTestCompileC("minmacosxsdk", "int main(){return 0;}", "-mmacosx-version-min=10.5 -isysroot /Developer/SDKs/MacOSX10.5.sdk"))
config:Add(OptTestCompileC("macosxppc", "int main(){return 0;}", "-arch ppc"))
config:Add(OptLibrary("zlib", "zlib.h", false))
config:Finalize("config.lua")

-- data compiler
function Script(name)
	if family == "windows" then
		return str_replace(name, "/", "\\")
	end
	return "python " .. name
end

function CHash(output, ...)
	local inputs = TableFlatten({...})

	output = Path(output)

	-- compile all the files
	local cmd = Script("scripts/cmd5.py") .. " "
	for index, inname in ipairs(inputs) do
		cmd = cmd .. Path(inname) .. " "
	end

	cmd = cmd .. " > " .. output

	AddJob(output, "cmd5 " .. output, cmd)
	for index, inname in ipairs(inputs) do
		AddDependency(output, inname)
	end
	AddDependency(output, "scripts/cmd5.py")
	return output
end

function ResCompile(scriptfile)
	scriptfile = Path(scriptfile)
	if config.compiler.driver == "cl" then
		output = PathBase(scriptfile) .. ".res"
		AddJob(output, "rc " .. scriptfile, "rc /fo " .. output .. " " .. scriptfile)
	elseif config.compiler.driver == "gcc" then
		output = PathBase(scriptfile) .. ".coff"
		AddJob(output, "windres " .. scriptfile, "windres -i " .. scriptfile .. " -o " .. output)
	end
	AddDependency(output, scriptfile)
	return output
end

function ContentCompile(action, output)
	output = Path(output)
	AddJob(
		output,
		action .. " > " .. output,
		Script("datasrc/compile.py") .. " " .. action .. " > " .. Path(output)
	)
	AddDependency(output, Path("datasrc/content.py"))
	AddDependency(output, Path("datasrc/network.py"))
	AddDependency(output, Path("datasrc/compile.py"))
	AddDependency(output, Path("datasrc/datatypes.py"))
	return output
end

function ContentCompile7(action, output)
	output = Path(output)
	AddJob(
		output,
		"seven " .. action .. " > " .. output,
		"PYTHONPATH=. " .. Script("-m datasrc.seven.compile") .. " " .. action .. " > " .. Path(output)
	)
	AddDependency(output, Path("datasrc/seven/compile.py"))
	AddDependency(output, Path("datasrc/seven/content.py"))
	AddDependency(output, Path("datasrc/seven/network.py"))
	AddDependency(output, Path("datasrc/seven/datatypes.py"))
	return output
end

function ContentCompileGlue(output)
	output = Path(output)
	AddJob(
		output,
		"crosscompile > " .. output,
		"PYTHONPATH=. " .. Script("datasrc/crosscompile.py") .. " > " .. Path(output)
	)
	AddDependency(output, Path("datasrc/crosscompile.py"))
	AddDependency(output, Path("datasrc/network.py"))
	AddDependency(output, Path("datasrc/seven/network.py"))
	AddDependency(output, Path("datasrc/compile.py"))
	AddDependency(output, Path("datasrc/seven/compile.py"))
	return output
end

-- Content Compile
network_source = ContentCompile("network_source", "src/game/generated/protocol.cpp")
network_header = ContentCompile("network_header", "src/game/generated/protocol.h")
network7_source = ContentCompile7("network_source", "src/game/generated/protocol7.cpp")
network7_header = ContentCompile7("network_header", "src/game/generated/protocol7.h")
protocolglue = ContentCompileGlue("src/game/generated/protocolglue.h")
server_content_source = ContentCompile("server_content_source", "src/game/generated/server_data.cpp")
server_content_header = ContentCompile("server_content_header", "src/game/generated/server_data.h")

AddDependency(network_source, network_header)
AddDependency(network7_source, network7_header)
AddDependency(network7_source, protocolglue)
AddDependency(protocolglue, network_header)
AddDependency(protocolglue, network7_header)
AddDependency(server_content_source, server_content_header)

nethash = CHash("src/game/generated/nethash.cpp", "src/engine/shared/protocol.h", "src/game/generated/protocol.h", "src/game/tuning.h", "src/game/gamecore.cpp", network_header)

server_link_other = {}

if family == "windows" then
	if config.compiler.driver == "cl" then
		server_link_other = {ResCompile("other/icons/teeworlds_srv_cl.rc")}
	elseif config.compiler.driver == "gcc" then
		server_link_other = {ResCompile("other/icons/teeworlds_srv_gcc.rc")}
	end
end

function Intermediate_Output(settings, input)
	return "objs/" .. string.sub(PathBase(input), string.len("src/")+1) .. settings.config_ext
end

function FilterPaths(paths, deny)
	local out = {}
	for _, p in ipairs(paths) do
		local skip = false
		for _, d in ipairs(deny) do
			if string.find(p, d, 1, true) then
				skip = true
				break
			end
		end
		if not skip then
			table.insert(out, p)
		end
	end
	return out
end

function build(settings)
	-- apply compiler settings
	config.compiler:Apply(settings)

	settings.cc.Output = Intermediate_Output

	if config.compiler.driver == "cl" then
		settings.cc.flags:Add("/wd4244")
	else
		settings.cc.flags:Add("-Wall", "-fno-exceptions")
		if family == "windows" then
			settings.cc.defines:Add("NO_VIZ")
		elseif platform == "macosx" then
			settings.cc.flags:Add("-mmacosx-version-min=10.5")
			settings.link.flags:Add("-mmacosx-version-min=10.5")
			if config.minmacosxsdk.value == 1 then
				settings.cc.flags:Add("-isysroot /Developer/SDKs/MacOSX10.5.sdk")
				settings.link.flags:Add("-isysroot /Developer/SDKs/MacOSX10.5.sdk")
			end
		elseif config.stackprotector.value == 1 then
			settings.cc.flags:Add("-fstack-protector", "-fstack-protector-all")
			settings.link.flags:Add("-fstack-protector", "-fstack-protector-all")
		end
	end

	settings.cc.includes:Add("src")
	settings.cc.defines:Add("CONF_OPENSSL")

	if family == "unix" then
		if platform == "macosx" then
			settings.link.frameworks:Add("Carbon")
			settings.link.frameworks:Add("AppKit")
		else
			settings.link.libs:Add("pthread")
		end
		settings.link.libs:Add("curl")
		settings.link.libs:Add("crypto")

		if platform == "solaris" then
			settings.link.flags:Add("-lsocket")
			settings.link.flags:Add("-lnsl")
		end
	elseif family == "windows" then
		settings.link.libs:Add("gdi32")
		settings.link.libs:Add("user32")
		settings.link.libs:Add("ws2_32")
		settings.link.libs:Add("ole32")
		settings.link.libs:Add("shell32")
		settings.link.libs:Add("libcurl")
		settings.link.libs:Add("libcrypto")
	end

	-- compile zlib if needed
	if config.zlib.value == 1 then
		settings.link.libs:Add("z")
		if config.zlib.include_path then
			settings.cc.includes:Add(config.zlib.include_path)
		end
		zlib = {}
	else
		zlib = Compile(settings, Collect("src/engine/external/zlib/*.c"))
		settings.cc.includes:Add("src/engine/external/zlib")
	end

	pnglite = Compile(settings, Collect("src/engine/external/pnglite/*.c"))
	json = Compile(settings, Collect("src/engine/external/json-parser/*.c"))

	engine_settings = settings:Copy()
	server_settings = engine_settings:Copy()
	launcher_settings = engine_settings:Copy()

	if platform == "macosx" then
		launcher_settings.link.frameworks:Add("Cocoa")
	end

	-- shared: skip leftover DDNet netban; base: OpenSSL hash only
	shared_src = FilterPaths(Collect("src/engine/shared/*.cpp"), {"netban.cpp"})
	base_src = {
		"src/base/system.c",
		"src/base/hash.c",
		"src/base/hash_openssl.c",
		"src/base/uuid.c",
	}

	engine = Compile(engine_settings, shared_src, base_src, network7_source)
	server = Compile(server_settings, Collect("src/engine/server/*.cpp"))

	versionserver = Compile(settings, Collect("src/versionsrv/*.cpp"))
	masterserver = Compile(settings, Collect("src/mastersrv/*.cpp"))
	game_shared = Compile(settings, Collect("src/game/*.cpp"), nethash, network_source)
	game_server = Compile(settings, CollectRecursive("src/game/server/*.cpp"), server_content_source)

	tools_src = Collect("src/tools/*.cpp", "src/tools/*.c")

	server_osxlaunch = {}
	if platform == "macosx" then
		server_osxlaunch = Compile(launcher_settings, "src/osxlaunch/server.m")
	end

	tools = {}
	for i,v in ipairs(tools_src) do
		toolname = PathFilename(PathBase(v))
		tools[i] = Link(settings, toolname, Compile(settings, v), engine, zlib, pnglite)
	end

	server_exe = Link(server_settings, "teeworlds_srv", engine, server,
		game_shared, game_server, zlib, json, server_link_other)

	serverlaunch = {}
	if platform == "macosx" then
		serverlaunch = Link(launcher_settings, "serverlaunch", server_osxlaunch)
	end

	versionserver_exe = Link(server_settings, "versionsrv", versionserver,
		engine, zlib)

	masterserver_exe = Link(server_settings, "mastersrv", masterserver,
		engine, zlib)

	s = PseudoTarget("server".."_"..settings.config_name, server_exe, serverlaunch)
	g = PseudoTarget("game".."_"..settings.config_name, server_exe)

	v = PseudoTarget("versionserver".."_"..settings.config_name, versionserver_exe)
	m = PseudoTarget("masterserver".."_"..settings.config_name, masterserver_exe)
	t = PseudoTarget("tools".."_"..settings.config_name, tools)

	all = PseudoTarget(settings.config_name, s, v, m, t)
	return all
end


debug_settings = NewSettings()
debug_settings.config_name = "debug"
debug_settings.config_ext = "_d"
debug_settings.debug = 1
debug_settings.optimize = 0
debug_settings.cc.defines:Add("CONF_DEBUG")

release_settings = NewSettings()
release_settings.config_name = "release"
release_settings.config_ext = ""
release_settings.debug = 0
release_settings.optimize = 1
release_settings.cc.defines:Add("CONF_RELEASE")

if platform == "macosx" then
	debug_settings_ppc = debug_settings:Copy()
	debug_settings_ppc.config_name = "debug_ppc"
	debug_settings_ppc.config_ext = "_ppc_d"
	debug_settings_ppc.cc.flags:Add("-arch ppc")
	debug_settings_ppc.link.flags:Add("-arch ppc")
	debug_settings_ppc.cc.defines:Add("CONF_DEBUG")

	release_settings_ppc = release_settings:Copy()
	release_settings_ppc.config_name = "release_ppc"
	release_settings_ppc.config_ext = "_ppc"
	release_settings_ppc.cc.flags:Add("-arch ppc")
	release_settings_ppc.link.flags:Add("-arch ppc")
	release_settings_ppc.cc.defines:Add("CONF_RELEASE")

	ppc_d = build(debug_settings_ppc)
	ppc_r = build(release_settings_ppc)

	if arch == "ia32" or arch == "amd64" then
		debug_settings_x86 = debug_settings:Copy()
		debug_settings_x86.config_name = "debug_x86"
		debug_settings_x86.config_ext = "_x86_d"
		debug_settings_x86.cc.flags:Add("-arch i386")
		debug_settings_x86.link.flags:Add("-arch i386")
		debug_settings_x86.cc.defines:Add("CONF_DEBUG")

		release_settings_x86 = release_settings:Copy()
		release_settings_x86.config_name = "release_x86"
		release_settings_x86.config_ext = "_x86"
		release_settings_x86.cc.flags:Add("-arch i386")
		release_settings_x86.link.flags:Add("-arch i386")
		release_settings_x86.cc.defines:Add("CONF_RELEASE")

		x86_d = build(debug_settings_x86)
		x86_r = build(release_settings_x86)
	end

	if arch == "amd64" then
		debug_settings_x86_64 = debug_settings:Copy()
		debug_settings_x86_64.config_name = "debug_x86_64"
		debug_settings_x86_64.config_ext = "_x86_64_d"
		debug_settings_x86_64.cc.flags:Add("-arch x86_64")
		debug_settings_x86_64.link.flags:Add("-arch x86_64")
		debug_settings_x86_64.cc.defines:Add("CONF_DEBUG")

		release_settings_x86_64 = release_settings:Copy()
		release_settings_x86_64.config_name = "release_x86_64"
		release_settings_x86_64.config_ext = "_x86_64"
		release_settings_x86_64.cc.flags:Add("-arch x86_64")
		release_settings_x86_64.link.flags:Add("-arch x86_64")
		release_settings_x86_64.cc.defines:Add("CONF_RELEASE")

		x86_64_d = build(debug_settings_x86_64)
		x86_64_r = build(release_settings_x86_64)
	end

	DefaultTarget("server_debug_x86")

	if config.macosxppc.value == 1 then
		if arch == "ia32" then
			PseudoTarget("release", ppc_r, x86_r)
			PseudoTarget("debug", ppc_d, x86_d)
			PseudoTarget("server_release", "server_release_ppc", "server_release_x86")
			PseudoTarget("server_debug", "server_debug_ppc", "server_debug_x86")
		elseif arch == "amd64" then
			PseudoTarget("release", ppc_r, x86_r, x86_64_r)
			PseudoTarget("debug", ppc_d, x86_d, x86_64_d)
			PseudoTarget("server_release", "server_release_ppc", "server_release_x86", "server_release_x86_64")
			PseudoTarget("server_debug", "server_debug_ppc", "server_debug_x86", "server_debug_x86_64")
		else
			PseudoTarget("release", ppc_r)
			PseudoTarget("debug", ppc_d)
			PseudoTarget("server_release", "server_release_ppc")
			PseudoTarget("server_debug", "server_debug_ppc")
		end
	else
		if arch == "ia32" then
			PseudoTarget("release", x86_r)
			PseudoTarget("debug", x86_d)
			PseudoTarget("server_release", "server_release_x86")
			PseudoTarget("server_debug", "server_debug_x86")
		elseif arch == "amd64" then
			PseudoTarget("release", x86_r, x86_64_r)
			PseudoTarget("debug", x86_d, x86_64_d)
			PseudoTarget("server_release", "server_release_x86", "server_release_x86_64")
			PseudoTarget("server_debug", "server_debug_x86", "server_debug_x86_64")
		end
	end
else
	build(debug_settings)
	build(release_settings)
	DefaultTarget("server_debug")
end
