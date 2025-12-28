#include "FTransformController.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Components/LineBatchComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

FTransformController::FTransformController()
{
}

void FTransformController::BeginTransform(const ETransformMode Mode)
{
	if (!GEditor || bIsTransforming)
	{
		return;
	}

	// Transform operations only work in Level Editor viewport
	if (!Blend4RealUtils::IsLevelEditorViewportFocused())
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors->Num() == 0)
	{
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

	// Log the mode
	FString ModeText;
	switch (Mode)
	{
	case ETransformMode::Translation:
		ModeText = TEXT("Move Actors");
		TransformPivot = Blend4RealUtils::ComputeSelectionPivot();
		{
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = Blend4RealUtils::GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin, RayDirection);
		}
		break;
	case ETransformMode::Rotation:
		ModeText = TEXT("Rotate Actors");
		TransformPivot = Blend4RealUtils::ComputeSelectionPivot();
		{
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = Blend4RealUtils::GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin, RayDirection);
			HitLocation = DragInitialProjectedPosition;
		}
		break;
	case ETransformMode::Scale:
		ModeText = TEXT("Scale Actors");
		TransformPivot = Blend4RealUtils::ComputeSelectionPivot();
		{
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = Blend4RealUtils::GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin, RayDirection);
			InitialScaleDistance = (DragInitialProjectedPosition - TransformPivot.GetLocation()).Length();
			HitLocation = DragInitialProjectedPosition;
		}
		break;
	default:
		ModeText = TEXT("Unknown");
		break;
	}

	// Mark all selected actors as modified
	TransactionIndex = GEditor->BeginTransaction(TEXT(""), FText::FromString(ModeText), nullptr);
	ActorsTransformMap.Empty();
	IgnoreSelectionQueryParams.bTraceComplex = true;
	IgnoreSelectionQueryParams.ClearIgnoredSourceObjects();

	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			Actor->Modify();
			ActorsTransformMap.Add(Actor->GetUniqueID(), Actor->GetActorTransform());
			IgnoreSelectionQueryParams.AddIgnoredSourceObject(Actor);
		}
	}
}

