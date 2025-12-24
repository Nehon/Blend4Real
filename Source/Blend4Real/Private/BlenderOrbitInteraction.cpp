// Created by: nehon

#include "BlenderOrbitInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"

/**
* This class is not used yet as it's an implementation using the Experimental InteractiveToolsFramework (ITF) system
* It will be used eventually in future versions
*/
UBlenderOrbitInteraction::UBlenderOrbitInteraction()
{
	InteractionName = TEXT("BlenderOrbit");
}

bool UBlenderOrbitInteraction::CanBeActivated(const FInputDeviceState& InInputDeviceState) const
{
	// No Alt key required - just check if enabled
	return IsEnabled();
}

FDeviceButtonState UBlenderOrbitInteraction::GetActiveMouseButtonState(const FInputDeviceState& Input)
{
	// Use middle mouse button instead of left
	return Input.Mouse.Middle;
}
