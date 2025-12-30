#pragma once

#include "CoreMinimal.h"
#include "Blend4RealUtils.h"
#include "CollisionQueryParams.h"

class ULineBatchComponent;
class SWindow;
class STextBlock;
class IBlend4RealTransformHandler;

static constexpr uint32 TRANSFORM_BATCH_ID = 14521274;

/**
 * Handles all object transformation operations: translate, rotate, scale
 */
class FTransformController
{
public:
	FTransformController();

	/** Begin a transform operation of the given mode */
	void BeginTransform(ETransformMode Mode);

	/** End the current transform operation */
	void EndTransform(bool bApply);

	/** Returns true if currently transforming */
	bool IsTransforming() const { return bIsTransforming; }

	/** Get the current transform mode */
	ETransformMode GetCurrentMode() const { return CurrentMode; }

	/** Set the constraint axis for the transform */
	void SetAxis(ETransformAxis::Type Axis);

	/** Handle numeric input for precise transforms */
	void HandleNumericInput(const FString& Digit);

	/** Handle backspace to remove last digit */
	void HandleBackspace();

	/** Apply any pending numeric transform */
	void ApplyNumericTransform();

	/** Returns true if currently in numeric input mode */
	bool IsNumericInputMode() const { return bIsNumericInput; }

	/**
	 * Update the transform based on current mouse position
	 * @param MousePosition - Current mouse screen position
	 * @param bInvertSnap - If true, invert the snap state
	 */
	void UpdateFromMouseMove(const FVector2D& MousePosition, bool bInvertSnap);

	/** Reset transform of selected actors for the given mode */
	void ResetTransform(ETransformMode Mode);

private:
	/** Get axis direction vector for the given axis */
	FVector GetAxisVector(ETransformAxis::Type Axis) const;

	/** Compute the transform plane based on current mode and axis */
	FPlane ComputePlane(const FVector& InitialPos);

	

	/** Apply transform to all selected actors */
	void TransformSelectedActors(const FVector& Direction, float Value, bool Snap, bool InvertSnap = false);

	/** Directly set transform components on selected actors */
	void SetDirectTransformToSelectedActors(const FVector* Location = nullptr,
	                                        const FRotator* Rotation = nullptr,
	                                        const FVector* Scale = nullptr);

	/** Apply the internal transform state to actors */
	void ApplyTransform(const FVector& Direction, float Value, bool InvertSnapState = false);

	// Visualization
	void ShowTransformInfo(const FString& Text, const FVector2D& ScreenPosition);
	void HideTransformInfo();
	void UpdateVisualization();
	void ClearVisualization();

	// State
	bool bIsTransforming = false;
	bool bIsNumericInput = false;
	int32 TransactionIndex = -1;
	ETransformMode CurrentMode = ETransformMode::None;
	ETransformAxis::Type CurrentAxis = ETransformAxis::None;
	FString NumericBuffer;
	FTransform TransformPivot;
	FVector DragInitialProjectedPosition = FVector::ZeroVector;
	FVector HitLocation = FVector::ZeroVector;
	FVector TransformViewDir = FVector::ZeroVector;
	float InitialScaleDistance = 0.f;
	FLinearColor OriginalSelectionColor = FLinearColor::Black;
	FCollisionQueryParams IgnoreSelectionQueryParams;

	/** Current transform handler - determines how transforms are applied to selection */
	TSharedPtr<IBlend4RealTransformHandler> TransformHandler;

	// Ray state (updated during GetPlaneHit)
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ZeroVector;

	// Visualization
	TSharedPtr<SWindow> TransformInfoWindow;
	TSharedPtr<STextBlock> TransformInfoText;
	ULineBatchComponent* LineBatcher = nullptr;
};