void FTransformController::EndTransform(const bool bApply)
{
	if (!bIsTransforming)
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
		// Restore original transforms
		ApplyTransform(FVector(0.0, 0.0, 0.0), 0);
		GEditor->CancelTransaction(TransactionIndex);
		TransactionIndex = -1;
	}
	else
	{
		GEditor->EndTransaction();
		TransactionIndex = -1;
	}

	ActorsTransformMap.Empty();
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

	const FString AxisText = Blend4RealUtils::AxisLabels[CurrentAxis];

	// Recompute plane hit for new axis
	const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
	DragInitialProjectedPosition = Blend4RealUtils::GetPlaneHit(HitPlane.GetNormal(), HitPlane.W, RayOrigin, RayDirection);
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
	HitLocation = Blend4RealUtils::GetPlaneHit(HitPlane.GetNormal(), HitPlane.W,RayOrigin, RayDirection);

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
		const ETransformAxis::Type Axis = static_cast<ETransformAxis::Type>(CurrentAxis >= ETransformAxis::LocalX ? CurrentAxis - 3 : CurrentAxis);
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
	if (CurrentAxis == ETransformAxis::None)
	{
		const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
		const bool Project = ViewportSettings->SnapToSurface.bEnabled && ActorsTransformMap.Array().Num() == 1;

		if (Project)
		{
			const FHitResult Result = Blend4RealUtils::ProjectToSurface(Blend4RealUtils::GetEditorWorld(),
				RayOrigin, RayDirection, IgnoreSelectionQueryParams);
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
					const FRotator SurfaceRotation = Result.Normal.Rotation();
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
		const FSceneView* Scene = Blend4RealUtils::GetActiveSceneView();
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

void FTransformController::ResetTransform(ETransformMode Mode)
{
	// Transform operations only work in Level Editor viewport
	if (!Blend4RealUtils::IsLevelEditorViewportFocused())
	{
		return;
	}

	GEditor->BeginTransaction(TEXT(""), FText::FromString(TEXT("Reset Transform")), nullptr);
	Blend4RealUtils::MarkSelectionModified();

	switch (Mode)
	{
	case ETransformMode::Translation:
		{
			const FVector Translation(0.0);
			SetDirectTransformToSelectedActors(&Translation);
		}
		break;
	case ETransformMode::Rotation:
		{
			const FRotator Rotation(0.0);
			SetDirectTransformToSelectedActors(nullptr, &Rotation);
		}
		break;
	case ETransformMode::Scale:
		{
			const FVector Scale(1.0);
			SetDirectTransformToSelectedActors(nullptr, nullptr, &Scale);
		}
		break;
	default:
		break;
	}

	GEditor->EndTransaction();
}

FVector FTransformController::GetAxisVector(const ETransformAxis::Type Axis) const
{
	switch (Axis)
	{
	case ETransformAxis::LocalX:
		if (ActorsTransformMap.Array().Num() == 1)
		{
			return ActorsTransformMap.Array()[0].Value.GetRotation().GetForwardVector();
		}
	// Fall through to WorldX
	case ETransformAxis::WorldX:
		return FVector(1.0, 0.0, 0.0);

	case ETransformAxis::LocalY:
		if (ActorsTransformMap.Array().Num() == 1)
		{
			return ActorsTransformMap.Array()[0].Value.GetRotation().GetRightVector();
		}
	// Fall through to WorldY
	case ETransformAxis::WorldY:
		return FVector(0.0, 1.0, 0.0);

	case ETransformAxis::LocalZ:
		if (ActorsTransformMap.Array().Num() == 1)
		{
			return ActorsTransformMap.Array()[0].Value.GetRotation().GetUpVector();
		}
	// Fall through to WorldZ
	case ETransformAxis::WorldZ:
		return FVector(0.0, 0.0, 1.0);

	case ETransformAxis::None:
	default:
		if (CurrentMode == ETransformMode::Translation)
		{
			return FVector(0.0, 0.0, 0.0);
		}
		if (CurrentMode == ETransformMode::Rotation)
		{
			const UWorld* World = Blend4RealUtils::GetEditorWorld();
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
	const FSceneView* Scene = Blend4RealUtils::GetActiveSceneView();
	if (!Scene)
	{
		return FPlane(FVector::UpVector, 0.0);
	}

	TransformViewDir = Scene->GetViewDirection().GetSafeNormal();
	const FVector Axis = GetAxisVector(CurrentAxis);
	const float DotVal = abs(FVector::DotProduct(TransformViewDir, Axis));

	if (CurrentMode == ETransformMode::Translation && CurrentAxis != ETransformAxis::None && DotVal > 0.3 && DotVal <= 0.96)
	{
		if (CurrentAxis == ETransformAxis::WorldZ)
		{
			TransformViewDir.Z = 0.0;
			TransformViewDir = TransformViewDir.GetSafeNormal();
			const FPlane ZeroPlane(TransformViewDir, 0.0);
			const float Dist = FMath::RayPlaneIntersectionParam(InitialPos, TransformViewDir, ZeroPlane);
			const FVector Normal = -TransformViewDir;
			return FPlane(Normal, Dist);
		}
		const float Dist = TransformPivot.GetLocation().Z;
		const FVector Normal(0.0, 0.0, 1.0);
		return FPlane(Normal, Dist);
	}

	const FPlane ZeroPlane(TransformViewDir, 0.0);
	const float Dist = FMath::RayPlaneIntersectionParam(InitialPos, TransformViewDir, ZeroPlane);
	const FVector Normal = -TransformViewDir;
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
	if (!GEditor)
	{
		return;
	}

	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
	const bool IsSnapTrEnabled = InvertSnap ? !ViewportSettings->GridEnabled : ViewportSettings->GridEnabled;
	const bool IsSnapRtEnabled = InvertSnap ? !ViewportSettings->RotGridEnabled : ViewportSettings->RotGridEnabled;
	const bool IsSnapScEnabled = InvertSnap ? !ViewportSettings->SnapScaleEnabled : ViewportSettings->SnapScaleEnabled;
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	FTransform NewTransform = TransformPivot;

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

			NewTransform.SetLocation(NewTransform.GetLocation() + SnappedTranslation);
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
				NewTransform.SetRotation(TransformPivot.GetRotation());
				break;
			}
			const bool DoSnap = IsSnapRtEnabled && Snap;
			const float SnapAngle = GEditor->GetRotGridSize().Yaw;
			const float SnappedValue = DoSnap ? (ceilf(Value / SnapAngle) * SnapAngle) : Value;
			const FQuat DeltaRotation = FQuat(Direction, FMath::DegreesToRadians(-SnappedValue));
			NewTransform.SetRotation(DeltaRotation * TransformPivot.GetRotation());
			ShowTransformInfo(FString::Printf(TEXT("%.1f\u00B0"), CurrentAxis == ETransformAxis::WorldZ ? -SnappedValue : SnappedValue),
			                  CursorPos);
		}
		break;

	case ETransformMode::Scale:
		{
			if (Value == 0.0)
			{
				NewTransform.SetScale3D(TransformPivot.GetScale3D());
				break;
			}
			const bool DoSnap = IsSnapScEnabled && Snap;
			const float ScaleSnapValue = GEditor->GetScaleGridSize();
			const float SnappedValue = DoSnap ? (ceilf(Value / ScaleSnapValue) * ScaleSnapValue) : Value;
			NewTransform.SetScale3D(Direction * (SnappedValue - 1.0) + 1.0);
			ShowTransformInfo(FString::Printf(TEXT("x %.2f"), SnappedValue), CursorPos);
		}
		break;

	default:
		break;
	}

	// Apply the new transform to selected actors
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			FTransform ActorTransform = ActorsTransformMap[Actor->GetUniqueID()];

			// Cancel the pivot transform and multiply by the new transform
			ActorTransform = ActorTransform * TransformPivot.Inverse();
			ActorTransform = ActorTransform * NewTransform;

			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
			}
		}
	}

	GEditor->RedrawLevelEditingViewports();
}

