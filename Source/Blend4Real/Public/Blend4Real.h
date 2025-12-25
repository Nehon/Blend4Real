#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FBlend4RealInputProcessor;

class FBlend4RealModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	void RegisterMenus();
	void PluginButtonClicked();
	/** Returns true if Blender Controls are currently enabled */
	bool IsBlend4RealEnabled() const;

	TSharedPtr<FUICommandList> PluginCommands;
	TSharedPtr<FBlend4RealInputProcessor> BlenderInputHandler;

private:
	/** Called when PIE starts - disables the input processor to avoid crashes */
	void OnBeginPIE(bool bIsSimulating);

	/** Called when PIE ends - re-enables the input processor if it was enabled before */
	void OnEndPIE(bool bIsSimulating);

	/** Tracks if the input processor was enabled before PIE started */
	bool bWasEnabledBeforePIE = false;

	/** Delegate handles for PIE events */
	FDelegateHandle BeginPIEDelegateHandle;
	FDelegateHandle EndPIEDelegateHandle;
};
