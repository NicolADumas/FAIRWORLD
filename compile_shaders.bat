@echo off
REM Script per ricompilare tutti gli shader GLSL -> SPV
REM Esegui questo ogni volta che modifichi un file .vert, .frag, o .comp

SET GLSLC=D:\VulkanSDK\Bin\glslc.exe
SET SRC=D:\FAIRWORLD\FAIRWORLD\src\render
SET BIN=D:\FAIRWORLD\FAIRWORLD\bin

echo [SHADERS] Compilazione in corso...

%GLSLC% %SRC%\shader.vert  -o %SRC%\vert.spv        && echo OK: vert.spv
%GLSLC% %SRC%\shader.frag  -o %SRC%\frag.spv        && echo OK: frag.spv  2>nul || (echo ATTENZIONE: shader.frag non trovato)
%GLSLC% %SRC%\forge.vert   -o %SRC%\forge_vert.spv  && echo OK: forge_vert.spv
%GLSLC% %SRC%\forge.frag   -o %SRC%\forge_frag.spv  && echo OK: forge_frag.spv
%GLSLC% %SRC%\sky.vert     -o %SRC%\sky_vert.spv    && echo OK: sky_vert.spv   2>nul || echo ATTENZIONE: sky.vert non trovato
%GLSLC% %SRC%\sky.frag     -o %SRC%\sky_frag.spv    && echo OK: sky_frag.spv   2>nul || echo ATTENZIONE: sky.frag non trovato
%GLSLC% %SRC%\shaders\terrain_generation.comp -o %SRC%\terrain_generation.spv && echo OK: terrain_generation.spv
%GLSLC% %SRC%\shaders\chunk_culling.comp      -o %SRC%\chunk_culling.spv      && echo OK: chunk_culling.spv

echo.
echo [SHADERS] Copia in bin...
copy /Y %SRC%\vert.spv        %BIN%\vert.spv
copy /Y %SRC%\frag.spv        %BIN%\frag.spv  2>nul
copy /Y %SRC%\forge_vert.spv  %BIN%\forge_vert.spv
copy /Y %SRC%\forge_frag.spv  %BIN%\forge_frag.spv
copy /Y %SRC%\sky_vert.spv    %BIN%\sky_vert.spv  2>nul
copy /Y %SRC%\sky_frag.spv    %BIN%\sky_frag.spv  2>nul
copy /Y %SRC%\terrain_generation.spv %BIN%\terrain_generation.spv  2>nul
copy /Y %SRC%\chunk_culling.spv      %BIN%\chunk_culling.spv       2>nul

echo.
echo [SHADERS] FATTO! Riavvia il gioco per caricare i nuovi shader.
pause
