require("config")

local module_name = "v8"

local os_names = {windows="win32",macosx="osx",linux="linux"}

local function setTarget(targetName)
	targetname(targetName)
	if os.get()=="windows" and config.out then
		postbuildcommands{"move /Y ..\\build\\windows\\"..targetName..'.dll "'..config.out..'"'}
	end
end

solution("gmodscripts-module")
	language("C++")
	kind("SharedLib")
	location("proj-"..os.get() .."-".._ACTION)

	defines{"GMMODULE"}
	flags{"Symbols","NoEditAndContinue","NoPCH","StaticRuntime","EnableSSE"}

	--From relese config
	defines{"NDEBUG"}
	flags{"Optimize","FloatFast"}

	includedirs{"include/",config.source_sdk_dir.."mp/src/public/",config.v8_dir.."include/"}

	libdirs{config.source_sdk_dir.."mp/src/lib/public/",config.v8_dir.."lib/"}
	links{ --Not sure what libs we need. Will it hurt to link to them all?
		"appframework",
		"bitmap",
		"choreoobjects",
		"dmxloader",
		"fgdlib",
		"libprotobuf",
		"libz",
		"mathlib",
		"matsys_controls",
		"nvtristrip",
		"particles",
		"raytrace",
		"shaderlib",
		"steam_api",
		"tier0",
		"tier1",
		"tier2",
		"tier3",
		"vgui_controls",
		"vmpi",
		"vstdlib",
		"vtf",

		--V8
		"v8_base",
		"v8_snapshot",
		"v8_libbase",
		--"v8_libplatform", not needed?
		"icuuc",
		"icui18n",

		--Windows dependency of V8 (wtf)
		"winmm"
	}

	targetdir("build/"..os.get().."/")

	configurations{ 
		"1-ServerModule","2-ClientModule"
	}
	
	configuration("1-ServerModule")
		setTarget("gmsv_"..module_name.."_"..os_names[os.get()])
	configuration("2-ClientModule")
		setTarget("gmcl_"..module_name.."_"..os_names[os.get()])

	project("gmodscripts-module")
		files{"src/**.*","include/**.*"}