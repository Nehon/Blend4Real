#include "Blend4RealUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Engine/Selection.h"
#include "Slate/SceneViewport.h"

namespace Blend4RealUtils
{
	const FColor AxisColors[TransformAxes_Count] = {
		FColor::Black, FColor::Red, FColor::Green, FColor::Blue, FColor::Red, FColor::Green, FColor::Blue
	};

	const char* AxisLabels[TransformAxes_Count] = {
		"None", "X", "Y", "Z", "Local X", "Local Y", "Local Z",
	};

	UWorld* GetEditorWorld()
	{
		if (!GEditor || !GEditor->GetActiveViewport())
		{
			return nullptr;
		}
		return GEditor->GetActiveViewport()->GetClient()->GetWorld();
	}

	FSceneView* GetActiveSceneView()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		const FViewport* Viewport = GEditor->GetActiveViewport();
		if (!Viewport)
		{
			return nullptr;
		}

		FEditorViewportClient* EClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
		if (!EClient)
		{
			return nullptr;
		}

		FSceneViewFamily ViewFamily = FSceneViewFamily::ConstructionValues(
			Viewport, EClient->GetScene(), EClient->EngineShowFlags);
		return EClient->CalcSceneView(&ViewFamily);
	}

	FTransform ComputeSelectionPivot()
	{
		if (!GEditor)
		{
			return FTransform();
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		FVector Center(0.0);
		int Count = 0;
		FTransform Transform = FTransform();

		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (const AActor* Actor = Cast<AActor>(*It))
			{
				FTransform ActorTransform = Actor->GetActorTransform();
				Center += ActorTransform.GetLocation();
				Count++;
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
		// Check if mouse is actually over the viewport widget
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		const TSharedPtr<SLevelViewport> LevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
		if (!LevelViewport.IsValid())
		{
			return FHitResult();
		}

		// Get the widget cached geometry for bounds checking
		auto Geometry = LevelViewport->GetCachedGeometry();
		auto LocalSize = Geometry.GetLocalSize();
		auto Min = Geometry.LocalToAbsolute(FVector2D(0, 0));
		auto Max = Geometry.LocalToAbsolute(LocalSize);

		if (MousePosition.X < Min.X || MousePosition.X > Max.X ||
			MousePosition.Y < Min.Y || MousePosition.Y > Max.Y)
		{
			// Mouse is outside the viewport bounds
			return FHitResult();
		}

		if (!GEditor)
		{
			return FHitResult();
		}

		FViewport* Viewport = GEditor->GetActiveViewport();
		if (!Viewport)
		{
			return FHitResult();
		}

		FIntPoint MousePos;
		Viewport->GetMousePos(MousePos);

		FEditorViewportClient* EClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
		if (!EClient)
		{
			return FHitResult();
		}

		FSceneViewFamily ViewFamily = FSceneViewFamily::ConstructionValues(
			Viewport, EClient->GetScene(), EClient->EngineShowFlags);
		MousePos /= EClient->GetDPIScale();

		const FSceneView* Scene = EClient->CalcSceneView(&ViewFamily);
		if (!Scene)
		{
			return FHitResult();
		}

		Scene->DeprojectFVector2D(MousePos, OutRayOrigin, OutRayDirection);

		FCollisionQueryParams Params;
		Params.bTraceComplex = true;
		return ProjectToSurface(OutRayOrigin, OutRayDirection, Params);
	}

	FHitResult ProjectToSurface(const FVector& Start, const FVector& Direction, const FCollisionQueryParams& Params)
	{
		FHitResult HitResult;
		const UWorld* World = GetEditorWorld();
		if (!World)
		{
			return HitResult;
		}

		const FVector End = Start + Direction * 100000.f;
		World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);
		return HitResult;
	}

	bool IsTransformKey(const FKeyEvent& KeyEvent)
	{
		const FKey Key = KeyEvent.GetKey();
		return Key == EKeys::G || Key == EKeys::R || Key == EKeys::S;
	}

	bool IsAxisKey(const FKeyEvent& KeyEvent, ETransformAxis& OutAxis)
	{
		const FKey Key = KeyEvent.GetKey();

		if (Key == EKeys::X)
		{
			OutAxis = WorldX;
			return true;
		}
		if (Key == EKeys::Y)
		{
			OutAxis = WorldY;
			return true;
		}
		if (Key == EKeys::Z)
		{
			OutAxis = WorldZ;
			return true;
		}

		return false;
	}

	bool IsNumericKey(const FKeyEvent& KeyEvent, FString& OutDigit)
	{
		const FKey Key = KeyEvent.GetKey();

		if (Key == EKeys::Zero || Key == EKeys::NumPadZero)
		{
			OutDigit = TEXT("0");
			return true;
		}
		if (Key == EKeys::One || Key == EKeys::NumPadOne)
		{
			OutDigit = TEXT("1");
			return true;
		}
		if (Key == EKeys::Two || Key == EKeys::NumPadTwo)
		{
			OutDigit = TEXT("2");
			return true;
		}
		if (Key == EKeys::Three || Key == EKeys::NumPadThree)
		{
			OutDigit = TEXT("3");
			return true;
		}
		if (Key == EKeys::Four || Key == EKeys::NumPadFour)
		{
			OutDigit = TEXT("4");
			return true;
		}
		if (Key == EKeys::Five || Key == EKeys::NumPadFive)
		{
			OutDigit = TEXT("5");
			return true;
		}
		if (Key == EKeys::Six || Key == EKeys::NumPadSix)
		{
			OutDigit = TEXT("6");
			return true;
		}
		if (Key == EKeys::Seven || Key == EKeys::NumPadSeven)
		{
			OutDigit = TEXT("7");
			return true;
		}
		if (Key == EKeys::Eight || Key == EKeys::NumPadEight)
		{
			OutDigit = TEXT("8");
			return true;
		}
		if (Key == EKeys::Nine || Key == EKeys::NumPadNine)
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
}
