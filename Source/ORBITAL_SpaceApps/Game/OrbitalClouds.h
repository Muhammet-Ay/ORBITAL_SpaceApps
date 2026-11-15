// ORBITAL: cloud layer around Earth on L_Mission. Display only; it changes no result, score or timeline.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OrbitalClouds.generated.h"

class AStaticMeshActor;
class UMaterialInstanceDynamic;

UCLASS()
class UOrbitalCloudsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	UPROPERTY()
	TObjectPtr<AStaticMeshActor> CloudShell;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterialInstance;

	double ShellRadius = 0.0;
	bool bGlowOn = false;
};
