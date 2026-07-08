using System;
using System.IO;
using System.Linq;
using UnrealBuildTool;

public class CyclingPowerBLE : ModuleRules
{
	public CyclingPowerBLE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// C++/WinRT relies on exceptions and coroutine-adjacent constructs.
		bEnableExceptions = true;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[]
			{
				"RuntimeObject.lib",
				"windowsapp.lib"
			});

			// The WinRT headers pull in a lot of macros/templates that don't
			// play well with unity builds pulling multiple cpp files together.
			bUseUnity = false;

			// UnrealBuildTool does not add the Windows SDK's C++/WinRT
			// projection headers (winrt/*.h) to the include path by default,
			// so we have to locate and add them ourselves. They live under:
			//   <Windows Kits 10 Include>\<SDK version>\cppwinrt
			string WinSdkIncludeRoot = @"C:\Program Files (x86)\Windows Kits\10\Include";
			string CppWinRTDir = null;

			if (Directory.Exists(WinSdkIncludeRoot))
			{
				CppWinRTDir = Directory.GetDirectories(WinSdkIncludeRoot)
					.OrderByDescending(d => d)
					.Select(d => Path.Combine(d, "cppwinrt"))
					.FirstOrDefault(Directory.Exists);
			}

			if (CppWinRTDir != null)
			{
				PublicSystemIncludePaths.Add(CppWinRTDir);
			}
			else
			{
				Console.WriteLine(
					"CyclingPowerBLE: could not find the C++/WinRT projection headers " +
					"(winrt/*.h) under \"" + WinSdkIncludeRoot + "\\<version>\\cppwinrt\". " +
					"In the Visual Studio Installer, make sure a Windows 10 or 11 SDK is " +
					"installed under 'Desktop development with C++' (it ships the cppwinrt " +
					"headers by default on VS2019 16.x+/VS2022) and retry the build.");
			}
		}
	}
}
