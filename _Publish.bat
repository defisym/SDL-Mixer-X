Set SDLPath=D:\DEV\SDL
Set FusionPath=C:\Steam\steamapps\common\Clickteam Fusion 2.5
Set ExtPath=D:\DEV\OpenFusionExamples\Extensions\SDL_Mixer\MixerXLib

Set IncludePath=D:\DEV\SDL-Mixer-X\include
Set LibPath=D:\DEV\SDL-Mixer-X\build\build32\lib
Set BinPath=D:\DEV\SDL-Mixer-X\build\build32\bin

echo y | xcopy "%IncludePath%\SDL_mixer.h" "%SDLPath%\include\" /s /e
echo y | xcopy "%IncludePath%\SDL_mixer_ext\SDL_mixer_ext.h" "%SDLPath%\include\SDL_mixer_ext\" /s /e

echo y | xcopy "%LibPath%\SDL2_mixer_ext.lib" "%SDLPath%\lib\x86\" /s /e
echo y | xcopy "%BinPath%\SDL2_mixer_ext.dll" "%SDLPath%\lib\x86\" /s /e

echo y | xcopy "%LibPath%\SDL2_mixer_ext.lib" "%ExtPath%\" /s /e
echo y | xcopy "%BinPath%\SDL2_mixer_ext.dll" "%ExtPath%\" /s /e

echo y | xcopy "%BinPath%\SDL2_mixer_ext.dll" "%FusionPath%\" /s /e
echo y | xcopy "%BinPath%\SDL2_mixer_ext.dll" "%FusionPath%\Data\Runtime\Unicode" /s /e
