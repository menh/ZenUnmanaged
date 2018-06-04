call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64

set includedirs=/I""%ZENO_ROOT%"" /I""%ZENO_ROOT%"\libs\dirent\src" /I""%ZENO_ROOT%"\libs\pthread\src" /I""%ZENO_ROOT%"\libs\paho.mqtt\src" /I""%ZENO_ROOT%"\libs\zip\src" /I""%ZENO_ROOT%"\libs\cJSON\src" /I""%ZENO_ROOT%"\libs\b64\src"
set libdirs=/LIBPATH:""%ZENO_ROOT%"\libs\pthread\lib\1.0.0.0" /LIBPATH:""%ZENO_ROOT%"\libs\paho.mqtt\lib\1.0.0.0"
set srcfiles=ZenCommon.c "%ZENO_ROOT%"\libs\cJSON\src\cJSON.c "%ZENO_ROOT%"\libs\b64\src\decode.c "%ZENO_ROOT%"\libs\b64\src\encode.c ZenMqtt.c "%ZENO_ROOT%"\libs\zip\src\zip.c
set libs="paho-mqtt3as.lib" "libpthreadGC2.a"

set compilerflags=/Fo"bin\Debug/" %includedirs% /GS /W3 /Zc:wchar_t  /ZI /Gm /Od /sdl /Fd"bin\Debug\vc141.pdb" /Zc:inline /fp:precise /D "_CRT_SECURE_NO_WARNINGS" /D "_DEBUG" /D "_WINDOWS" /D "_USRDLL" /D "ZENCOMMON_EXPORTS" /D "_WINDLL" /D "_UNICODE" /D "UNICODE" /errorReport:prompt /WX- /Zc:forScope /RTC1 /Gd /MDd   /Fp"bin\Debug\ZenCommon.pch" 
set linkerflags=/OUT:"bin\Debug\ZenCommon.dll"  %libdirs% /MANIFEST /NXCOMPAT /PDB:"bin\Debug\ZenCommon.pdb" /DYNAMICBASE %libs% "kernel32.lib" "user32.lib" "gdi32.lib" "winspool.lib" "comdlg32.lib" "advapi32.lib" "shell32.lib" "ole32.lib" "oleaut32.lib" "uuid.lib" "odbc32.lib" "odbccp32.lib" /IMPLIB:"bin\Debug\ZenCommon.lib" /DEBUG /DLL /MACHINE:X64 /INCREMENTAL  /SUBSYSTEM:WINDOWS /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /ManifestFile:"bin\Debug\ZenCommon.dll.intermediate.manifest" /ERRORREPORT:PROMPT /NOLOGO  /TLBID:1  

cl.exe %compilerflags% %srcfiles%  /link %linkerflags%