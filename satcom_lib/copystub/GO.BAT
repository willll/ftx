@echo off

@REM Add Saturn SDK / SaturnOrbit stuff to environment variables
CALL ..\..\satcom_lib_defs.bat satsdk_en


mkdir obj

:start
echo --------------------------------------------
echo  Type `c' to cleanup temporary data.
echo  Type `r' to rebuild program.
SET /P cm="Command: "

If "%cm%" == "c" goto cleanup
If "%cm%" == "r" goto rebuild
goto end


:cleanup
make clean
goto start

:rebuild
make mrproper
make
dir copystub.bin
..\..\tools\bin2c copystub.bin copystub_inc.c
echo OK!
goto start




:end

