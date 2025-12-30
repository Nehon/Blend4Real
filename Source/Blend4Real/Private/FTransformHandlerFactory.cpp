#include "FTransformHandlerFactory.h"
#include "IBlend4RealTransformHandler.h"
#include "FActorTransformHandler.h"
#include "FComponentTransformHandler.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"

TSharedPtr<IBlend4RealTransformHandler> FTransformHandlerFactory::CreateHandler()
{
	if (!GEditor)
	{
		return nullptr;
	}

	// Get current mouse position to check which viewport type we're over
	const FVector2D MousePosition = FSlateApplication::Get().GetCursorPos();

	// Level Editor: Check selection state to determine handler type
	if (Blend4RealUtils::IsMouseOverViewport(MousePosition, FName("SLevelViewport")))
	{
		// Priority 1: Components (more specific selection)
		USelection* SelectedComponents = GEditor->GetSelectedComponents();
		if (SelectedComponents && SelectedComponents->Num() > 0)
		{
			bool bHasSceneComponent = false;
			for (FSelectionIterator It(*SelectedComponents); It; ++It)
			{
				if (const USceneComponent* Obj = Cast<USceneComponent>(*It))
				{
					// Only SceneComponents can be transformed
					UE_LOG(LogTemp, Display, TEXT("Transform for comp: %s"), *Obj->GetName());
					bHasSceneComponent = true;
				}
				else
				{
					UE_LOG(LogTemp, Display, TEXT("Unknown obj for comp: %s"), *It->GetName());
				}
			}
			if (bHasSceneComponent)
			{
				return MakeShared<FComponentTransformHandler>();
			}
		}

		// Priority 2: Actors
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (SelectedActors && SelectedActors->Num() > 0)
		{
			return MakeShared<FActorTransformHandler>();
		}


		// Nothing selected
		return nullptr;
	}

	// TODO: SCS Editor support (requires separate FSCSTransformHandler)
	// if (Blend4RealUtils::IsSCSEditorViewportFocused())
	// {
	//     return MakeShared<FSCSTransformHandler>();
	// }

	// TODO: Add more viewport types here:
	// - Animation Editor (bones)
	// - Static Mesh Editor (sockets)
	// - Skeleton Editor (sockets)

	// No supported viewport focused
	return nullptr;
}
