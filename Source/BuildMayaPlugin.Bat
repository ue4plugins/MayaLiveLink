@echo off

RMDIR %~dp0Staging /S /Q
..\..\..\..\Build\BatchFiles\RunUAT.bat  BuildGraph -Script=Engine/Source/Programs/NotForLicensees/MayaLiveLinkPlugin/BuildMayaPlugin.xml -Target="Stage Maya Plugin Module"