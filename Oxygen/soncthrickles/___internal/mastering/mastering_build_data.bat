@echo on

set outputDir=_master_image_template
set msbuildPath="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"


pushd ..\..

rmdir "%outputDir%\bonus\sonic3air_dev\scripts" /s /q
mkdir "%outputDir%\data"


:: Make sure the Release build is up-to-date
%msbuildPath% build/_vstudio/sonic3air.sln /property:Configuration=Release /property:Platform=Win32 -verbosity:minimal

:: Build data packages and meta data
"bin\Release_x86\Sonic3AIR.exe" -pack
move "enginedata.bin" "%outputDir%\data"
move "gamedata.bin" "%outputDir%\data"
move "audiodata.bin" "%outputDir%\data"
move "audioremaster.bin" "%outputDir%\data"
copy "data\metadata.json" "%outputDir%\data" /y

:: Copy scripts
:: TODO: Make sure these scripts are really up-to-date
copy "saves\scripts.bin" "%outputDir%\data"

popd


:: Done
if "%1"=="" pause
