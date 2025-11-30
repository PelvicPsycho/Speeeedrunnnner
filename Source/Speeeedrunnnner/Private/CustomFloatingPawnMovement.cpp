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
	AirControl = 0.5f; // Valor sugerido para testar
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
			if (!bPositionCorrected && bIsOnGround)
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
    
    // 1. Separar a velocidade Vertical da Horizontal para proteger a gravidade
    float OldVelocityZ = Velocity.Z;
    FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.f);
    float CurrentHorizontalSpeed = HorizontalVelocity.Size();

    // ==========================================
    // LÓGICA DE CHÃO
    // ==========================================
    if (bIsOnGround)
    {
        // Se tem input, aplica o Turning Boost (curva rápida)
        if (AnalogInputModifier > 0.f && CurrentHorizontalSpeed > 0.f)
        {
            const float TimeScale = FMath::Clamp(DeltaTime * TurningBoost, 0.f, 1.f);
            // Essa formula mágica gira o vetor de velocidade em direção ao Input sem perder magnitude
            HorizontalVelocity = HorizontalVelocity + (ControlAcceleration * CurrentHorizontalSpeed - HorizontalVelocity) * TimeScale;
        }

        // Deceleração (Fricção) quando solta o controle
        if (AnalogInputModifier == 0.f && CurrentHorizontalSpeed > 0.f)
        {
            const float NewHorizontalSpeed = FMath::Max(CurrentHorizontalSpeed - FMath::Abs(Deceleration) * DeltaTime, 0.f);
            HorizontalVelocity = HorizontalVelocity.GetSafeNormal() * NewHorizontalSpeed;
        }

        // Aceleração Padrão
        const bool bExceedingMaxSpeed = CurrentHorizontalSpeed > MaxPawnSpeed;
        const float TargetMaxSpeed = bExceedingMaxSpeed ? CurrentHorizontalSpeed : MaxPawnSpeed;
        
        HorizontalVelocity += ControlAcceleration * FMath::Abs(Acceleration) * DeltaTime;
        HorizontalVelocity = HorizontalVelocity.GetClampedToMaxSize(TargetMaxSpeed);
    }
    // ==========================================
    // LÓGICA DE AR (AQUI ESTÁ A CORREÇÃO)
    // ==========================================
    else 
    {
        if (AnalogInputModifier > 0.f)
        {
            // 1. STEERING NO AR (O PULO DO GATO)
            // Usamos o AirControl para definir o quão forte conseguimos "girar" o vetor no ar.
            // Se AirControl for 1.0, vira igual no chão. Se for 0.1, vira muito pouco.
            if (CurrentHorizontalSpeed > 0.f)
            {
                // Multiplicamos o TurningBoost pelo AirControl
                const float AirTurnScale = FMath::Clamp(DeltaTime * TurningBoost * AirControl, 0.f, 1.f);
                HorizontalVelocity = HorizontalVelocity + (ControlAcceleration * CurrentHorizontalSpeed - HorizontalVelocity) * AirTurnScale;
            }

            // 2. ACELERAÇÃO NO AR
            // Adiciona velocidade na direção do input (para ganhar velocidade se estiver parado ou lento)
            FVector AirAccel = ControlAcceleration * FMath::Abs(Acceleration) * AirControl * DeltaTime;
            HorizontalVelocity += AirAccel;

            // 3. LIMITAR VELOCIDADE NO AR (OPCIONAL)
            // Isso impede que ele acelere infinitamente, mas respeita se ele já estava rápido (ex: lançado por uma mola)
            const float AirMaxSpeed = MaxSpeed; 
            if (HorizontalVelocity.Size() > AirMaxSpeed)
            {
                // Se já estamos rápidos, só clampamos se tentarmos acelerar AINDA MAIS.
                // Caso contrário, mantemos a velocidade atual (preserva momentum de launch pads)
                float SpeedToClamp = FMath::Max(CurrentHorizontalSpeed, AirMaxSpeed);
                
                // Se o input estiver oposto à velocidade, permitimos reduzir a velocidade (freio aéreo)
                // Se não quiser freio aéreo, remova a lógica abaixo e use apenas o GetClampedToMaxSize normal.
                bool bMovingAgainstInput = (FVector::DotProduct(HorizontalVelocity.GetSafeNormal(), ControlAcceleration) < -0.2f);
                if(bMovingAgainstInput)
                {
                     // Permite desacelerar no ar
                     SpeedToClamp = AirMaxSpeed;
                }

                HorizontalVelocity = HorizontalVelocity.GetClampedToMaxSize(SpeedToClamp);
            }
        }
        // Nota: Não aplicamos Deceleration (fricção) no ar quando solta o controle, 
        // para manter o arco do pulo natural.
    }

    // Reconstrói o vetor final com a Gravidade original
    Velocity = FVector(HorizontalVelocity.X, HorizontalVelocity.Y, OldVelocityZ);

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
//	DrawDebugLine(
//		GetWorld(),
//		StartLocation,
//		EndLocation,
//		bHit ? FColor::Green : FColor::Red,
//		true,  // Persistent lines
//		-1.0f, // Lifetime (-1 means persistent until manually cleared)
//		0,     // Depth priority
//		2.0f   // Thickness
//	); // DELETAR /\

	if (bHit && HitResult.bBlockingHit)
	{
		LastGroundHit = HitResult;
		bIsOnGround = true;

		// Calculate the angle of the slope RELATIVE TO GRAVITY DIRECTION
		// Use DownVector when inverted, UpVector when normal
		const FVector GravityUpVector = GravityScale < 0.f ? FVector::DownVector : FVector::UpVector;
		const float SlopeAngle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(HitResult.Normal, GravityUpVector)));

		// PRINT SLOPE ANGLE
//		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, FString::Printf(TEXT("Slope Angle: %.2f degrees (GravityScale: %.1f)"), SlopeAngle, GravityScale));

		// Check if the slope is too steep
		bIsOnSteepSlope = (SlopeAngle > MaxWalkableAngle);

		// Zero out velocity in the direction of gravity when on ground
		if (!bIsOnSteepSlope)
		{
			if (GravityScale < 0.f)
			{
				// Inverted gravity: zero positive Z (moving away from ceiling)
				if (Velocity.Z > 0.f)
				{
					Velocity.Z = 0.f;
				}
			}
			else
			{
				// Normal gravity: zero negative Z (moving down into floor)
				if (Velocity.Z < 0.f)
				{
					Velocity.Z = 0.f;
				}
			}
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
