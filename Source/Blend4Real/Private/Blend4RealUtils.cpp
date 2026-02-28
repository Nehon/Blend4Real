// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "Blend4RealUtils.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "PlatformInputsUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Selection.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"


/** Segment-triangle intersection test. Adapted from MeshPaint's LegacySegmentTriangleIntersection. */
static bool SegmentTriangleIntersection(const FVector& Start, const FVector& End,
	const FVector& A, const FVector& B, const FVector& C,
	FVector& OutIntersectPoint, FVector& OutTriangleNormal)
{
	const FVector BA = A - B;
	const FVector CB = B - C;
	const FVector TriNormal = BA ^ CB;
	if (FMath::IsNearlyZero(TriNormal.SizeSquared()))
	{
		return false;
	}

	if (!FMath::SegmentPlaneIntersection(Start, End, FPlane(A, TriNormal), OutIntersectPoint))
	{
		return false;
	}

	const FVector BaryCentric = FMath::ComputeBaryCentric2D(OutIntersectPoint, A, B, C);
	if (BaryCentric.X > 0.f && BaryCentric.Y > 0.f && BaryCentric.Z > 0.f)
	{
		OutTriangleNormal = TriNormal.GetSafeNormal();
		return true;
	}
	return false;
}

namespace Blend4RealUtils
{
	// Custom pivot override state
	static bool bHasCustomPivot = false;
	static FVector CustomPivotLocation = FVector::ZeroVector;

	// Forward declaration
	FEditorViewportClient* GetViewportClientAtPosition(const FVector2D& ScreenPosition,
	                                                   const FName& ViewportTypeFilter = NAME_None);

	// Helper to check if a widget type string matches any editor viewport pattern
	bool IsEditorViewportType(const FString& TypeString)
	{
		return TypeString.Contains(TEXT("EditorViewport"))
			|| TypeString.Contains(TEXT("PreviewViewport"))
			|| TypeString.Contains(TEXT("SystemViewport"))
			|| TypeString == TEXT("SLevelViewport")
			|| TypeString == TEXT("SSCSEditorViewport");
	}

	const FColor AxisColors[ETransformAxis::TransformAxes_Count] = {
		FColor::Black, FColor::Red, FColor::Green, FColor::Blue, FColor::Red, FColor::Green, FColor::Blue
	};

	const char* AxisLabels[ETransformAxis::TransformAxes_Count] = {
		"None", "X", "Y", "Z", "Local X", "Local Y", "Local Z", "X Plane", "Y Plane", "Z Plane", "Local X Plane",
		"Local Y Plane", "Local Z Plane",
	};

	UWorld* GetEditorWorld()
	{
		if (!GEditor || !GEditor->GetActiveViewport())
		{
			return nullptr;
		}
		return GEditor->GetActiveViewport()->GetClient()->GetWorld();
	}

	FSceneView* GetActiveSceneView(FEditorViewportClient* EClient)
	{
		if (!EClient)
		{
			EClient = GetFocusedViewportClient();
			if (!EClient)
			{
				return nullptr;
			}
		}

		FSceneViewFamily ViewFamily = FSceneViewFamily::ConstructionValues(
			EClient->Viewport, EClient->GetScene(), EClient->EngineShowFlags);
		return EClient->CalcSceneView(&ViewFamily);
	}

	void SetCustomPivot(const FVector& Location)
	{
		bHasCustomPivot = true;
		CustomPivotLocation = Location;

		// Sync with Unreal's gizmo pivot location so that
		// "Pivot > Set as Pivot Offset" context menu works
		GLevelEditorModeTools().SetPivotLocation(Location, false);
	}

	void ClearCustomPivot()
	{
		bHasCustomPivot = false;
		CustomPivotLocation = FVector::ZeroVector;
	}

	bool HasCustomPivot()
	{
		return bHasCustomPivot;
	}

	FVector GetCustomPivot()
	{
		return CustomPivotLocation;
	}

