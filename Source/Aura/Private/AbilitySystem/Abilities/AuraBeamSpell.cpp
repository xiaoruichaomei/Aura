// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/AuraBeamSpell.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "Aura/Aura.h"
#include "CollisionShape.h"
#include "AuraGameplayTags.h"
#include "Character/BaseCharacter.h"
#include "Interface/CombatInterface.h"
#include "Player/AuraPlayerController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

UAuraBeamSpell::UAuraBeamSpell()
{
	// 蒙太奇/异步任务（PlayMontageAndWait、TargetDataUnderMouse）需要按次实例化
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
}

void UAuraBeamSpell::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// 施法期间阻塞其他技能（带 Abilities 标签的技能不能再激活）
	if (ActivationBlockedTags.IsEmpty())
	{
		ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Abilities")));
	}

	// 阻塞玩家移动（记录 ASC，供 EndAbility 可靠移除）
	if (CurrentActorInfo)
	{
		OwnerASC = CurrentActorInfo->AbilitySystemComponent.Get();
	}
	if (UAbilitySystemComponent* ASC = OwnerASC)
	{
		ASC->AddLooseGameplayTag(FAuraGameplayTags::Get().Player_Block);
		bBlockTagAdded = true;
	}

	// 安全网：若 2 秒内未开始施法（目标数据没到等），强制结束，防止永久卡住
	bCastStarted = false;
	UWorld* World = GetWorld();
	if (!World && OwnerActor) { World = OwnerActor->GetWorld(); }
	if (!World && OwnerASC)   { World = OwnerASC->GetWorld(); }
	if (World)
	{
		World->GetTimerManager().SetTimer(CastSafetyTimer, this, &UAuraBeamSpell::OnCastSafetyTimeout, 2.f, false);
	}

	// 蓝图的空地分支可能在此调用链中同步 EndAbility，初始化必须全部在 Super 之前完成
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UAuraBeamSpell::OnCastSafetyTimeout()
{
	if (!bCastStarted && IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UAuraBeamSpell::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (CachedChannelingMontage && OwnerASC)
	{
		OwnerASC->StopMontageIfCurrent(*CachedChannelingMontage, 0.1f);
	}

	// 解除移动阻塞（用记录的 OwnerASC，更可靠）
	if (bBlockTagAdded)
	{
		if (UAbilitySystemComponent* ASC = OwnerASC ? OwnerASC.Get() : GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(FAuraGameplayTags::Get().Player_Block);
		}
		bBlockTagAdded = false;
	}

	// 技能结束时移除目标身上的电击 GC（停止光束/循环音）
	RemoveShockLoopCues();

	// 停止持续伤害定时器
	if (bDamageTimerRunning)
	{
		bDamageTimerRunning = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(DamageTimer);
		}
	}
	if (bBeamUpdateTimerRunning)
	{
		bBeamUpdateTimerRunning = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BeamUpdateTimer);
		}
	}

	// 清除施法安全定时器
	if (CastSafetyTimer.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(CastSafetyTimer);
		}
	}

	// 施放结束后进入冷却：期间 CanActivateAbility 返回 false，按住不会快速重激活
	if (UAbilitySystemComponent* ASC = OwnerASC ? OwnerASC.Get() : GetAbilitySystemComponentFromActorInfo())
	{
		const float CD = CooldownDuration.GetValueAtLevel(GetAbilityLevel());
		if (CD > 0.f)
		{
			const FGameplayTag CooldownTag = FAuraGameplayTags::Get().Cooldown_Lightning_Electrocute;
			ASC->AddLooseGameplayTag(CooldownTag);
			if (UWorld* World = GetWorld())
			{
				FTimerHandle CooldownTimer;
				World->GetTimerManager().SetTimer(CooldownTimer, [ASC, CooldownTag]()
				{
					if (IsValid(ASC))
					{
						ASC->RemoveLooseGameplayTag(CooldownTag);
					}
				}, CD, false);
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UAuraBeamSpell::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	// 冷却中不能释放
	if (const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		if (ASC->HasMatchingGameplayTag(FAuraGameplayTags::Get().Cooldown_Lightning_Electrocute))
		{
			return false;
		}
	}
	return true;
}

void UAuraBeamSpell::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 引导释放：按住时保持施法（循环蒙太奇），松开输入结束技能
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UAuraBeamSpell::StoreOwnerVariables()
{
	if (CurrentActorInfo)
	{
		OwnerActor = CurrentActorInfo->AvatarActor.Get();
		OwnerASC = CurrentActorInfo->AbilitySystemComponent.Get();
	}
}

void UAuraBeamSpell::StoreSingleTarget(const FVector& InTargetLocation)
{
	TargetLocation = InTargetLocation;
}

bool UAuraBeamSpell::TraceFirstTarget(const FVector& BeamTargetLocation)
{
	if (!OwnerActor)
	{
		StoreOwnerVariables();
	}
	if (!OwnerActor)
	{
		return false;
	}

	// 从施法者法杖（TipSocket，胸口高度）向目标方向线检测，找第一个敌人目标（用 ECC_Beam 通道，角色网格已响应 Block）。
	// 注意不能用左手 socket——玩家 LeftHandTipSocketName 未配置，GetSocketLocation 返回脚底高度会从敌人下方穿过。
	const FVector Start = ICombatInterface::Execute_GetCombatSockettLocation(OwnerActor, FAuraGameplayTags::Get().CombatSocket_Weapon);
	// Cursor hits are usually on the ground. Trace horizontally at weapon height so the
	// beam cannot pass below a character whose capsule lies between caster and cursor.
	const FVector End(BeamTargetLocation.X, BeamTargetLocation.Y, Start.Z);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor); // 忽略施法者自身，避免线检测命中自己的网格

	TArray<FHitResult> HitResults;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	GetWorld()->SweepMultiByObjectType(HitResults, Start, End, FQuat::Identity, ObjectQueryParams, FCollisionShape::MakeSphere(60.f), QueryParams);

	AActor* ClosestEnemy = nullptr;
	FVector ClosestImpactPoint = BeamTargetLocation;
	float ClosestDistanceSquared = MAX_FLT;
	for (const FHitResult& HitResult : HitResults)
	{
		AActor* HitActor = HitResult.GetActor();
		// 只电敌人：存活 + 战斗单位 + 与施法者为敌
		if (IsValid(HitActor) && HitActor->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(HitActor) && UAuraAbilitySystemLibrary::IsNotFriend(HitActor, OwnerActor))
		{
			const float DistanceSquared = FVector::DistSquared(Start, HitActor->GetActorLocation());
			if (DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				ClosestEnemy = HitActor;
				ClosestImpactPoint = HitResult.ImpactPoint;
			}
		}
	}
	if (ClosestEnemy)
	{
		TargetActor = ClosestEnemy;
		TargetLocation = ClosestImpactPoint;
		return true;
	}

	// 没命中敌人
	TargetActor = nullptr;
	TargetLocation = BeamTargetLocation;
	return false;
}

void UAuraBeamSpell::CauseBeamDamage()
{
	if (IsValid(TargetActor))
	{
		CauseDamage(TargetActor);
	}
}

void UAuraBeamSpell::StoreChainTargets()
{
	AdditionalTargets.Empty();

	if (!OwnerActor)
	{
		StoreOwnerVariables();
	}
	if (!OwnerActor || !IsValid(TargetActor))
	{
		return;
	}

	// 附加目标数 = min(技能等级, MaxChainTargets)：1级传导1次（共2个敌人），5级以上5次
	const int32 AdditionalCount = FMath::Max(0, FMath::Min(GetAbilityLevel(), MaxChainTargets));

	TArray<AActor*> AllTargets;
	AllTargets.Add(TargetActor);
	AActor* CurrentPoint = TargetActor;

	// 从当前目标出发，找最近的下一个敌人（链式：1→2→3…）
	for (int32 i = 0; i < AdditionalCount; ++i)
	{
		TArray<AActor*> ActorsToIgnoreLocal;
		ActorsToIgnoreLocal.Add(OwnerActor);
		ActorsToIgnoreLocal.Append(AllTargets);

		TArray<AActor*> Candidates;
		UAuraAbilitySystemLibrary::GetLivePlayersWithinRadius(this, Candidates, ActorsToIgnoreLocal, AdditionalTargetRadius, CurrentPoint->GetActorLocation());

		AActor* Next = nullptr;
		float BestDist = MAX_FLT;
		for (AActor* Candidate : Candidates)
		{
			if (!AllTargets.Contains(Candidate) && UAuraAbilitySystemLibrary::IsNotFriend(Candidate, OwnerActor))
			{
				const float Dist = FVector::DistSquared(Candidate->GetActorLocation(), CurrentPoint->GetActorLocation());
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Next = Candidate;
				}
			}
		}
		if (Next)
		{
			AllTargets.Add(Next);
			AdditionalTargets.Add(Next);
			CurrentPoint = Next;
		}
		else
		{
			break; // 附近没有更多敌人
		}
	}
}

