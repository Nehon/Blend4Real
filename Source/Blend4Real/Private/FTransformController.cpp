#include "FTransformController.h"
#include "Blend4RealUtils.h"
#include "IBlend4RealTransformHandler.h"
#include "FTransformHandlerFactory.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Components/LineBatchComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

using namespace Blend4RealUtils;

FTransformController::FTransformController()
{
}

void FTransformController::BeginTransform(const ETransformMode Mode)
{
	if (!GEditor || bIsTransforming)
	{
		return;
	}

	// Get appropriate handler for current viewport context
	TransformHandler = FTransformHandlerFactory::CreateHandler();
	if (!TransformHandler || !TransformHandler->HasSelection())
	{
		TransformHandler.Reset();
		return;
	}

	// Save and change selection outline color
	if (GEngine)
	{
		OriginalSelectionColor = GEngine->GetSelectionOutlineColor();
		GEngine->SetSelectionOutlineColor(FLinearColor(1.0f, 1.0f, 1.0f));
	}

	bIsTransforming = true;
	CurrentMode = Mode;
	CurrentAxis = ETransformAxis::None;
	bIsNumericInput = false;
	NumericBuffer.Empty();

	// Get the mode description text
	FString ModeText;
	switch (Mode)
	{
	case ETransformMode::Translation:
		ModeText = TEXT("Move");
		break;
	case ETransformMode::Rotation:
		ModeText = TEXT("Rotate");
		break;
	case ETransformMode::Scale:
		ModeText = TEXT("Scale");
		break;
	default:
		ModeText = TEXT("Transform");
		break;
	}

	// Begin transaction and capture initial state
	TransactionIndex = TransformHandler->BeginTransaction(FText::FromString(ModeText));
	TransformHandler->CaptureInitialState();

	// Compute pivot and initial picking state
	TransformPivot = TransformHandler->ComputeSelectionPivot();

	const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
	DragInitialProjectedPosition = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin,
	                                           RayDirection);

	HitLocation = DragInitialProjectedPosition;
	InitialScaleDistance = (DragInitialProjectedPosition - TransformPivot.GetLocation()).Length();

	// Set up collision query params to ignore selected actors (for surface snapping)
	IgnoreSelectionQueryParams.bTraceComplex = true;
	IgnoreSelectionQueryParams.ClearIgnoredSourceObjects();

	// Add selected actors to ignore list (only works for actor handler, but harmless for others)
	if (GEditor)
	{
		// actors
		USelection* SelectedActors = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (const AActor* Actor = Cast<AActor>(*It))
			{
				IgnoreSelectionQueryParams.AddIgnoredSourceObject(Actor);
			}
		}
		// // components
		USelection* SelectedComponents = GEditor->GetSelectedComponents();
		for (FSelectionIterator It(*SelectedComponents); It; ++It)
		{
			if (const UActorComponent* ActorComponent = Cast<UActorComponent>(*It))
			{
				UE_LOG(LogTemp, Display, TEXT("Selected Component: %s"), *ActorComponent->GetName());
				IgnoreSelectionQueryParams.AddIgnoredSourceObject(ActorComponent);
			}
		}
	}
}

void FTransformController::EndTransform(const bool bApply)
{
	if (!bIsTransforming || !TransformHandler)
	{
		return;
	}

	HideTransformInfo();

	// Restore original selection outline color
	if (GEngine)
	{
		GEngine->SetSelectionOutlineColor(OriginalSelectionColor);
	}

	if (!bApply)
	{
		// Restore original transforms and cancel transaction
		TransformHandler->RestoreInitialState();
		TransformHandler->CancelTransaction(TransactionIndex);
	}
	else
	{
		TransformHandler->EndTransaction();
	}

	TransactionIndex = -1;
	TransformHandler.Reset();
	bIsTransforming = false;
	CurrentMode = ETransformMode::None;
	CurrentAxis = ETransformAxis::None;
	bIsNumericInput = false;
	NumericBuffer.Empty();

	ClearVisualization();
}

