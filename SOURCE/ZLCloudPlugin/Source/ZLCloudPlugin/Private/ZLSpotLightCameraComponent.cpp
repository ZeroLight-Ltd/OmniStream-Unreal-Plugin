// Copyright ZeroLight ltd. All Rights Reserved.
// ZLSpotLightCameraComponent.cpp

#include "ZLSpotLightCameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"

// Sets default values for this component's properties
UZLSpotLightCameraComponent::UZLSpotLightCameraComponent()
{
	// Enable ticking in editor
	bTickInEditor = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Enable editor ticking
	PrimaryComponentTick.bTickEvenWhenPaused = true;

	// Initialize default values
	OrbitSpeed = 100.0f;
	MinZoomDistance = 10.0f;
	MaxZoomDistance = 10000.0f;
	TargetArmLength = 200.0f;
	CurrentYaw = 180.0f;
	CurrentPitch = 0.0f;
	ZoomSmoothness = 5.0f;

	// Create the SpringArmComponent
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));

	// Update spring arm collision settings
	SpringArm->bDoCollisionTest = false;

	// Find the existing camera component instead of creating a new one
	CameraComponent = nullptr;  // Will be set in InitializeCamera()
}

// Called when the game starts
void UZLSpotLightCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	InitializeCamera();
}

// Add new function to handle initialization
void UZLSpotLightCameraComponent::InitializeCamera()
{
	// Move initialization logic here so it works both in editor and at runtime
	OwnerActor = GetOwner();
	if (OwnerActor)
	{
		// Attach spring arm to root component with relative transform
		if (!SpringArm->GetAttachParent())
		{
			SpringArm->AttachToComponent(OwnerActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		}

		// Find the existing camera component
		CameraComponent = OwnerActor->FindComponentByClass<UCameraComponent>();
		if (CameraComponent)
		{
			// Attach the camera to the spring arm's end
			CameraComponent->AttachToComponent(SpringArm, FAttachmentTransformRules::KeepRelativeTransform, USpringArmComponent::SocketName);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("No Camera Component found on owner actor"));
		}
	}
}

// Override OnRegister to initialize in editor
void UZLSpotLightCameraComponent::OnRegister()
{
	Super::OnRegister();
	
	// Initialize camera when component is registered (works in editor)
	InitializeCamera();
}

// Called every frame
void UZLSpotLightCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (OwnerActor && CameraComponent)
	{
		// Update the spring arm's world location to match the owner's location
		FVector OwnerLocation = OwnerActor->GetActorLocation();
		SpringArm->SetWorldLocation(OwnerLocation);

		// Smoothly interpolate arm length
		SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArmLength, DeltaTime, ZoomSmoothness);

		// Calculate target rotation
		FRotator TargetRotation = FRotator(CurrentPitch, CurrentYaw, 0);
		
		// Update spring arm rotation
		SpringArm->SetRelativeRotation(TargetRotation);
	}
}

void UZLSpotLightCameraComponent::UpdateCamera(float yaw, float pitch, float zoom)
{
	CurrentPitch = pitch;
	CurrentYaw = yaw;
	SpringArm->TargetArmLength = TargetArmLength = zoom;
}

// Function to orbit the camera
float UZLSpotLightCameraComponent::OrbitCameraYaw(float AxisValue)
{
	CurrentYaw += AxisValue * OrbitSpeed * GetWorld()->DeltaTimeSeconds;
	return CurrentYaw;
}

float UZLSpotLightCameraComponent::OrbitCameraPitch(float AxisValue)
{
	CurrentPitch += AxisValue * OrbitSpeed * GetWorld()->DeltaTimeSeconds;
	return CurrentPitch;
}

// Function to zoom the camera in and out
float UZLSpotLightCameraComponent::ZoomCamera(float AxisValue)
{
	float NewArmLength = TargetArmLength - AxisValue * 100.0f;
	TargetArmLength = FMath::Clamp(NewArmLength, MinZoomDistance, MaxZoomDistance);
	return TargetArmLength;
}
