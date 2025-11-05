// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomFloatingPawnMovement.h"
#include "Engine/HitResult.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/WorldSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomFloatingPawnMovement)

UCustomFloatingPawnMovement::UCustomFloatingPawnMovement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MaxSpeed = 1200.f;
	Acceleration = 4000.f;
	Deceleration = 8000.f;
	TurningBoost = 8.0f;
	bPositionCorrected = false;

	// Gravity and ground settings
	GravityScale = 1.f;  // Gravity force
	GroundFriction = 8.0f;  // Friction when on flat ground
	SlopeFriction = 4.0f;   // Friction when on slopes
	MaxWalkableAngle = 45.0f;  // Maximum walkable slope angle
	GroundTraceDistance = 200.f;  // Distance to check for ground
	GravityForce = -9.8f; // gravidade
	GravityMultiplier = 500.0f; //gravity multiplier to no bother with force and scale
	bIsOnGround = false;
	bIsOnSteepSlope = false;

	ResetMoveState();
}

void UCustomFloatingPawnMovement::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PawnOwner || !UpdatedComponent)
	{
		return;
	}

	if (!bIsOnGround) // If not grounded and moving downwards add gravidade
	{
		Velocity.Z += GravityScale * GravityForce * GravityMultiplier * DeltaTime;
	}
	
	const AController* Controller = PawnOwner->GetController();
	if (Controller && Controller->IsLocalController())
	{
		// Check if we're on the ground
		CheckGround();

		// apply input for local players but also for AI that's not following a navigation path at the moment
		if (Controller->IsLocalPlayerController() == true || Controller->IsFollowingAPath() == false || NavMovementProperties.bUseAccelerationForPaths)
		{
			ApplyControlInputToVelocity(DeltaTime);
		}
		// if it's not player controller, but we do have a controller, then it's AI
		// (that's not following a path) and we need to limit the speed
		else if (IsExceedingMaxSpeed(MaxSpeed) == true)
		{
			Velocity = Velocity.GetUnsafeNormal() * MaxSpeed;
		}

		// Apply friction if on ground
		ApplyGroundFriction(DeltaTime);

		LimitWorldBounds();
		bPositionCorrected = false;

		// Move actor
		FVector Delta = Velocity * DeltaTime;

		if (!Delta.IsNearlyZero(1e-6f))
		{
			const FVector OldLocation = UpdatedComponent->GetComponentLocation();
			const FQuat Rotation = UpdatedComponent->GetComponentQuat();

			FHitResult Hit(1.f);
			SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);

			if (Hit.IsValidBlockingHit())
			{
				HandleImpact(Hit, DeltaTime, Delta);
				// Try to slide the remaining distance along the surface.
				SlideAlongSurface(Delta, 1.f-Hit.Time, Hit.Normal, Hit, true);
			}

			// Update velocity
			// We don't want position changes to vastly reverse our direction (which can happen due to penetration fixups etc)
			if (!bPositionCorrected)
			{
				const FVector NewLocation = UpdatedComponent->GetComponentLocation();
				Velocity = ((NewLocation - OldLocation) / DeltaTime);
			}
		}
	
		// Finalize
		UpdateComponentVelocity();
	}
};

bool UCustomFloatingPawnMovement::LimitWorldBounds()
{
	AWorldSettings* WorldSettings = PawnOwner ? PawnOwner->GetWorldSettings() : NULL;
	if (!WorldSettings || !WorldSettings->AreWorldBoundsChecksEnabled() || !UpdatedComponent)
	{
		return false;
	}

	const FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	if ( CurrentLocation.Z < WorldSettings->KillZ )
	{
		Velocity.Z = FMath::Min<FVector::FReal>(GetMaxSpeed(), WorldSettings->KillZ - CurrentLocation.Z + 2.0f);
		return true;
	}

	return false;
}