void FTransformController::SetDirectTransformToSelectedActors(const FVector* Location, const FRotator* Rotation,
                                                              const FVector* Scale)
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
			FTransform ActorTransform = Actor->GetActorTransform();
			ActorTransform.SetLocation(Location ? *Location : ActorTransform.GetLocation());
			ActorTransform.SetRotation(Rotation ? Rotation->Quaternion() : ActorTransform.GetRotation());
			ActorTransform.SetScale3D(Scale ? *Scale : ActorTransform.GetScale3D());

			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
			}
		}
	}
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
	if (!bIsTransforming)
	{
		return;
	}

	// Get or cache the line batcher
	if (!LineBatcher)
	{
		UWorld* World = Blend4RealUtils::GetEditorWorld();
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
		const FVector Axis = GetAxisVector(CurrentAxis) * 100000.0;
		LineBatcher->DrawLine(
			DragInitialActorPosition - Axis,
			DragInitialActorPosition + Axis,
			FLinearColor(Blend4RealUtils::AxisColors[CurrentAxis]),
			SDPG_Foreground, 2.0f, 0.0f, TRANSFORM_BATCH_ID);
	}

	GEditor->RedrawLevelEditingViewports();
}

void FTransformController::ClearVisualization() const
{
	if (LineBatcher)
	{
		LineBatcher->ClearBatch(TRANSFORM_BATCH_ID);
		GEditor->RedrawLevelEditingViewports();
	}
}
