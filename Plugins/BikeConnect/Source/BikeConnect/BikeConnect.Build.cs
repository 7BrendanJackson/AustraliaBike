using UnrealBuildTool;

public class BikeConnect : ModuleRules
{
	public BikeConnect(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true; // WRL::ComPtr / HRESULT helpers used by the WinRT backend rely on this

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("WITH_BIKECONNECT_WINRT=1");

			// These are all part of the Windows 10/11 SDK that ships with Visual Studio -
			// nothing external needs to be downloaded to use them.
			PublicSystemLibraries.AddRange(new string[]
			{
				"RuntimeObject.lib",
				"Bluetoothapis.lib"
			});
		}
		else
		{
			PublicDefinitions.Add("WITH_BIKECONNECT_WINRT=0");
		}
	}
}