void FTransformController::SetAxis(ETransformAxis::Type Axis)
{
	if (CurrentAxis == Axis || CurrentMode == ETransformMode::Scale)
	{
		// Toggle to local axis; Scale is always forced in local space
		CurrentAxis = static_cast<ETransformAxis::Type>(Axis + 3);
	}
	else
	{
		CurrentAxis = Axis;
	}
	const FString AxisText = AxisLabels[CurrentAxis];

	// Recompute plane hit for new axis
	const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
	DragInitialProjectedPosition = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin,
	                                           RayDirection);
	TransformSelectedActors(FVector(0.0), 0, false);

	UpdateVisualization();
}

void FTransformController::HandleNumericInput(const FString& Digit)
{
	if (CurrentAxis == ETransformAxis::None)
	{
		// Force numerical transform on X if no axis has been defined
		CurrentAxis = ETransformAxis::WorldX;
	}
	bIsNumericInput = true;
	NumericBuffer.Append(Digit);
	ApplyNumericTransform();
}

void FTransformController::HandleBackspace()
{
	if (!NumericBuffer.IsEmpty())
	{
		NumericBuffer.RemoveAt(NumericBuffer.Len() - 1);
		ApplyNumericTransform();
	}
}

void FTransformController::ApplyNumericTransform()
{
	if (!bIsTransforming)
	{
		return;
	}

	const FVector AxisVector = GetAxisVector(CurrentAxis);
	if (NumericBuffer.IsEmpty())
	{
		TransformSelectedActors(AxisVector, 0, false);
		const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
		ShowTransformInfo(FString::Printf(TEXT("0.0")), CursorPos);
		UpdateVisualization();
		return;
	}

	const float NumericValue = FCString::Atof(*NumericBuffer);
	TransformSelectedActors(AxisVector, CurrentAxis == ETransformAxis::WorldZ ? -NumericValue : NumericValue, false);

	UpdateVisualization();
}

void FTransformController::UpdateFromMouseMove(const FVector2D& MousePosition, bool bInvertSnap)
{
	if (!bIsTransforming || bIsNumericInput)
	{
		return;
	}

	const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
	HitLocation = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin, RayDirection);

	const FVector AxisVector = GetAxisVector(CurrentAxis);

	if (CurrentMode == ETransformMode::Rotation)
	{
		const FVector Dir = (TransformPivot.GetLocation() - HitLocation).GetSafeNormal();
		const FVector OriginalDir = (TransformPivot.GetLocation() - DragInitialProjectedPosition).GetSafeNormal();

		const float X = FVector::DotProduct(Dir, OriginalDir);
		const FVector Normal = TransformViewDir * (signbit(TransformViewDir.Dot(AxisVector)) == 1 ? -1.0 : 1.0);
		const FVector Tangent = FVector::CrossProduct(OriginalDir, Normal);
		const float Y = FVector::DotProduct(Dir, Tangent);
		const float Angle = FMath::RadiansToDegrees(atan2f(Y, X));
		ApplyTransform(AxisVector, Angle, bInvertSnap);
		UpdateVisualization();
		return;
	}

	if (CurrentMode == ETransformMode::Scale)
	{
		const ETransformAxis::Type Axis = static_cast<ETransformAxis::Type>(CurrentAxis >= ETransformAxis::LocalX
			                                                                    ? CurrentAxis - 3
			                                                                    : CurrentAxis);
		const float NewDistance = (TransformPivot.GetLocation() - HitLocation).Length();
		if (InitialScaleDistance < 0.001)
		{
			return;
		}
		const float Scale = NewDistance / InitialScaleDistance;
		ApplyTransform(GetAxisVector(Axis), Scale, bInvertSnap);
		UpdateVisualization();
		return;
	}

	// Translation
	if (CurrentAxis == ETransformAxis::None || CurrentAxis >= ETransformAxis::WorldXPlane)
	{
		const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
		const bool Project = ViewportSettings->SnapToSurface.bEnabled && TransformHandler && TransformHandler->
			GetSelectionCount() == 1;

		if (Project)
		{
			const FHitResult Result = ProjectToSurface(GetEditorWorld(),
			                                           RayOrigin, RayDirection,
			                                           IgnoreSelectionQueryParams);
			if (Result.IsValidBlockingHit())
			{
				const bool AlignToNormal = ViewportSettings->SnapToSurface.bSnapRotation;
				const float Offset = ViewportSettings->SnapToSurface.SnapOffsetExtent;
				FVector Location = Result.Location + Result.Normal * Offset;
				const bool IsSnapTrEnabled = bInvertSnap
					                             ? !ViewportSettings->GridEnabled
					                             : ViewportSettings->GridEnabled;
				if (IsSnapTrEnabled)
				{
					FVector SnappedLocation = Location - TransformPivot.GetLocation();
					const float GridSize = GEditor->GetGridSize();
					SnappedLocation = FVector(
						ceilf(SnappedLocation.X / GridSize) * GridSize,
						ceilf(SnappedLocation.Y / GridSize) * GridSize,
						ceilf(SnappedLocation.Z / GridSize) * GridSize);
					Location = SnappedLocation + TransformPivot.GetLocation();
					SetDirectTransformToSelectedActors(&Location);
				}
				else if (AlignToNormal)
				{
					const FRotator SurfaceRotation = FRotationMatrix::MakeFromZ(Result.Normal).Rotator();
					SetDirectTransformToSelectedActors(&Location, &SurfaceRotation);
				}
				else
				{
					SetDirectTransformToSelectedActors(&Location);
				}
			}
		}
		else
		{
			FVector Dir = (DragInitialProjectedPosition - HitLocation);
			const float TransformValue = Dir.Length();
			Dir /= TransformValue;
			ApplyTransform(Dir, -TransformValue, bInvertSnap);
		}
	}
	else
	{
		// single axis transform
		const FSceneView* Scene = GetActiveSceneView();
		if (!Scene)
		{
			return;
		}
		const FVector ViewDir = Scene->GetViewDirection().GetSafeNormal();
		const bool IsAlignedWithCamera = abs(FVector::DotProduct(ViewDir, AxisVector)) > 0.96;
		const FVector Axis = IsAlignedWithCamera ? Scene->GetViewUp() : AxisVector;
		const float TransformValue = Axis.Dot(DragInitialProjectedPosition - HitLocation);
		ApplyTransform(AxisVector, -TransformValue, bInvertSnap);
	}

	UpdateVisualization();
}

