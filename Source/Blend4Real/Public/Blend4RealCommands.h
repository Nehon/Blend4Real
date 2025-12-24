#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Blend4RealStyle.h"

class FBlend4RealCommands : public TCommands<FBlend4RealCommands>
{
public:
	FBlend4RealCommands()
		: TCommands<FBlend4RealCommands>(
			TEXT("Blend4Real"),
			NSLOCTEXT("Contexts", "Blend4Real", "Blend4Real Plugin"),
			NAME_None,
			FBlend4RealStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> PluginAction;
};