void UAuraBeamSpell::ApplyBeamDamage()
{
	bCastStarted = true; // 已开始施法（安全网据此不再强制结束）

	// Resolve the latest cursor target before creating the first cue. This prevents
	// an empty-ground beam from appearing briefly before it snaps to an enemy.
	if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
	{
		if (AAuraPlayerController* PlayerController = Cast<AAuraPlayerController>(CurrentActorInfo->PlayerController.Get()))
		{
			FVector CursorLocation;
			if (PlayerController->GetBeamCursorLocation(CursorLocation))
			{
				PlayerController->SubmitBeamCursorLocation(CursorLocation);
				TraceFirstTarget(CursorLocation);
				StoreChainTargets();
			}
		}
	}
	else
	{
		// The server already received the activation target through TargetDataUnderMouse.
		TraceFirstTarget(TargetLocation);
		StoreChainTargets();
	}

	if (IsValid(TargetActor))
	{
		DamageChainTargets();
	}
	UpdateOwnerFacing(TargetLocation);

	AddShockLoopCue(); // 挂光束（先伤害再快照，起点更接近击退后的位置）

	// Keep both timers running even when the cast starts on empty ground. Moving the
	// cursor onto an enemy later must begin targeting and dealing damage immediately.
	if (!bDamageTimerRunning && DamageInterval > 0.f)
	{
		bDamageTimerRunning = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(DamageTimer, this, &UAuraBeamSpell::DamageChainTargets, DamageInterval, true);
		}
	}
	if (!bBeamUpdateTimerRunning && BeamUpdateInterval > 0.f)
	{
		bBeamUpdateTimerRunning = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(BeamUpdateTimer, this, &UAuraBeamSpell::UpdateBeamFromCursor, BeamUpdateInterval, true);
		}
	}
}