void FTransformController::ResetTransform(const ETransformMode Mode) const
{
	// Get appropriate handler for current viewport context
	TSharedPtr<IBlend4RealTransformHandler> ResetHandler = FTransformHandlerFactory::CreateHandler();
	if (!ResetHandler || !ResetHandler->HasSelection())
	{
		return;
	}

	ResetHandler->BeginTransaction(FText::FromString(TEXT("Reset Transform")));

	switch (Mode)
	{
	case ETransformMode::Translation:
		{
			const FVector Translation(0.0);
			ResetHandler->SetDirectTransform(&Translation, nullptr, nullptr);
		}
		break;
	case ETransformMode::Rotation:
		{
			const FRotator Rotation(0.0);
			ResetHandler->SetDirectTransform(nullptr, &Rotation, nullptr);
		}
		break;
	case ETransformMode::Scale:
		{
			const FVector Scale(1.0);
			ResetHandler->SetDirectTransform(nullptr, nullptr, &Scale);
		}
		break;
	default:
		break;
	}

	ResetHandler->EndTransaction();
	// Invalidate the focused viewport to trigger redraw
	if (FEditorViewportClient* ViewportClient = GetFocusedViewportClient())
	{
		ViewportClient->Invalidate();
	}
}

FVector FTransformController::GetAxisVector(const ETransformAxis::Type Axis) const
{
	// For local axes, compute the average axis direction across all selected items
	const bool bHasSelection = TransformHandler && TransformHandler->HasSelection();

	switch (Axis)
	{
	case ETransformAxis::LocalX:
		if (bHasSelection)
		{
			return TransformHandler->ComputeAverageLocalAxis(EAxis::X);
		}
	// Fall through to WorldX
	case ETransformAxis::WorldX:
		return FVector(1.0, 0.0, 0.0);

	case ETransformAxis::LocalY:
		if (bHasSelection)
		{
			return TransformHandler->ComputeAverageLocalAxis(EAxis::Y);
		}
	// Fall through to WorldY
	case ETransformAxis::WorldY:
		return FVector(0.0, 1.0, 0.0);

	case ETransformAxis::LocalZ:
		if (bHasSelection)
		{
			return TransformHandler->ComputeAverageLocalAxis(EAxis::Z);
		}
	// Fall through to WorldZ
	case ETransformAxis::WorldZ:
		return FVector(0.0, 0.0, 1.0);

	// For planes, only translation and Scale make sense. For rotation, we use the normal axis of the plane for the axis
	// Meaning that rotating on Z plane is equivalent to rotating on Z axis.
	// X Plane
	case ETransformAxis::LocalXPlane:
		if (bHasSelection)
		{
			return CurrentMode == ETransformMode::Rotation
				       ? TransformHandler->ComputeAverageLocalAxis(EAxis::X)
				       : (TransformHandler->ComputeAverageLocalAxis(EAxis::Y) + TransformHandler->ComputeAverageLocalAxis(EAxis::Z)).GetSafeNormal();
		}
	// Fall through to WorldXPlane
	case ETransformAxis::WorldXPlane:
		return CurrentMode == ETransformMode::Rotation
			       ? FVector(1.0, 0.0, 0.0)
			       : FVector(0.0, 1.0, 1.0).GetSafeNormal();

	//Y Plane
	case ETransformAxis::LocalYPlane:
		if (bHasSelection)
		{
			return CurrentMode == ETransformMode::Rotation
				       ? TransformHandler->ComputeAverageLocalAxis(EAxis::Y)
				       : (TransformHandler->ComputeAverageLocalAxis(EAxis::X) + TransformHandler->ComputeAverageLocalAxis(EAxis::Z)).GetSafeNormal();
		}
	// Fall through to WorldYPlane
	case ETransformAxis::WorldYPlane:
		return CurrentMode == ETransformMode::Rotation
			       ? FVector(0.0, 1.0, 0.0)
			       : FVector(1.0, 0.0, 1.0).GetSafeNormal();

	//Z Plane
	case ETransformAxis::LocalZPlane:
		if (bHasSelection)
		{
			return CurrentMode == ETransformMode::Rotation
				       ? TransformHandler->ComputeAverageLocalAxis(EAxis::Z)
				       : (TransformHandler->ComputeAverageLocalAxis(EAxis::X) + TransformHandler->ComputeAverageLocalAxis(EAxis::Y)).GetSafeNormal();
		}
	// Fall through to WorldZPlane
	case ETransformAxis::WorldZPlane:
		return CurrentMode == ETransformMode::Rotation
			       ? FVector(0.0, 0.0, 1.0)
			       : FVector(1.0, 1.0, 0.0).GetSafeNormal();

	// No Axis (camera aligned)
	case ETransformAxis::None:
	default:
		if (CurrentMode == ETransformMode::Translation)
		{
			return FVector(0.0, 0.0, 0.0);
		}
		if (CurrentMode == ETransformMode::Rotation)
		{
			const UWorld* World = GetEditorWorld();
			if (World == nullptr)
			{
				return FVector(0.0, 0.0, 0.0);
			}
			const auto& ViewLocations = World->ViewLocationsRenderedLastFrame;
			if (ViewLocations.Num() == 0)
			{
				return FVector(0.0, 0.0, 0.0);
			}
			const FVector CamLocation = ViewLocations[0];
			return (CamLocation - TransformPivot.GetLocation()).GetSafeNormal();
		}
		// Scale - uniform
		return FVector(1.0, 1.0, 1.0);
	}
}

