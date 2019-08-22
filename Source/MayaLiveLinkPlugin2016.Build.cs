// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public abstract class MayaLiveLinkPluginBase : ModuleRules
{
	public MayaLiveLinkPluginBase(ReadOnlyTargetRules Target) : base(Target)
	{
		// For LaunchEngineLoop.cpp include.  You shouldn't need to add anything else to this line.
		PrivateIncludePaths.AddRange(new string[] { "Runtime/Launch/Public", "Runtime/Launch/Private" });

		// Unreal dependency modules
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"ApplicationCore",
			"Projects",
			"UdpMessaging",
			"LiveLinkInterface",
			"LiveLinkMessageBusFramework",
		});


		//
		// Maya SDK setup
		//

		{
			//string MayaVersionString = GetMayaVersion();
			string MayaInstallFolder = GetMayaInstallFolderPath();

			// Make sure this version of Maya is actually installed
			if (Directory.Exists(MayaInstallFolder))
			{
				// These are required for Maya headers to compile properly as a DLL
				PublicDefinitions.Add("NT_PLUGIN=1");
				PublicDefinitions.Add("REQUIRE_IOSTREAM=1");

				string IncludePath = GetMayaIncludePath();
				PrivateIncludePaths.Add(IncludePath);

				if (Target.Platform == UnrealTargetPlatform.Win64)  // @todo: Support other platforms?
				{
					PublicLibraryPaths.Add(GetMayaLibraryPath());

					// Maya libraries we're depending on
					PublicAdditionalLibraries.AddRange(new string[]
						{
							"Foundation.lib",
							"OpenMaya.lib",
							"OpenMayaAnim.lib",
							"OpenMayaUI.lib"}
					);
				}
			}
			//else
			//{
			//	throw new BuildException("Couldn't find Autodesk Maya " + MayaVersionString + " in folder '" + MayaInstallFolder + "'.  This version of Maya must be installed for us to find the Maya SDK files.");
			//}
		}
	}

	public abstract string GetMayaVersion();
	public virtual string GetMayaInstallFolderPath() { return @"C:\Program Files\Autodesk\Maya" + GetMayaVersion(); }
	public virtual string GetMayaIncludePath() { return Path.Combine(GetMayaInstallFolderPath(), "include"); }
	public virtual string GetMayaLibraryPath() { return Path.Combine(GetMayaInstallFolderPath(), "lib"); }
}

public class MayaLiveLinkPlugin2016 : MayaLiveLinkPluginBase
{
	public MayaLiveLinkPlugin2016(ReadOnlyTargetRules Target) : base(Target)
	{
	}

	public override string GetMayaVersion() { return "2016"; }
}
