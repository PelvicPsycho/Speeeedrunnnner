// Fill out your copyright notice in the Description page of Project Settings.


#include "BolaAndante.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"     // Necessário para o Subsystem
#include "EnhancedInputComponent.h"      // Necessário para o Cast
#include "Components/InputComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/LocalPlayer.h" // ULocalPlayer também precisa ser conhecido

// INCLUA OS CABEÇALHOS DOS COMPONENTES QUE VOCÊ VAI USAR
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h" // Para encontrar assets
#include "GameFramework/FloatingPawnMovement.h"

// Sets default values
ABolaAndante::ABolaAndante()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	BolaMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshDaBola"));
	RootComponent = BolaMesh;

	NossoMovimento = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("NossoMovimento"));
}

// Called when the game starts or when spawned
void ABolaAndante::BeginPlay()
{
	Super::BeginPlay();

	// --- 1. Adicionar o Mapping Context ---
    
	// Garante que temos um PlayerController
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		// Pega o Subsystem de Input Local do Jogador
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			// Adiciona nosso IMC
			// Certifique-se de que PlayerInputMappingContext não é nulo!
			if (PlayerInputMappingContext)
			{
				Subsystem->AddMappingContext(PlayerInputMappingContext, 0);
			}
		}
	}
}

// Called every frame
void ABolaAndante::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
//void ABolaAndante::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
//{
//	Super::SetupPlayerInputComponent(PlayerInputComponent);

//}

void ABolaAndante::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // --- 2. Fazer o Bind (ligação) das Ações ---

    // Faz o cast do componente de Input para o Enhanced Input Component
    if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        // Verifica se a nossa Ação de Mover (IA_Move) existe
        if (MoveAction)
        {
            // Binda a ação.
            // - ETriggerEvent::Triggered: Dispara a cada frame que a tecla estiver pressionada
            // - this: O objeto que contém a função
            // - &AMyCharacter::Move: A função que queremos chamar
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABolaAndante::Move);
        }

        // Exemplo de Bind para o Look (mouse)
        if (LookAction)
        {
            EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABolaAndante::Look);
        }
    }
}


// --- 3. Implementar as Funções de Ação ---

void ABolaAndante::Move(const FInputActionValue& Value)
{
    // O 'Value' é o que vem do Input (no caso do IA_Move, um Vector2D)
    const FVector2D MovementVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        // --- Lógica de Movimento Padrão (igual ao template) ---
        
        // Pega a rotação do controle (para saber para onde é "frente")
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawRotation(0, Rotation.Yaw, 0);

        // Pega o vetor "Frente" (baseado no Yaw do controle)
        const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

        // Pega o vetor "Direita" (baseado no Yaw do controle)
        const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        // Adiciona o movimento
        AddMovementInput(ForwardDirection, MovementVector.Y); // W/S
        AddMovementInput(RightDirection, MovementVector.X);   // A/D
    }
}

void ABolaAndante::Look(const FInputActionValue& Value)
{
    // O 'Value' aqui também é um Vector2D (do mouse)
    const FVector2D LookAxisVector = Value.Get<FVector2D>();

    if (Controller != nullptr)
    {
        // Adiciona Yaw (olhar esquerda/direita)
        AddControllerYawInput(LookAxisVector.X);
        
        // Adiciona Pitch (olhar cima/baixo)
        AddControllerPitchInput(LookAxisVector.Y);
    }
}
