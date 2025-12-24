#pragma once

// Orbit implementation selection (for debug purpose)
#define ORBIT_PAN_IMPLEM_INLINE 1
#define ORBIT_IMPLEM_ITF 0

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Framework/Application/IInputProcessor.h"
#include "EditorViewportClient.h"

class UBlenderOrbitInteraction;
class UViewportOrbitInteraction;
static constexpr uint32 TRANSFORM_BATCH_ID = 14521274;

enum class ETransformMode
{
	None,
	Translation, // 'G' key - Translation
	Rotation, // 'R' key - Rotation
	Scale // 'S' key - Scale
};

enum ETransformAxis
{
	None,
	WorldX,
	WorldY,
	WorldZ,
	LocalX,
	LocalY,
	LocalZ,
	TransformAxes_Count
};

class FBlend4RealInputProcessor : public TSharedFromThis<FBlend4RealInputProcessor>, public IInputProcessor
{
public:
	FBlend4RealInputProcessor();
	virtual ~FBlend4RealInputProcessor() override;

	// IInputProcessor interface
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool
	HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	void ToggleEnabled();
	bool IsEnabled() const { return bIsEnabled; }

private:
	bool bIsEnabled = false;
	bool bIsTransforming = false;
	bool bIsNumericInput = false;
	int TransactionIndex = -1;
	ETransformMode CurrentMode = ETransformMode::None;
	ETransformAxis CurrentAxis = None;
	FString NumericBuffer = TEXT("");
	FVector2D LastMousePosition = FVector2D();
	FTransform TransformPivot = FTransform();
	FVector DragInitialProjectedPosition = FVector();
	FVector HitLocation = FVector();
	FVector TransformViewDir = FVector();
	float InitialScaleDistance = 0.f;
	TMap<uint32_t, FTransform> ActorsTransformMap = {};
	FLinearColor OriginalSelectionColor = FLinearColor::Black;
	TSharedPtr<SWindow> TransformInfoWindow;
	TSharedPtr<STextBlock> TransformInfoText;
	ULineBatchComponent* LineBatcher = nullptr;
	FCollisionQueryParams IgnoreSelectionQueryParams;
	FVector RayOrigin;
	FVector RayDirection;

#if ORBIT_PAN_IMPLEM_INLINE
	bool bIsOrbiting = false;
	bool bIsPanning = false;
	FVector OrbitPivot = FVector();
#endif

	// Transform functions
	void BeginTransform(const ETransformMode Mode);
	void EndTransform(bool bApply = true);
	void ApplyTransform(const FVector& Direction, const float Value, const bool InvertSnapState = false);
	void ApplyNumericTransform();

	// Helper functions
	void SetTransformAxis(const ETransformAxis Axis);
	FVector GetAxisVector(const ETransformAxis Axis) const;
	bool IsTransformKey(const FKeyEvent& KeyEvent);
	bool IsAxisKey(const FKeyEvent& KeyEvent, ETransformAxis& OutAxis);
	bool IsNumericKey(const FKeyEvent& KeyEvent, FString& OutDigit);
	void SetSelectionAsModified();
	void RegisterInputProcessor();
	void UnregisterInputProcessor();
	void TransformSelectedActors(const FVector& Direction, const float Value, const bool Snap = true,
	                             const bool InvertSnap = false);
	void SetDirectTransformToSelectedActor(const FVector* Location = nullptr, const FRotator* Rotation = nullptr,
	                                       const FVector* Scale = nullptr);
	void ResetSelectedActorsTransform(const ETransformMode TransformMode);
	FVector GetPlaneHit(const FVector& Normal, const float Distance);
	FPlane ComputePlane(const FVector& InitialPos);
	void ShowTransformInfo(const FString& Text, const FVector2D& ScreenPosition);
	void HideTransformInfo();
	void UpdateTransformVisualization();
	void ClearTransformVisualization();
	FHitResult ProjectToSurface(const FVector& Start, const FVector& Direction,
	                            const FCollisionQueryParams& Params) const;
	void DuplicateSelectedAndGrab();
	void DeleteSelected();
	FHitResult GetScenePick(const FVector2D MouseEventPosition);
	bool FocusOnMouseHit(const FVector2D MousePosition);
#if ORBIT_IMPLEM_ITF
	// Orbit interaction management
	TWeakObjectPtr<UBlenderOrbitInteraction> BlenderOrbitInteraction;
	TWeakObjectPtr<UViewportOrbitInteraction> OriginalOrbitInteraction;
	void EnableBlenderOrbit();
	void DisableBlenderOrbit();
#endif

#if ORBIT_PAN_IMPLEM_INLINE
	// Orbit functions

	void BeginOrbit(const FVector2D MousePosition);
	void EndOrbit();
	// Pan functions
	void BeginPan();
	void EndPan();
	void OrbitCamera(const FVector2D Delta, FLevelEditorViewportClient* ViewportClient) const;
	void PanCamera(const FVector2D Delta, FLevelEditorViewportClient* ViewportClient) const;
#endif
};