FPlane FTransformController::ComputePlane(const FVector& InitialPos)
{
	const FSceneView* Scene = GetActiveSceneView();
	if (!Scene)
	{
		return FPlane(FVector::UpVector, 0.0);
	}

	TransformViewDir = Scene->GetViewDirection().GetSafeNormal();
	const FVector Axis = GetAxisVector(CurrentAxis);
	const float DotVal = abs(FVector::DotProduct(TransformViewDir, Axis));
	FVector Normal = TransformViewDir;
	if (CurrentMode == ETransformMode::Translation && CurrentAxis >= ETransformAxis::WorldXPlane)
	{
		switch (CurrentAxis)
		{
		case ETransformAxis::WorldXPlane: return FPlane(FVector::UnitX(), TransformPivot.GetLocation().X);
		case ETransformAxis::WorldYPlane: return FPlane(FVector::UnitY(), TransformPivot.GetLocation().Y);
		case ETransformAxis::WorldZPlane: return FPlane(FVector::UnitZ(), TransformPivot.GetLocation().Z);
		case ETransformAxis::LocalXPlane:
			Normal = -TransformHandler->ComputeAverageLocalAxis(EAxis::X);
			break;
		case ETransformAxis::LocalYPlane:
			Normal = -TransformHandler->ComputeAverageLocalAxis(EAxis::Y);
			break;
		case ETransformAxis::LocalZPlane:
			Normal = -TransformHandler->ComputeAverageLocalAxis(EAxis::Z);
			break;
		default: return FPlane(FVector::UnitZ(), 0);
		}
	}
	if (CurrentMode == ETransformMode::Translation && CurrentAxis != ETransformAxis::None && DotVal > 0.3 && DotVal <=
		0.96)
	{
		if (CurrentAxis == ETransformAxis::WorldZ)
		{
			TransformViewDir.Z = 0.0;
			TransformViewDir = TransformViewDir.GetSafeNormal();
			const FPlane ZeroPlane(TransformViewDir, 0.0);
			const float Dist = FMath::RayPlaneIntersectionParam(InitialPos, TransformViewDir, ZeroPlane);
			Normal = -TransformViewDir;
			return FPlane(Normal, Dist);
		}
		const float Dist = TransformPivot.GetLocation().Z;
		Normal = FVector::UnitZ();
		return FPlane(Normal, Dist);
	}

	const FPlane ZeroPlane(Normal, 0.0);
	const float Dist = FMath::RayPlaneIntersectionParam(InitialPos, TransformViewDir, ZeroPlane);
	Normal = -Normal;
	return FPlane(Normal, Dist);
}