void UAuraBeamSpell::UpdateBeamFromCursor()
{
	if (!IsActive() || !CurrentActorInfo)
	{
		return;
	}
	EnsureChannelingMontage();

	AAuraPlayerController* PlayerController = Cast<AAuraPlayerController>(CurrentActorInfo->PlayerController.Get());
	if (!PlayerController)
	{
		return;
	}

	FVector CursorLocation;
	if (!PlayerController->GetBeamCursorLocation(CursorLocation))
	{
		return;
	}

	if (CurrentActorInfo->IsLocallyControlled())
	{
		PlayerController->SubmitBeamCursorLocation(CursorLocation);
	}
	RefreshBeamTarget(CursorLocation);
	UpdateOwnerFacing(CursorLocation);
}

void UAuraBeamSpell::UpdateOwnerFacing(const FVector& CursorLocation)
{
	if (!IsValid(OwnerActor))
	{
		return;
	}

	const FVector AimLocation = IsValid(TargetActor) ? TargetActor->GetActorLocation() : CursorLocation;
	FVector AimDirection = AimLocation - OwnerActor->GetActorLocation();
	AimDirection.Z = 0.f;
	if (AimDirection.IsNearlyZero())
	{
		return;
	}

	const FRotator CurrentRotation = OwnerActor->GetActorRotation();
	const FRotator DesiredRotation(0.f, AimDirection.Rotation().Yaw, 0.f);
	const FRotator NewRotation = BeamTurnSpeed > 0.f
		? FMath::RInterpTo(CurrentRotation, DesiredRotation, BeamUpdateInterval, BeamTurnSpeed)
		: DesiredRotation;
	OwnerActor->SetActorRotation(NewRotation);
}