	FTransform ComputeSelectionPivot()
	{
		// If custom pivot is set, use it directly
		if (bHasCustomPivot)
		{
			FTransform Transform;
			Transform.SetLocation(CustomPivotLocation);
			return Transform;
		}

		if (!GEditor)
		{
			return FTransform();
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		FVector Center(0.0);
		int Count = 0;
		FTransform Transform = FTransform();

		if (SelectedActors->Num() > 0)
		{
			for (FSelectionIterator It(*SelectedActors); It; ++It)
			{
				if (const AActor* Actor = Cast<AActor>(*It))
				{
					// Get the actor's world transform
					const FTransform ActorTransform = Actor->GetActorTransform();

					// Get the pivot offset (local space) and transform to world space
					// This matches how Unreal's editor gizmo computes the pivot point
					const FVector PivotOffset = Actor->GetPivotOffset();
					const FVector PivotWorldPosition = ActorTransform.TransformPosition(PivotOffset);

					Center += PivotWorldPosition;
					Count++;
				}
			}
		}
		else
		{
			// No selected actors, we try to find selected components
			// Components don't have pivot offsets, so just use their location
			USelection* SelectedComponents = GEditor->GetSelectedComponents();
			for (FSelectionIterator It(*SelectedComponents); It; ++It)
			{
				if (const USceneComponent* Component = Cast<USceneComponent>(*It))
				{
					Center += Component->GetComponentLocation();
					Count++;
				}
			}
		}

		if (Count > 0)
		{
			Center /= Count;
		}
		Transform.SetLocation(Center);
		return Transform;
	}

	FHitResult ScenePickAtPosition(const FVector2D& MousePosition, FVector& OutRayOrigin, FVector& OutRayDirection)
	{
		// Get the viewport client and its screen origin
		FVector2D ViewportScreenOrigin;
		FEditorViewportClient* EClient = GetViewportClientAndScreenOrigin(MousePosition, ViewportScreenOrigin);
		if (EClient == nullptr)
		{
			return FHitResult();
		}

		FViewport* Viewport = EClient->Viewport;
		if (!Viewport)
		{
			return FHitResult();
		}

		FSceneViewFamily ViewFamily = FSceneViewFamily::ConstructionValues(
			Viewport, EClient->GetScene(), EClient->EngineShowFlags);

		const FSceneView* Scene = EClient->CalcSceneView(&ViewFamily);
		if (!Scene)
		{
			return FHitResult();
		}
		// Convert screen position to viewport-local coordinates using the widget's screen origin
		const FVector2D LocalMousePos = MousePosition - ViewportScreenOrigin;

		Scene->DeprojectFVector2D(LocalMousePos, OutRayOrigin, OutRayDirection);

		// Step 1: Check hit proxy to see what's RENDERED under the cursor.
		// This catches skeletal meshes that have no collision geometry.
		const int32 HitX = FMath::FloorToInt(LocalMousePos.X);
		const int32 HitY = FMath::FloorToInt(LocalMousePos.Y);

		HHitProxy* HitProxy = Viewport->GetHitProxy(HitX, HitY);

		if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
		{
			const HActor* ActorProxy = static_cast<HActor*>(HitProxy);

			// Step 2: If the rendered component is a skeletal mesh, use LineTraceComponent
			// for precise surface pick (skeletal meshes often lack collision geometry)
			if (ActorProxy->PrimComponent && ActorProxy->PrimComponent->IsA<USkeletalMeshComponent>())
			{
				AActor* HitActor = ActorProxy->Actor.Get();
				USkeletalMeshComponent* SkelComp = HitActor
					? HitActor->FindComponentByClass<USkeletalMeshComponent>()
					: nullptr;

				if (SkelComp)
				{
					const FVector TraceEnd = OutRayOrigin + OutRayDirection * 1000000.f;
					FHitResult CompHitResult;
					if (SkelComp->LineTraceComponent(
							CompHitResult, OutRayOrigin, TraceEnd, FCollisionQueryParams::DefaultQueryParam))
					{
						CompHitResult.bBlockingHit = true;
						return CompHitResult;
					}

					// LineTraceComponent failed (no collision geometry) — CPU skinned triangle intersection
					USkeletalMesh* SkelMesh = SkelComp->GetSkeletalMeshAsset();
					if (SkelMesh)
					{
						FSkeletalMeshRenderData* RenderData = SkelMesh->GetResourceForRendering();
						if (RenderData && RenderData->LODRenderData.Num() > 0)
						{
							const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];

							// Get current posed vertex positions (component space)
							TArray<FMatrix44f> RefToLocals;
							SkelComp->GetCurrentRefToLocalMatrices(RefToLocals, 0);
							TArray<FVector3f> SkinnedPositions;
							USkinnedMeshComponent::ComputeSkinnedPositions(
								SkelComp, SkinnedPositions, RefToLocals,
								LODData, *LODData.GetSkinWeightVertexBuffer());

							// Get index buffer
							TArray<uint32> Indices;
							LODData.MultiSizeIndexContainer.GetIndexBuffer(Indices);

							// Transform ray to component local space
							const FTransform& CompTransform = SkelComp->GetComponentTransform();
							const FTransform InvCompTransform = CompTransform.Inverse();
							const FVector LocalStart = InvCompTransform.TransformPosition(OutRayOrigin);
							const FVector LocalEnd = InvCompTransform.TransformPosition(TraceEnd);

							// Ray-triangle intersection over all triangles
							float MinDistance = FLT_MAX;
							FVector ClosestIntersect = FVector::ZeroVector;
							FVector ClosestNormal = FVector::UpVector;
							const int32 NumTriangles = Indices.Num() / 3;

							for (int32 Tri = 0; Tri < NumTriangles; ++Tri)
							{
								const FVector P0 = FVector(SkinnedPositions[Indices[Tri * 3 + 0]]);
								const FVector P1 = FVector(SkinnedPositions[Indices[Tri * 3 + 1]]);
								const FVector P2 = FVector(SkinnedPositions[Indices[Tri * 3 + 2]]);

								FVector IntersectPoint, HitNormal;
								if (SegmentTriangleIntersection(LocalStart, LocalEnd, P0, P1, P2,
										IntersectPoint, HitNormal))
								{
									const float Dist = FVector::DistSquared(LocalStart, IntersectPoint);
									if (Dist < MinDistance)
									{
										MinDistance = Dist;
										ClosestIntersect = IntersectPoint;
										ClosestNormal = HitNormal;
									}
								}
							}

							if (MinDistance != FLT_MAX)
							{
								FHitResult SkinHit;
								SkinHit.bBlockingHit = true;
								SkinHit.ImpactPoint = CompTransform.TransformPosition(ClosestIntersect);
								SkinHit.Location = SkinHit.ImpactPoint;
								SkinHit.ImpactNormal = CompTransform.TransformVectorNoScale(ClosestNormal).GetSafeNormal();
								SkinHit.Distance = FVector::Dist(OutRayOrigin, SkinHit.ImpactPoint);
								return SkinHit;
							}
						}
					}

					// Final fallback: bounding box intersection
					const FBox Box = SkelComp->Bounds.GetBox();
					FVector HitLocation, HitNormal;
					float HitTime;
					if (FMath::LineExtentBoxIntersection(
							Box, OutRayOrigin, TraceEnd, FVector::ZeroVector,
							HitLocation, HitNormal, HitTime))
					{
						FHitResult BoundsHit;
						BoundsHit.bBlockingHit = true;
						BoundsHit.Location = HitLocation;
						BoundsHit.ImpactPoint = HitLocation;
						BoundsHit.ImpactNormal = HitNormal;
						BoundsHit.Distance = FVector::Dist(OutRayOrigin, HitLocation);
						return BoundsHit;
					}
				}
			}
		}

		// Step 3: Fall back to regular ECC_Visibility collision trace
		FCollisionQueryParams Params;
		Params.bTraceComplex = true;

		return ProjectToSurface(EClient->GetWorld(), OutRayOrigin, OutRayDirection, Params);
	}

