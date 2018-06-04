call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64

set includedirs=/I""%ZENO_ROOT%"" /I""%ZENO_ROOT%"\libs\os_call\src" /I""%ZENO_ROOT%"\libs\dirent\src" /I""%ZENO_ROOT%"\libs\pthread\src" /I""%ZENO_ROOT%"\libs\zip\src" /I""%ZENO_ROOT%"\libs\cJSON\src" /I""%ZENO_ROOT%"\libs\ini\src" /I""%ZENO_ROOT%"\ZenCommon"
set libdirs=/LIBPATH:""%ZENO_ROOT%"\libs\pthread\lib\1.0.0.0" /LIBPATH:""%ZENO_ROOT%"\libs\ZenCommon\lib_msvc\1.0.0.0"
set srcfiles=ZenEngine.c "%ZENO_ROOT%"\libs\ini\src\ini.c
set libs="ZenCommon.lib" "libpthreadGC2.a"

set compilerflags=/Fo"bin/Debug/" %includedirs% /GS /W3 /Zc:wchar_t /ZI /Gm /Od /sdl /Fd"bin\Debug\vc141.pdb" /Zc:inline /fp:precise /D "_CRT_SECURE_NO_WARNINGS" /D "HAVE_STRUCT_TIMESPEC" /D "_DEBUG" /D "_CONSOLE" /D "_UNICODE" /D "UNICODE" /errorReport:prompt /WX- /Zc:forScope /Gd /Oy- /MDd /Fp"bin\Debug\ZenEngine.pch"
set linkerflags= /OUT:"bin\Debug\ZenEngine.exe" /MANIFEST /NXCOMPAT /PDB:"bin\Debug\ZenEngine.pdb" /DYNAMICBASE %libs% "kernel32.lib" "user32.lib" "gdi32.lib" "winspool.lib" "comdlg32.lib" "advapi32.lib" "shell32.lib" "ole32.lib" "oleaut32.lib" "uuid.lib" "odbc32.lib" "odbccp32.lib" %libdirs% /MACHINE:X64 /INCREMENTAL /SUBSYSTEM:CONSOLE /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /ManifestFile:"bin\Debug\ZenEngine.exe.intermediate.manifest" /ERRORREPORT:PROMPT /NOLOGO /TLBID:1 

cl.exe %compilerflags% %srcfiles% /link %linkerflags%