void UAuraBeamSpell::EnsureChannelingMontage()
{
	const ABaseCharacter* SourceCharacter = Cast<ABaseCharacter>(OwnerActor);
	UAnimInstance* AnimInstance = SourceCharacter && SourceCharacter->GetMesh() ? SourceCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !OwnerASC)
	{
		return;
	}

	if (UAnimMontage* ActiveMontage = AnimInstance->GetCurrentActiveMontage())
	{
		// Ignore the intro and temporary hit reactions. Only the channeling montage loops.
		if (ActiveMontage->GetName().Contains(TEXT("Loop"), ESearchCase::IgnoreCase))
		{
			if (CachedChannelingMontage != ActiveMontage)
			{
				CachedChannelingMontage = ActiveMontage;
				bChannelingMontageLoopConfigured = false;
			}
			const FName LoopSection(TEXT("Loop"));
			CachedChannelingSection = ActiveMontage->GetSectionIndex(LoopSection) != INDEX_NONE
				? LoopSection
				: AnimInstance->Montage_GetCurrentSection(ActiveMontage);
			if (!bChannelingMontageLoopConfigured && !CachedChannelingSection.IsNone())
			{
				OwnerASC->CurrentMontageSetNextSectionName(CachedChannelingSection, CachedChannelingSection);
				bChannelingMontageLoopConfigured = true;
			}
		}
		return;
	}

	if (CachedChannelingMontage)
	{
		OwnerASC->PlayMontage(this, CurrentActivationInfo, CachedChannelingMontage, 1.f, CachedChannelingSection);
		if (!CachedChannelingSection.IsNone())
		{
			OwnerASC->CurrentMontageSetNextSectionName(CachedChannelingSection, CachedChannelingSection);
			bChannelingMontageLoopConfigured = true;
		}
	}
}

void UAuraBeamSpell::RefreshBeamTarget(const FVector& CursorLocation)
{
	AActor* OldPrimaryTarget = TargetActor.Get();
	const TArray<TObjectPtr<AActor>> OldChainTargets = AdditionalTargets;

	TraceFirstTarget(CursorLocation);
	if (IsValid(TargetActor))
	{
		StoreChainTargets();
	}
	else
	{
		AdditionalTargets.Empty();
	}

	if (!HaveSameCueTargets(OldPrimaryTarget, OldChainTargets))
	{
		RemoveShockLoopCuesForTargets(OldPrimaryTarget, OldChainTargets);
		AddShockLoopCue();
	}
}

bool UAuraBeamSpell::HaveSameCueTargets(AActor* OldPrimaryTarget, const TArray<TObjectPtr<AActor>>& OldChainTargets) const
{
	if (OldPrimaryTarget != TargetActor.Get() || OldChainTargets.Num() != AdditionalTargets.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < OldChainTargets.Num(); ++Index)
	{
		if (OldChainTargets[Index] != AdditionalTargets[Index])
		{
			return false;
		}
	}
	return true;
}

void UAuraBeamSpell::DamageChainTargets()
{
	if (IsValid(TargetActor))
	{
		CauseDamage(TargetActor);
	}
	for (const TObjectPtr<AActor>& Target : AdditionalTargets)
	{
		if (IsValid(Target))
		{
			CauseDamage(Target);
		}
	}
}

