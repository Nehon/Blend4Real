#include "Blend4RealCommands.h"

#define LOCTEXT_NAMESPACE "FBlend4RealModule"

void FBlend4RealCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "Blend4Real", "Toggle Blender controls for Unreal Engine Editor",
	           EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
