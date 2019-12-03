// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public abstract class MayaLiveLinkPluginTargetBase : TargetRules
{
	public MayaLiveLinkPluginTargetBase(TargetInfo Target, string InMayaVersionString) : base(Target)
	{
		Type = TargetType.Program;

		bShouldCompileAsDLL = true;
		LinkType = TargetLinkType.Monolithic;
		SolutionDirectory = "Programs/LiveLink";

		// We only need minimal use of the engine for this plugin
		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bCompileICU = false;
		bHasExports = true;

		bBuildInSolutionByDefault = false;

		ExeBinariesSubFolder = "NotForLicensees/Maya/" + InMayaVersionString;
		this.LaunchModuleName = "MayaLiveLinkPlugin" + InMayaVersionString;

		// Add a post-build step that copies the output to a file with the .mll extension
		string OutputName = LaunchModuleName;
		if (Target.Configuration != UnrealTargetConfiguration.Development)
		{
			OutputName = string.Format("{0}-{1}-{2}", OutputName, Target.Platform, Target.Configuration);
		}

		string BinaryFilePath = "$(EngineDir)\\Binaries\\Win64\\NotForLicensees\\Maya\\" + InMayaVersionString + "\\" + OutputName;
		string Command = "copy /Y " + BinaryFilePath + ".dll " + BinaryFilePath + ".mll";
		PostBuildSteps.Add(Command);
	}
}

public class MayaLiveLinkPlugin2016Target : MayaLiveLinkPluginTargetBase
{
	public MayaLiveLinkPlugin2016Target(TargetInfo Target) : base(Target, "2016")
	{
	}
}