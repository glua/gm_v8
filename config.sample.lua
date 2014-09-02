config={}
--This is the directory of the source sdk
--By default we assume the sdk repo is cloned in the same directory as ours.
config.source_sdk_dir = "../source-sdk-2013/"
--This is the directory of V8 headers and compiled libraries
--Compiling or obtaining them is up to you. Have fun.
--We expect a directory containing an "include" directory and a "lib" directory
config.v8_dir = "thirdparty/v8/"
--If you're on windows, the compiled module will be moved here.
--Set it to nil, comment it out, or delete the line to disable
config.out = "%ProgramFiles(x86)%\\Steam\\steamapps\\common\\GarrysMod\\garrysmod\\lua\\bin\\"