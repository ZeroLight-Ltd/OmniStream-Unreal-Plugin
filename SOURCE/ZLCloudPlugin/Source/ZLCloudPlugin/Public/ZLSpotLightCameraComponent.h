// Copyright ZeroLight ltd. All Rights Reserved.
// ZLSpotLightCameraComponent.h

#pragma once

#include "CoreMinimal.h"
#include "IZLCloudPluginModule.h"
#include "Components/ActorComponent.h"
#include "ZLSpotLightCameraComponent.generated.h"

UCLASS(Blueprintable, ClassGroup = (ZLSpotLight) , meta = (BlueprintSpawnableComponent))
class ZLCLOUDPLUGIN_API UZLSpotLightCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UZLSpotLightCameraComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Camera Spring Arm and Camera Components
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zerolight Omnistream Spotlight Camera")
	class USpringArmComponent* SpringArm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zerolight Omnistream Spotlight Camera")
	class UCameraComponent* CameraComponent;

	// Orbit speed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zerolight Omnistream Spotlight Camera")
	float OrbitSpeed;

	// Zoom limits
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zerolight Omnistream Spotlight Camera")
	float MinZoomDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zerolight Omnistream Spotlight Camera")
	float MaxZoomDistance;

	// Input functions
	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Spotlight Camera")
	float OrbitCameraYaw(float AxisValue);

	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Spotlight Camera")
	float OrbitCameraPitch(float AxisValue);

	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Spotlight Camera")
	float ZoomCamera(float AxisValue);

	UFUNCTION(BlueprintCallable, Category = "Zerolight Omnistream Spotlight Camera")
	void UpdateCamera(float yaw, float pitch, float zoom);

	virtual void OnRegister() override;
	void InitializeCamera();

private:
	// Current rotation values
	float CurrentYaw;
	float CurrentPitch;
	float TargetArmLength;
	float ZoomSmoothness;

	// Owner actor reference
	AActor* OwnerActor;
};
