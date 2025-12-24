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
};
