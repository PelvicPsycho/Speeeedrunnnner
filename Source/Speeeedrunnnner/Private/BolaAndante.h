// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h" // Necessário para FInputActionValue
#include "BolaAndante.generated.h"

// Declarações "Forward" para não precisar incluir os .h aqui
class UInputMappingContext;
class UInputAction;

class UStaticMeshComponent; //precisa avisar de sei la o q e pq
class UFloatingPawnMovement; // 1. "Aviso" que vamos usar esta classe

UCLASS()
class ABolaAndante : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ABolaAndante();

	// ADICIONE ISTO:
	// UPROPERTY faz com que a Unreal consiga "ver" esta variável.
	// VisibleAnywhere significa que você poderá vê-la no editor de Blueprints.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Componentes")
	UStaticMeshComponent* BolaMesh;

	// 2. DECLARE O NOVO COMPONENTE
	//    Use EditAnywhere para podermos mudar a velocidade no Blueprint
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Componentis")
	UFloatingPawnMovement* NossoMovimento;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
protected:
	/** * Esta é a UPROPERTY para linkar seu asset 
	 * IA_Player_InputMappingContext no Editor
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerInputMappingContext;

	/** UPROPERTY para linkar seu asset IA_Move */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** UPROPERTY para linkar seu asset IA_Look (mouse) - Opcional */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** * Função que será chamada pelo BindAction de Movimento.
	 * O parâmetro FInputActionValue é obrigatório.
	 */
	void Move(const FInputActionValue& Value);

	/** Função para o Look (mouse) - Opcional */
	void Look(const FInputActionValue& Value);

	
//public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
//	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

};