void UCustomFloatingPawnMovement::ApplyControlInputToVelocity(float DeltaTime)
{
	const FVector ControlAcceleration = GetPendingInputVector().GetClampedToMaxSize(1.f);

	const float AnalogInputModifier = (ControlAcceleration.SizeSquared() > 0.f ? ControlAcceleration.Size() : 0.f);
	const float MaxPawnSpeed = GetMaxSpeed() * AnalogInputModifier;
	const bool bExceedingMaxSpeed = IsExceedingMaxSpeed(MaxPawnSpeed);

	if (AnalogInputModifier > 0.f && !bExceedingMaxSpeed)
	{
		// Apply change in velocity direction
		if (Velocity.SizeSquared() > 0.f)
		{
			// Change direction faster than only using acceleration, but never increase velocity magnitude.
			const float TimeScale = FMath::Clamp(DeltaTime * TurningBoost, 0.f, 1.f);
			Velocity = Velocity + (ControlAcceleration * Velocity.Size() - Velocity) * TimeScale;
		}
	}
	else
	{
		// Dampen velocity magnitude based on deceleration (only X and Y, preserve Z).
		if (Velocity.SizeSquared() > 0.f)
		{
			const FVector OldVelocity = Velocity;
			const float OldVelocityZ = Velocity.Z; // Store Z component
			
			// Get horizontal velocity (X and Y only)
			FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.f);
			const float HorizontalSpeed = HorizontalVelocity.Size();
			
			if (HorizontalSpeed > 0.f)
			{
				const float NewHorizontalSpeed = FMath::Max(HorizontalSpeed - FMath::Abs(Deceleration) * DeltaTime, 0.f);
				HorizontalVelocity = HorizontalVelocity.GetSafeNormal() * NewHorizontalSpeed;
			}
			
			// Reconstruct velocity with dampened X/Y and original Z
			Velocity = FVector(HorizontalVelocity.X, HorizontalVelocity.Y, OldVelocityZ);

			// Don't allow braking to lower us below max speed if we started above it.
			if (bExceedingMaxSpeed && Velocity.SizeSquared() < FMath::Square(MaxPawnSpeed))
			{
				Velocity = OldVelocity.GetSafeNormal() * MaxPawnSpeed;
			}
		}
	}

	// Apply acceleration and clamp velocity magnitude.
	const float NewMaxSpeed = (IsExceedingMaxSpeed(MaxPawnSpeed)) ? Velocity.Size() : MaxPawnSpeed;
	Velocity += ControlAcceleration * FMath::Abs(Acceleration) * DeltaTime;
	Velocity = Velocity.GetClampedToMaxSize(NewMaxSpeed);

	ConsumeInputVector();
}

bool UCustomFloatingPawnMovement::ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotationQuat)
{
	bPositionCorrected |= Super::ResolvePenetrationImpl(Adjustment, Hit, NewRotationQuat);
	return bPositionCorrected;
}

void UCustomFloatingPawnMovement::CheckGround()
{
	if (!UpdatedComponent)
	{
		bIsOnGround = false;
		bIsOnSteepSlope = false;
		return;
	}

	const FVector StartLocation = UpdatedComponent->GetComponentLocation();
	const FVector EndLocation = StartLocation - FVector(0.f, 0.f, GroundTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PawnOwner);

	FHitResult HitResult;
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		StartLocation,
		EndLocation,
		ECC_Visibility,
		QueryParams
	);

	// Draw debug line DELETAR \/
	DrawDebugLine(
		GetWorld(),
		StartLocation,
		EndLocation,
		bHit ? FColor::Green : FColor::Red,
		true,  // Persistent lines
		-1.0f, // Lifetime (-1 means persistent until manually cleared)
		0,     // Depth priority
		2.0f   // Thickness
	); // DELETAR /\

	if (bHit && HitResult.bBlockingHit)
	{
		LastGroundHit = HitResult;
		bIsOnGround = true;

		// Calculate the angle of the slope
		const FVector UpVector = FVector::UpVector;
		const float SlopeAngle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(HitResult.Normal, UpVector)));

		// Check if the slope is too steep
		bIsOnSteepSlope = (SlopeAngle > MaxWalkableAngle);

		// If on ground and not moving down fast, zero out downward velocity
		if (!bIsOnSteepSlope && Velocity.Z < 0.f)
		{
			Velocity.Z = 0.f;
		}
	}
	else
	{
		bIsOnGround = false;
		bIsOnSteepSlope = false;
	}
}

void UCustomFloatingPawnMovement::ApplyGroundFriction(float DeltaTime)
{
	if (!bIsOnGround || Velocity.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Get horizontal velocity (X and Y only)
	FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.f);
	const float HorizontalSpeed = HorizontalVelocity.Size();

	if (HorizontalSpeed > KINDA_SMALL_NUMBER)
	{
		// Choose friction based on slope
		float FrictionToApply = bIsOnSteepSlope ? SlopeFriction : GroundFriction;

		// On steep slopes, apply friction in the direction opposite to the slope's pull
		if (bIsOnSteepSlope)
		{
			// Project the slope normal onto the horizontal plane to get slide direction
			FVector SlopeDirection = FVector(LastGroundHit.Normal.X, LastGroundHit.Normal.Y, 0.f);
			if (SlopeDirection.SizeSquared() > KINDA_SMALL_NUMBER)
			{
				SlopeDirection.Normalize();
				
				// Apply friction against the sliding direction
				const float SlideSpeed = FVector::DotProduct(HorizontalVelocity, SlopeDirection);
				if (FMath::Abs(SlideSpeed) > KINDA_SMALL_NUMBER)
				{
					const FVector FrictionForce = -SlopeDirection * SlideSpeed * FrictionToApply * DeltaTime;
					HorizontalVelocity += FrictionForce;
				}
			}
		}
		else
		{
			// On flat ground, apply friction to all horizontal movement
			const float NewHorizontalSpeed = FMath::Max(0.f, HorizontalSpeed - FrictionToApply * DeltaTime * 100.f);
			HorizontalVelocity = HorizontalVelocity.GetSafeNormal() * NewHorizontalSpeed;
		}

		// Update velocity with modified horizontal component
		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
	}
}