	FHitResult ProjectToSurface(const UWorld* World, const FVector& Start, const FVector& Direction,
	                            const FCollisionQueryParams& Params)
	{
		FHitResult HitResult;
		if (!World)
		{
			return HitResult;
		}

		const FVector End = Start + Direction * 1000000.f;
		World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);
		return HitResult;
	}

	bool IsTransformKey(const FKeyEvent& KeyEvent)
	{
		const FKey Key = KeyEvent.GetKey();
		return Key == EKeys::G || Key == EKeys::R || Key == EKeys::S;
	}

	bool IsAxisKey(const FKeyEvent& KeyEvent, const EModifierKey::Type Modifiers, ETransformAxis::Type& OutAxis)
	{
		const FKey Key = KeyEvent.GetKey();
		const bool bShift = Modifiers == EModifierKey::Shift;

		if (Key == EKeys::X)
		{
			OutAxis = bShift ? ETransformAxis::WorldXPlane : ETransformAxis::WorldX;
			return true;
		}
		if (Key == EKeys::Y)
		{
			OutAxis = bShift ? ETransformAxis::WorldYPlane : ETransformAxis::WorldY;
			return true;
		}
		if (Key == EKeys::Z)
		{
			OutAxis = bShift ? ETransformAxis::WorldZPlane : ETransformAxis::WorldZ;
			return true;
		}

		return false;
	}


	bool IsNumericKey(const FKeyEvent& KeyEvent, FString& OutDigit)
	{
		const FKey Key = KeyEvent.GetKey();
		const TCHAR Character = PlatformInputs::TranslateKeyWithModifiers(KeyEvent);

		if (Key == EKeys::Zero || Key == EKeys::NumPadZero || Character == '0')
		{
			OutDigit = TEXT("0");
			return true;
		}
		if (Key == EKeys::One || Key == EKeys::NumPadOne || Character == '1')
		{
			OutDigit = TEXT("1");
			return true;
		}
		if (Key == EKeys::Two || Key == EKeys::NumPadTwo || Character == '2')
		{
			OutDigit = TEXT("2");
			return true;
		}
		if (Key == EKeys::Three || Key == EKeys::NumPadThree || Character == '3')
		{
			OutDigit = TEXT("3");
			return true;
		}
		if (Key == EKeys::Four || Key == EKeys::NumPadFour || Character == '4')
		{
			OutDigit = TEXT("4");
			return true;
		}
		if (Key == EKeys::Five || Key == EKeys::NumPadFive || Character == '5')
		{
			OutDigit = TEXT("5");
			return true;
		}
		if (Key == EKeys::Six || Key == EKeys::NumPadSix || Character == '6')
		{
			OutDigit = TEXT("6");
			return true;
		}
		if (Key == EKeys::Seven || Key == EKeys::NumPadSeven || Character == '7')
		{
			OutDigit = TEXT("7");
			return true;
		}
		if (Key == EKeys::Eight || Key == EKeys::NumPadEight || Character == '8')
		{
			OutDigit = TEXT("8");
			return true;
		}
		if (Key == EKeys::Nine || Key == EKeys::NumPadNine || Character == '9')
		{
			OutDigit = TEXT("9");
			return true;
		}
		if (Key == EKeys::Period || Key == EKeys::Decimal)
		{
			OutDigit = TEXT(".");
			return true;
		}
		if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
		{
			OutDigit = TEXT("-");
			return true;
		}
		return false;
	}

	void MarkSelectionModified()
	{
		if (!GEditor)
		{
			return;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				Actor->Modify();
			}
		}
	}

	bool IsEditorViewportWidgetFocused()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}

		const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!FocusedWidget.IsValid())
		{
			return false;
		}

		// Walk up the widget hierarchy to check if an editor viewport is in the chain
		TSharedPtr<SWidget> CurrentWidget = FocusedWidget;
		while (CurrentWidget.IsValid())
		{
			const FString TypeString = CurrentWidget->GetType().ToString();
			if (IsEditorViewportType(TypeString))
			{
				return true;
			}
			CurrentWidget = CurrentWidget->GetParentWidget();
		}

		return false;
	}

	FEditorViewportClient* GetViewportClientAndScreenOrigin(const FVector2D& ScreenPosition,
	                                                        FVector2D& OutViewportScreenOrigin,
	                                                        const FName& ViewportTypeFilter)
	{
		OutViewportScreenOrigin = FVector2D::ZeroVector;

		if (!FSlateApplication::IsInitialized())
		{
			return nullptr;
		}

		// Get all visible windows
		TArray<TSharedRef<SWindow>> VisibleWindows;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(VisibleWindows);

		// Use LocateWindowUnderMouse to find the widget path under cursor
		FWidgetPath PathUnderCursor = FSlateApplication::Get().LocateWindowUnderMouse(
			ScreenPosition,
			VisibleWindows,
			true
		);

		// First pass: check if there's an editor viewport in the widget path
		// Only SEditorViewport and its subclasses have FEditorViewportClient
		// Plain SViewport (e.g., content browser thumbnails) do NOT have FEditorViewportClient
		// If a filter is specified, only match that specific viewport type
		bool bHasEditorViewportParent = false;
		for (int32 i = PathUnderCursor.Widgets.Num() - 1; i >= 0; --i)
		{
			const TSharedRef<SWidget>& Widget = PathUnderCursor.Widgets[i].Widget;
			const FName WidgetType = Widget->GetType();
			const FString TypeString = WidgetType.ToString();

			// If filter specified, check for exact match
			if (!ViewportTypeFilter.IsNone())
			{
				if (WidgetType == ViewportTypeFilter)
				{
					bHasEditorViewportParent = true;
					break;
				}
			}
			// Otherwise check for any editor viewport type
			else if (IsEditorViewportType(TypeString))
			{
				bHasEditorViewportParent = true;
				break;
			}
		}

		if (!bHasEditorViewportParent)
		{
			// No matching editor viewport in path
			return nullptr;
		}

		// Second pass: find the SViewport and extract the client
		for (int32 i = PathUnderCursor.Widgets.Num() - 1; i >= 0; --i)
		{
			const FArrangedWidget& ArrangedWidget = PathUnderCursor.Widgets[i];
			const TSharedRef<SWidget>& Widget = ArrangedWidget.Widget;
			const FName WidgetType = Widget->GetType();

			if (WidgetType == FName("SViewport"))
			{
				// Found SViewport - get its viewport interface
				const TSharedRef<SViewport> ViewportWidget = StaticCastSharedRef<SViewport>(Widget);
				TSharedPtr<ISlateViewport> ViewportInterface = ViewportWidget->GetViewportInterface().Pin();

				if (ViewportInterface.IsValid())
				{
					// FSceneViewport implements ISlateViewport and inherits from FViewport
					// Cast is safe because we verified an editor viewport parent exists
					FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(ViewportInterface.Get());
					if (SceneViewport)
					{
						FViewportClient* Client = SceneViewport->GetClient();
						if (Client)
						{
							// Get the viewport's screen position from the widget geometry
							OutViewportScreenOrigin = ArrangedWidget.Geometry.GetAbsolutePosition();
							return static_cast<FEditorViewportClient*>(Client);
						}
					}
				}
			}
		}

		// No viewport found at this position
		return nullptr;
	}

	FEditorViewportClient* GetViewportClientAtPosition(const FVector2D& ScreenPosition, const FName& ViewportTypeFilter)
	{
		FVector2D Unused;
		return GetViewportClientAndScreenOrigin(ScreenPosition, Unused, ViewportTypeFilter);
	}

	FEditorViewportClient* GetFocusedViewportClient()
	{
		// Get the viewport under the current cursor position
		if (FSlateApplication::IsInitialized())
		{
			const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
			FEditorViewportClient* ViewportClient = GetViewportClientAtPosition(CursorPos);
			if (ViewportClient)
			{
				return ViewportClient;
			}
		}

		// Fallback to GEditor's active viewport
		if (GEditor && GEditor->GetActiveViewport())
		{
			return static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
		}
		return nullptr;
	}

	bool IsLevelEditorViewportFocused()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}

		TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!FocusedWidget.IsValid())
		{
			return false;
		}

		// Walk up the widget hierarchy to find an SLevelViewport
		TSharedPtr<SWidget> CurrentWidget = FocusedWidget;
		while (CurrentWidget.IsValid())
		{
			const FName WidgetType = CurrentWidget->GetType();
			if (WidgetType == FName("SLevelViewport"))
			{
				return true;
			}
			CurrentWidget = CurrentWidget->GetParentWidget();
		}

		return false;
	}


	bool IsSCSEditorViewportFocused()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}

		TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!FocusedWidget.IsValid())
		{
			return false;
		}

		// Walk up the widget hierarchy to find an SSCSEditorViewport
		TSharedPtr<SWidget> CurrentWidget = FocusedWidget;
		while (CurrentWidget.IsValid())
		{
			const FName WidgetType = CurrentWidget->GetType();
			if (WidgetType == FName("SSCSEditorViewport"))
			{
				return true;
			}
			CurrentWidget = CurrentWidget->GetParentWidget();
		}

		return false;
	}

	bool IsMouseOverViewport(const FVector2D& MousePosition, const FName& ViewportTypeFilter)
	{
		return GetViewportClientAtPosition(MousePosition, ViewportTypeFilter) != nullptr;
	}


	FVector GetPlaneHit(const FVector& Normal, const float Distance, FVector& RayOrigin, FVector& RayDirection)
	{
		FEditorViewportClient* EClient = GetFocusedViewportClient();
		if (!EClient)
		{
			return FVector::ZeroVector;
		}
		const FSceneView* Scene = GetActiveSceneView(EClient);
		if (!Scene)
		{
			return FVector::ZeroVector;
		}
		FIntPoint MousePos;
		EClient->Viewport->GetMousePos(MousePos);
		Scene->DeprojectFVector2D(MousePos, RayOrigin, RayDirection);

		const FPlane Plane(Normal.X, Normal.Y, Normal.Z, Distance);
		return FMath::RayPlaneIntersection(RayOrigin, RayDirection, Plane);
	}
}
