// Created by: nehon

#pragma once

#include "CoreMinimal.h"
#include "ViewportInteractions/ViewportOrbitInteraction.h"
#include "BlenderOrbitInteraction.generated.h"

/**
 * Blender-style orbit interaction triggered by middle mouse button (no Alt key required)
 * This class is not used yet as it's an implementation using the Experimental InteractiveToolsFramework (ITF) system
 * It will be used eventually in future versions
 */
UCLASS(Transient)
class UBlenderOrbitInteraction : public UViewportOrbitInteraction
{
	GENERATED_BODY()

public:
	UBlenderOrbitInteraction();

	//~ Begin UViewportDragInteraction
	virtual bool CanBeActivated(const FInputDeviceState& InInputDeviceState = FInputDeviceState()) const override;
	//~ End UViewportDragInteraction

protected:
	//~ Begin UViewportDragInteraction
	virtual FDeviceButtonState GetActiveMouseButtonState(const FInputDeviceState& Input) override;
	//~ End UViewportDragInteraction
};