void FTransformController::ApplyTransform(const FVector& Direction, const float Value, const bool InvertSnapState)
{
	if (!bIsTransforming || !GEditor)
	{
		return;
	}
	TransformSelectedActors(Direction, Value, true, InvertSnapState);
}

void FTransformController::TransformSelectedActors(const FVector& Direction, const float Value, const bool Snap,
                                                   const bool InvertSnap)
{
	if (!GEditor || !TransformHandler)
	{
		return;
	}

	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
	const bool IsSnapTrEnabled = InvertSnap ? !ViewportSettings->GridEnabled : ViewportSettings->GridEnabled;
	const bool IsSnapRtEnabled = InvertSnap ? !ViewportSettings->RotGridEnabled : ViewportSettings->RotGridEnabled;
	const bool IsSnapScEnabled = InvertSnap ? !ViewportSettings->SnapScaleEnabled : ViewportSettings->SnapScaleEnabled;
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	FTransform NewPivotTransform = TransformPivot;

	switch (CurrentMode)
	{
	case ETransformMode::Translation:
		{
			const float GridSize = GEditor->GetGridSize();
			const bool DoSnap = IsSnapTrEnabled && Snap;
			const FVector Translation = Direction * Value;

			const float SnappedValue = DoSnap ? (ceilf(Value / GridSize) * GridSize) : Value;
			const FVector SnappedTranslation = DoSnap
				                                   ? FVector(
					                                   ceilf(Translation.X / GridSize) * GridSize,
					                                   ceilf(Translation.Y / GridSize) * GridSize,
					                                   ceilf(Translation.Z / GridSize) * GridSize)
				                                   : Translation;

			NewPivotTransform.SetLocation(NewPivotTransform.GetLocation() + SnappedTranslation);
			if (Value != 0.0)
			{
				ShowTransformInfo(FString::Printf(TEXT("%.1f"), SnappedValue), CursorPos);
			}
		}
		break;

	case ETransformMode::Rotation:
		{
			if (Direction.IsNearlyZero())
			{
				NewPivotTransform.SetRotation(TransformPivot.GetRotation());
				break;
			}
			const bool DoSnap = IsSnapRtEnabled && Snap;
			const float SnapAngle = GEditor->GetRotGridSize().Yaw;
			const float SnappedValue = DoSnap ? (ceilf(Value / SnapAngle) * SnapAngle) : Value;
			const FQuat DeltaRotation = FQuat(Direction, FMath::DegreesToRadians(-SnappedValue));
			NewPivotTransform.SetRotation(DeltaRotation * TransformPivot.GetRotation());
			ShowTransformInfo(
				FString::Printf(
					TEXT("%.1f\u00B0"), CurrentAxis == ETransformAxis::WorldZ ? -SnappedValue : SnappedValue),
				CursorPos);
		}
		break;

	case ETransformMode::Scale:
		{
			if (Value == 0.0)
			{
				NewPivotTransform.SetScale3D(TransformPivot.GetScale3D());
				break;
			}
			const bool DoSnap = IsSnapScEnabled && Snap;
			const float ScaleSnapValue = GEditor->GetScaleGridSize();
			const float SnappedValue = DoSnap ? (ceilf(Value / ScaleSnapValue) * ScaleSnapValue) : Value;
			NewPivotTransform.SetScale3D(Direction * (SnappedValue - 1.0) + 1.0);
			ShowTransformInfo(FString::Printf(TEXT("x %.2f"), SnappedValue), CursorPos);
		}
		break;

	default:
		break;
	}

	// Apply the new pivot transform to selection via handler
	TransformHandler->ApplyTransformAroundPivot(TransformPivot, NewPivotTransform);

	GEditor->RedrawLevelEditingViewports();
}

