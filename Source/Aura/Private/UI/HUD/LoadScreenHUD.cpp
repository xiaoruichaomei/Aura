


#include "UI/HUD/LoadScreenHUD.h"

#include "UI/ViewModel/MVVM_LoadScreen.h"
#include "UI/Widget/LoadScreenWidget.h"

void ALoadScreenHUD::BeginPlay()
{
	Super::BeginPlay();
	
	LoadScreenViewModel = NewObject<UMVVM_LoadScreen>(this, LoadScreenViewModelClass);
	LoadScreenViewModel->InitializedLoadSlots();
	
	LoadScreenWidget = CreateWidget<ULoadScreenWidget>(GetWorld(), LoadScreenWidgetClass);
	LoadScreenWidget->AddToViewport();
	LoadScreenWidget->BlueprintInitializeWidget();
	
	// SaveGame slots are local/server-owned data. A network client must not
	// read them through its local LoadMenu GameMode, which may not have the
	// configured SaveGame class and is not authoritative anyway.
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
	{
		LoadScreenViewModel->LoadData();
	}
}
