// Copyright Epic Games, Inc. All Rights Reserved.

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

				string MayaIncludePath = GetMayaIncludePath();
				PrivateIncludePaths.Add(MayaIncludePath);

				string UfeIncludePath = GetUfeIncludePath();
				if (Directory.Exists(UfeIncludePath))
				{
					PrivateIncludePaths.Add(UfeIncludePath);
				}

				if (Target.Platform == UnrealTargetPlatform.Win64)  // @todo: Support other platforms?
				{
					string MayaLibDir = GetMayaLibraryPath();

					// Maya libraries we're depending on
					string[] MayaLibs = new string[]
					{
						"Foundation.lib",
						"OpenMaya.lib",
						"OpenMayaAnim.lib",
						"OpenMayaUI.lib"
					};
					foreach(string MayaLib in MayaLibs)
					{
						 PublicAdditionalLibraries.Add(Path.Combine(MayaLibDir, MayaLib));
					}

					string UfeLibDir = GetUfeLibraryPath();

					if (Directory.Exists(UfeLibDir))
					{
						// UFE libraries we're depending on
						string[] UfeLibs = new string[]
						{
							"ufe_2.lib"
						};
						foreach (string UfeLib in UfeLibs)
						{
							PublicAdditionalLibraries.Add(Path.Combine(UfeLibDir, UfeLib));
						}
					}
				}
			}
			//else
			//{
			//	throw new BuildException("Couldn't find Autodesk Maya " + MayaVersionString + " in folder '" + MayaInstallFolder + "'.  This version of Maya must be installed for us to find the Maya SDK files.");
			//}
		}
	}

	public abstract string GetMayaVersion();
	public virtual string GetMayaInstallFolderPath()
	{
		// Try with standard setup
		string Location = @"C:\Program Files\Autodesk\Maya" + GetMayaVersion();
		if (!Directory.Exists(Location))
		{
			// Try with build machine setup
			string SDKRootEnvVar = System.Environment.GetEnvironmentVariable("UE_SDKS_ROOT");
			if (SDKRootEnvVar != null && SDKRootEnvVar != "")
			{
				Location = Path.Combine(SDKRootEnvVar, "HostWin64", "Win64", "Maya", GetMayaVersion());
			}
		}

		return Location;
	}
	public virtual string GetMayaIncludePath() { return Path.Combine(GetMayaInstallFolderPath(), "include"); }
	public virtual string GetMayaLibraryPath() { return Path.Combine(GetMayaInstallFolderPath(), "lib"); }
	public virtual string GetUfeIncludePath() { return Path.Combine(GetMayaInstallFolderPath(), "devkit", "ufe", "include"); }
	public virtual string GetUfeLibraryPath() { return Path.Combine(GetMayaInstallFolderPath(), "devkit", "ufe", "lib"); }
}

public class MayaLiveLinkPlugin2016 : MayaLiveLinkPluginBase
{
	public MayaLiveLinkPlugin2016(ReadOnlyTargetRules Target) : base(Target)
	{
	}

	public override string GetMayaVersion() { return "2016"; }
}