void FTransformController::SetDirectTransformToSelectedActors(const FVector* Location, const FRotator* Rotation,
                                                              const FVector* Scale) const
{
	if (!TransformHandler)
	{
		return;
	}

	TransformHandler->SetDirectTransform(Location, Rotation, Scale);
}

void FTransformController::ShowTransformInfo(const FString& Text, const FVector2D& ScreenPosition)
{
	if (!TransformInfoWindow.IsValid())
	{
		TransformInfoWindow = SNew(SWindow)
			.Type(EWindowType::CursorDecorator)
			.IsPopupWindow(true)
			.SizingRule(ESizingRule::Autosized)
			.SupportsTransparency(EWindowTransparency::PerWindow)
			.FocusWhenFirstShown(false)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.4f))
				.Padding(FMargin(8, 4))
				[
					SAssignNew(TransformInfoText, STextBlock)
					.Text(FText::FromString(Text))
					.ColorAndOpacity(FLinearColor::White)
				]
			];
		FSlateApplication::Get().AddWindow(TransformInfoWindow.ToSharedRef());
	}

	TransformInfoText->SetText(FText::FromString(Text));
	TransformInfoWindow->MoveWindowTo(ScreenPosition + FVector2D(20, 20));
}

void FTransformController::HideTransformInfo()
{
	if (TransformInfoWindow.IsValid())
	{
		TransformInfoWindow->RequestDestroyWindow();
		TransformInfoWindow.Reset();
	}
}

