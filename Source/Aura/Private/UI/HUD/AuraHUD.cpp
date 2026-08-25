


#include "UI/HUD/AuraHUD.h"

#include "UI/Widget/AuraUserWidget.h"
#include "UI/WidgetController/AttributeMenuWidgetController.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"

UOverlayWidgetController* AAuraHUD::GetOverlayWidgetController(const FWidgetControllerParams& WCParams)
{
	if (!OverlayWidgetController)
	{
		OverlayWidgetController = NewObject<UOverlayWidgetController>(this, OverlayWidgetControllerClass);
		OverlayWidgetController->SetWidgetControllerParams(WCParams);
		OverlayWidgetController->BindCallbacksToDependencies();
	}
	return OverlayWidgetController;
}

UAttributeMenuWidgetController* AAuraHUD::GetAttributeMenuWidgetController(const FWidgetControllerParams& WCParams)
{
	if (!AttributeMenuWidgetController)
	{
		AttributeMenuWidgetController = NewObject<UAttributeMenuWidgetController>(this, AttributeMenuWidgetControllerClass);
		AttributeMenuWidgetController->SetWidgetControllerParams(WCParams);
		AttributeMenuWidgetController->BindCallbacksToDependencies();
	}
	return AttributeMenuWidgetController;
}

USpellMenuWidgetController* AAuraHUD::GetSpellMenuWidgetController(const FWidgetControllerParams& WCParams)
{
	if (!SpellMenuAuraWidgetController)
	{
		SpellMenuAuraWidgetController = NewObject<USpellMenuWidgetController>(this, SpellMenuAuraWidgetControllerClass);
		SpellMenuAuraWidgetController->SetWidgetControllerParams(WCParams);
		SpellMenuAuraWidgetController->BindCallbacksToDependencies();
	}
	return SpellMenuAuraWidgetController;
}

void AAuraHUD::InitOverlay(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS)
{
	checkf(OverlayWidgetClass, TEXT("OverlayWidgetClass uninitialized, please fill out BP_AuraHUD"));
	checkf(OverlayWidgetControllerClass, TEXT("OverlayWidgetControllerClass uninitialized, please fill out BP_AuraHUD"));

	if (IsValid(OverlayWidget))
	{
		const FWidgetControllerParams WidgetControllerParams(PC, PS, ASC, AS);
		UOverlayWidgetController* WidgetController = GetOverlayWidgetController(WidgetControllerParams);
		OverlayWidget->SetWidgetController(WidgetController);
		WidgetController->BroadcastInitialValues();
		return;
	}
	
	UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), OverlayWidgetClass);
	OverlayWidget = Cast<UAuraUserWidget>(Widget);
	
	const FWidgetControllerParams WidgetControllerParams(PC, PS, ASC, AS);
	UOverlayWidgetController* WidgetController = GetOverlayWidgetController(WidgetControllerParams);
	
	OverlayWidget->SetWidgetController(WidgetController);
	// Let GameMode restore the saved PlayerState and vitals before the first
	// UI broadcast. The controller also skips this broadcast while restoring.
	const TWeakObjectPtr<UOverlayWidgetController> WeakWidgetController(WidgetController);
	GetWorld()->GetTimerManager().SetTimerForNextTick([WeakWidgetController]()
	{
		if (UOverlayWidgetController* Controller = WeakWidgetController.Get())
		{
			Controller->BroadcastInitialValues();
		}
	});
	
	Widget->AddToViewport();
}

void AAuraHUD::BeginPlay()
{
	Super::BeginPlay();
}
