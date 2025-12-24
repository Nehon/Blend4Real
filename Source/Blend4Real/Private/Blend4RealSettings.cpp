#include "Blend4RealSettings.h"
#include "Logging/MessageLog.h"

UBlend4RealSettings::FOnBlend4RealSettingsChanged UBlend4RealSettings::OnSettingsChanged;

UBlend4RealSettings::UBlend4RealSettings()
{
	CategoryName = FName("Plugins");
}

UBlend4RealSettings* UBlend4RealSettings::Get()
{
	return GetMutableDefault<UBlend4RealSettings>();
}

bool UBlend4RealSettings::MatchesChord(const FInputChord& Chord, const FKey& Key, EModifierKey::Type ModMask)
{
	if (!Chord.IsValidChord()) return false;
	if (Chord.Key != Key) return false;

	// Check modifiers match exactly
	const bool bEventShift = (ModMask & EModifierKey::Shift) != 0;
	const bool bEventCtrl = (ModMask & EModifierKey::Control) != 0;
	const bool bEventAlt = (ModMask & EModifierKey::Alt) != 0;
	const bool bEventCmd = (ModMask & EModifierKey::Command) != 0;

	return (Chord.bShift == bEventShift) &&
		   (Chord.bCtrl == bEventCtrl) &&
		   (Chord.bAlt == bEventAlt) &&
		   (Chord.bCmd == bEventCmd);
}

bool UBlend4RealSettings::MatchesChord(const FInputChord& Chord, const FKeyEvent& KeyEvent)
{
	const EModifierKey::Type ModMask = EModifierKey::FromBools(
		KeyEvent.IsControlDown(), KeyEvent.IsAltDown(),
		KeyEvent.IsShiftDown(), KeyEvent.IsCommandDown());
	return MatchesChord(Chord, KeyEvent.GetKey(), ModMask);
}

bool UBlend4RealSettings::MatchesChord(const FInputChord& Chord, const FPointerEvent& MouseEvent)
{
	const EModifierKey::Type ModMask = EModifierKey::FromBools(
		MouseEvent.IsControlDown(), MouseEvent.IsAltDown(),
		MouseEvent.IsShiftDown(), MouseEvent.IsCommandDown());
	return MatchesChord(Chord, MouseEvent.GetEffectingButton(), ModMask);
}

TArray<FString> UBlend4RealSettings::GetConflictingBindings(const FInputChord& Chord, const FName& ExcludeProperty) const
{
	TArray<FString> Conflicts;
	if (!Chord.IsValidChord()) return Conflicts;

	// Build map of all bindings
	TMap<FName, TPair<const FInputChord*, FString>> Bindings;
	Bindings.Add("TranslationKey", {&TranslationKey, TEXT("Begin Translation")});
	Bindings.Add("RotationKey", {&RotationKey, TEXT("Begin Rotation")});
	Bindings.Add("ScaleKey", {&ScaleKey, TEXT("Begin Scale")});
	Bindings.Add("ResetTranslationKey", {&ResetTranslationKey, TEXT("Reset Translation")});
	Bindings.Add("ResetRotationKey", {&ResetRotationKey, TEXT("Reset Rotation")});
	Bindings.Add("ResetScaleKey", {&ResetScaleKey, TEXT("Reset Scale")});
	Bindings.Add("DuplicateKey", {&DuplicateKey, TEXT("Duplicate")});
	Bindings.Add("DeleteSelectedKey", {&DeleteSelectedKey, TEXT("Delete Selected")});
	Bindings.Add("OrbitCameraKey", {&OrbitCameraKey, TEXT("Orbit Camera")});
	Bindings.Add("PanCameraKey", {&PanCameraKey, TEXT("Pan Camera")});
	Bindings.Add("FocusOnHitKey", {&FocusOnHitKey, TEXT("Focus on Hit")});
	Bindings.Add("ApplyTransformKey", {&ApplyTransformKey, TEXT("Apply Transform")});
	Bindings.Add("CancelTransformKey", {&CancelTransformKey, TEXT("Cancel Transform")});

	for (const auto& [Name, Pair] : Bindings)
	{
		if (Name != ExcludeProperty && *Pair.Key == Chord)
		{
			Conflicts.Add(Pair.Value);
		}
	}
	return Conflicts;
}

void UBlend4RealSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		const FInputChord* ChangedChord = nullptr;

		// Map property name to chord pointer
		if (PropName == "TranslationKey") ChangedChord = &TranslationKey;
		else if (PropName == "RotationKey") ChangedChord = &RotationKey;
		else if (PropName == "ScaleKey") ChangedChord = &ScaleKey;
		else if (PropName == "ResetTranslationKey") ChangedChord = &ResetTranslationKey;
		else if (PropName == "ResetRotationKey") ChangedChord = &ResetRotationKey;
		else if (PropName == "ResetScaleKey") ChangedChord = &ResetScaleKey;
		else if (PropName == "DuplicateKey") ChangedChord = &DuplicateKey;
		else if (PropName == "DeleteSelectedKey") ChangedChord = &DeleteSelectedKey;
		else if (PropName == "OrbitCameraKey") ChangedChord = &OrbitCameraKey;
		else if (PropName == "PanCameraKey") ChangedChord = &PanCameraKey;
		else if (PropName == "FocusOnHitKey") ChangedChord = &FocusOnHitKey;
		else if (PropName == "ApplyTransformKey") ChangedChord = &ApplyTransformKey;
		else if (PropName == "CancelTransformKey") ChangedChord = &CancelTransformKey;

		if (ChangedChord && ChangedChord->IsValidChord())
		{
			TArray<FString> Conflicts = GetConflictingBindings(*ChangedChord, PropName);
			if (Conflicts.Num() > 0)
			{
				FMessageLog("Blend4Real").Warning(FText::Format(
					NSLOCTEXT("Blend4Real", "KeyConflict", "Key '{0}' conflicts with: {1}"),
					FText::FromString(ChangedChord->GetInputText().ToString()),
					FText::FromString(FString::Join(Conflicts, TEXT(", ")))
				));
				FMessageLog("Blend4Real").Open();
			}
		}
	}

	// Broadcast settings change
	OnSettingsChanged.Broadcast(this);
}