void FTransformController::UpdateVisualization()
{
	if (!bIsTransforming || !TransformHandler)
	{
		return;
	}

	// Get or cache the line batcher from the appropriate world
	if (!LineBatcher)
	{
		// Use handler's world if available (e.g., preview scene), otherwise use editor world
		UWorld* World = TransformHandler->GetVisualizationWorld();
		if (!World)
		{
			World = GetEditorWorld();
		}
		if (!World)
		{
			return;
		}
		LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);
		if (!LineBatcher)
		{
			return;
		}
	}

	LineBatcher->ClearBatch(TRANSFORM_BATCH_ID);

	// Draw mode-specific visualization
	if (CurrentMode == ETransformMode::Rotation)
	{
		LineBatcher->DrawLine(
			TransformPivot.GetLocation(), HitLocation,
			FLinearColor::White, SDPG_Foreground, 1.0f, 0.0f, TRANSFORM_BATCH_ID);

		const FVector InitialDirection = (DragInitialProjectedPosition - TransformPivot.GetLocation()).GetSafeNormal();
		const FVector EndPos = InitialDirection * 100.0 + TransformPivot.GetLocation();
		LineBatcher->DrawLine(
			TransformPivot.GetLocation(), EndPos,
			FLinearColor(FColor::Cyan), SDPG_Foreground, 1.0f, 0.0f, TRANSFORM_BATCH_ID);
	}
	else if (CurrentMode == ETransformMode::Scale)
	{
		LineBatcher->DrawLine(
			TransformPivot.GetLocation(), HitLocation,
			FLinearColor::White, SDPG_Foreground, 1.0f, 0.0f, TRANSFORM_BATCH_ID);
	}

	// Draw axis constraint line if an axis is selected
	if (CurrentAxis != ETransformAxis::None)
	{
		const FVector DragInitialActorPosition = TransformPivot.GetLocation();

		if (CurrentAxis < ETransformAxis::WorldXPlane)
		{
			const FVector Axis = GetAxisVector(CurrentAxis) * 100000.0;
			LineBatcher->DrawLine(
				DragInitialActorPosition - Axis,
				DragInitialActorPosition + Axis,
				FLinearColor(AxisColors[CurrentAxis]),
				SDPG_Foreground, 2.0f, 0.0f, TRANSFORM_BATCH_ID);
		}
		else
		{
			// plane transform : Switch on Axis
			FVector Axis1, Axis2;
			FLinearColor Color1, Color2;
			switch (CurrentAxis)
			{
			// X
			case ETransformAxis::LocalXPlane:
				Axis1 = TransformHandler->ComputeAverageLocalAxis(EAxis::Y);
				Axis2 = TransformHandler->ComputeAverageLocalAxis(EAxis::Z);
				Color1 = AxisColors[ETransformAxis::LocalY];
				Color2 = AxisColors[ETransformAxis::LocalZ];
				break;
			case ETransformAxis::WorldXPlane:
				Axis1 = FVector::UnitY();
				Axis2 = FVector::UnitZ();
				Color1 = AxisColors[ETransformAxis::LocalY];
				Color2 = AxisColors[ETransformAxis::LocalZ];
				break;
			// Y
			case ETransformAxis::LocalYPlane:
				Axis1 = TransformHandler->ComputeAverageLocalAxis(EAxis::X);
				Axis2 = TransformHandler->ComputeAverageLocalAxis(EAxis::Z);
				Color1 = AxisColors[ETransformAxis::LocalX];
				Color2 = AxisColors[ETransformAxis::LocalZ];
				break;
			case ETransformAxis::WorldYPlane:
				Axis1 = FVector::UnitX();
				Axis2 = FVector::UnitZ();
				Color1 = AxisColors[ETransformAxis::LocalX];
				Color2 = AxisColors[ETransformAxis::LocalZ];
				break;
			// Z
			case ETransformAxis::LocalZPlane:
				Axis1 = TransformHandler->ComputeAverageLocalAxis(EAxis::X);
				Axis2 = TransformHandler->ComputeAverageLocalAxis(EAxis::Y);
				Color1 = AxisColors[ETransformAxis::LocalX];
				Color2 = AxisColors[ETransformAxis::LocalY];
				break;
			case ETransformAxis::WorldZPlane:
				Axis1 = FVector::UnitX();
				Axis2 = FVector::UnitY();
				Color1 = AxisColors[ETransformAxis::LocalX];
				Color2 = AxisColors[ETransformAxis::LocalY];
				break;
			default:
				return;
			}

			// draw lines
			Axis1 *= 100000.0;
			Axis2 *= 100000.0;
			LineBatcher->DrawLine(
				DragInitialActorPosition - Axis1,
				DragInitialActorPosition + Axis1,
				Color1,
				SDPG_Foreground, 2.0f, 0.0f, TRANSFORM_BATCH_ID);
			LineBatcher->DrawLine(
				DragInitialActorPosition - Axis2,
				DragInitialActorPosition + Axis2,
				Color2,
				SDPG_Foreground, 2.0f, 0.0f, TRANSFORM_BATCH_ID);
		}
	}

	// Invalidate the focused viewport to trigger redraw
	if (FEditorViewportClient* ViewportClient = GetFocusedViewportClient())
	{
		ViewportClient->Invalidate();
	}
}

void FTransformController::ClearVisualization()
{
	if (LineBatcher)
	{
		LineBatcher->ClearBatch(TRANSFORM_BATCH_ID);
		// Reset the cached line batcher so next transform uses the correct world
		LineBatcher = nullptr;
	}

	// Invalidate the focused viewport to trigger redraw
	if (FEditorViewportClient* ViewportClient = GetFocusedViewportClient())
	{
		ViewportClient->Invalidate();
	}
}