void UAuraBeamSpell::AddShockLoopCue()
{
	TArray<AActor*> AllTargets;
	if (IsValid(TargetActor))
	{
		AllTargets.Add(TargetActor);
	}
	for (const TObjectPtr<AActor>& Target : AdditionalTargets)
	{
		if (IsValid(Target))
		{
			AllTargets.Add(Target);
		}
	}

	// 施法者上下文（instigator = 施法者角色）
	const FGameplayEffectContextHandle CueContext = GetContextFromOwner(FGameplayAbilityTargetDataHandle());

	// 无目标：在施法者身上挂一个光束 cue，从法杖朝鼠标方向（TargetLocation）
	if (AllTargets.Num() == 0)
	{
		if (UAbilitySystemComponent* CasterASC = OwnerASC ? OwnerASC.Get() : GetAbilitySystemComponentFromActorInfo())
		{
			FGameplayCueParameters CueParams(CueContext);
			CueParams.Location = TargetLocation; // 光束终点 = 鼠标方向
			if (const ABaseCharacter* SourceChar = Cast<ABaseCharacter>(OwnerActor))
			{
				CueParams.TargetAttachComponent = SourceChar->GetWeapon(); // 附着法杖
			}
			CasterASC->AddGameplayCue(FAuraGameplayTags::Get().GameplayCue_Electrocute, CueParams);
		}
		return;
	}

	// 链式光束：起点 = 施法者法杖(第一条) 或 上一个目标(后续)，终点 = 当前目标
	FVector PrevPoint = FVector::ZeroVector;
	AActor* PrevTargetActor = nullptr; // 链式光束起点要跟随的上一个目标（随目标移动每帧刷新）
	if (OwnerActor && OwnerActor->Implements<UCombatInterface>())
	{
		PrevPoint = ICombatInterface::Execute_GetCombatSockettLocation(OwnerActor, FAuraGameplayTags::Get().CombatSocket_Weapon);
	}

	for (int32 i = 0; i < AllTargets.Num(); ++i)
	{
		AActor* Target = AllTargets[i];
		if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
		{
			FGameplayCueParameters CueParams(CueContext);
			CueParams.Location = PrevPoint; // 这条光束的起点
			if (i == 0)
			{
				// 第一条光束附着到施法者法杖 TipSocket
				if (const ABaseCharacter* SourceChar = Cast<ABaseCharacter>(OwnerActor))
				{
					CueParams.TargetAttachComponent = SourceChar->GetWeapon();
				}
			}
			else
			{
				// 链式光束起点跟随上一个目标：把上一个目标通过 SourceObject 传给 GC，GC 每帧刷新起点位置
				CueParams.SourceObject = PrevTargetActor;
			}
			TargetASC->AddGameplayCue(FAuraGameplayTags::Get().GameplayCue_Electrocute, CueParams);
			PrevTargetActor = Target;
			PrevPoint = Target->GetActorLocation(); // 下一条从当前目标出发
		}
	}
}

void UAuraBeamSpell::RemoveShockLoopCues()
{
	RemoveShockLoopCuesForTargets(TargetActor.Get(), AdditionalTargets);
}

void UAuraBeamSpell::RemoveShockLoopCuesForTargets(AActor* PrimaryTarget, const TArray<TObjectPtr<AActor>>& ChainTargets)
{
	TArray<AActor*> AllTargets;
	if (IsValid(PrimaryTarget))
	{
		AllTargets.Add(PrimaryTarget);
	}
	for (const TObjectPtr<AActor>& Target : ChainTargets)
	{
		if (IsValid(Target))
		{
			AllTargets.Add(Target);
		}
	}

	// 无目标时 cue 挂在施法者身上
	if (AllTargets.Num() == 0)
	{
		if (UAbilitySystemComponent* CasterASC = OwnerASC ? OwnerASC.Get() : GetAbilitySystemComponentFromActorInfo())
		{
			CasterASC->RemoveGameplayCue(FAuraGameplayTags::Get().GameplayCue_Electrocute);
		}
		return;
	}

	for (AActor* Target : AllTargets)
	{
		if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
		{
			TargetASC->RemoveGameplayCue(FAuraGameplayTags::Get().GameplayCue_Electrocute);
		}
	}
}
