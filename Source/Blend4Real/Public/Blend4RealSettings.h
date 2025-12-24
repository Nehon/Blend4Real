#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Commands/InputChord.h"
#include "Blend4RealSettings.generated.h"

UENUM(BlueprintType)
enum class EBlend4RealOrbitMode : uint8
{
	Default UMETA(DisplayName = "Default", ToolTip = "Use Unreal's default orbit behavior"),
	OrbitAroundMouseProjection UMETA(DisplayName = "Orbit Around Mouse Cursor Projection", ToolTip = "Orbit around the point where the mouse cursor projects onto the scene"),
	OrbitAroundSelection UMETA(DisplayName = "Orbit Around Selection", ToolTip = "Orbit around the center of the selected actors")
};

UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Blend4Real"))
class BLEND4REAL_API UBlend4RealSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBlend4RealSettings();

	// Get the settings instance
	static UBlend4RealSettings* Get();

	// Settings category in Project Settings
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	// Orbit mode setting
	UPROPERTY(Config, EditAnywhere, Category = "Navigation", meta = (DisplayName = "Orbit Mode", ToolTip = "Controls how the camera orbits when using middle mouse button"))
	EBlend4RealOrbitMode OrbitMode = EBlend4RealOrbitMode::OrbitAroundMouseProjection;

	// Helper methods to get orbit flags
	bool ShouldOrbitAroundSelection() const { return OrbitMode == EBlend4RealOrbitMode::OrbitAroundSelection; }
	bool ShouldOrbitAroundMouseHit() const { return OrbitMode == EBlend4RealOrbitMode::OrbitAroundMouseProjection; }

	// ===== Keybindings: Transform Initiation =====
	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Transform",
		meta = (DisplayName = "Begin Translation (Grab)"))
	FInputChord TranslationKey = FInputChord(EKeys::G);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Transform",
		meta = (DisplayName = "Begin Rotation"))
	FInputChord RotationKey = FInputChord(EKeys::R);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Transform",
		meta = (DisplayName = "Begin Scale"))
	FInputChord ScaleKey = FInputChord(EKeys::S);

	// ===== Keybindings: Transform Reset =====
	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Transform Reset",
		meta = (DisplayName = "Reset Translation"))
	FInputChord ResetTranslationKey = FInputChord(EModifierKey::Alt, EKeys::G);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Transform Reset",
		meta = (DisplayName = "Reset Rotation"))
	FInputChord ResetRotationKey = FInputChord(EModifierKey::Alt, EKeys::R);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Transform Reset",
		meta = (DisplayName = "Reset Scale"))
	FInputChord ResetScaleKey = FInputChord(EModifierKey::Alt, EKeys::S);

	// ===== Keybindings: Object Actions =====
	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Objects",
		meta = (DisplayName = "Duplicate"))
	FInputChord DuplicateKey = FInputChord(EModifierKey::Shift, EKeys::D);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Objects",
		meta = (DisplayName = "Delete Selected"))
	FInputChord DeleteSelectedKey = FInputChord(EKeys::X);

	// ===== Keybindings: Camera Navigation =====
	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Camera",
		meta = (DisplayName = "Orbit Camera"))
	FInputChord OrbitCameraKey = FInputChord(EKeys::MiddleMouseButton);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Camera",
		meta = (DisplayName = "Pan Camera"))
	FInputChord PanCameraKey = FInputChord(EModifierKey::Shift, EKeys::MiddleMouseButton);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Camera",
		meta = (DisplayName = "Focus on Hit"))
	FInputChord FocusOnHitKey = FInputChord(EModifierKey::Alt, EKeys::MiddleMouseButton);

	// ===== Keybindings: Transform Confirmation =====
	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Confirmation",
		meta = (DisplayName = "Apply Transform"))
	FInputChord ApplyTransformKey = FInputChord(EKeys::LeftMouseButton);

	UPROPERTY(Config, EditAnywhere, Category = "Keybindings|Confirmation",
		meta = (DisplayName = "Cancel Transform"))
	FInputChord CancelTransformKey = FInputChord(EKeys::RightMouseButton);

	// Check if input matches a keybinding chord
	static bool MatchesChord(const FInputChord& Chord, const FKey& Key, EModifierKey::Type ModMask);
	static bool MatchesChord(const FInputChord& Chord, const FKeyEvent& KeyEvent);
	static bool MatchesChord(const FInputChord& Chord, const FPointerEvent& MouseEvent);

	// Conflict detection (returns names of conflicting bindings)
	TArray<FString> GetConflictingBindings(const FInputChord& Chord, const FName& ExcludeProperty) const;

	// Delegate for settings changes
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBlend4RealSettingsChanged, const UBlend4RealSettings*);
	static FOnBlend4RealSettingsChanged OnSettingsChanged;

protected:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
};
