// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/AuraBeamSpell.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/GameplayEffects/AuraElectrocuteStunGameplayEffect.h"
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

	// 眩晕 GE 默认指向本模块的 C++ 类（GA_Electrocute 蓝图可覆盖）
	ElectrocuteStunChannelClass = UAuraElectrocuteStunChannel::StaticClass();
	ElectrocuteStunTailClass = UAuraElectrocuteStunTail::StaticClass();
	ManaCost = 10.f;
	ManaCostPerTick = 5.f;
	CooldownDuration = 5.f;
}

FString UAuraBeamSpell::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = Super::GetResolvedDescription(Level, AbilityInfo);
	const int32 ChainTargetCount = FMath::Max(0, FMath::Min(Level, MaxChainTargets));
	Description = Description.Replace(TEXT("{ChainTargets}"), *FString::FromInt(ChainTargetCount));
	Description = Description.Replace(TEXT("{TotalTargets}"), *FString::FromInt(ChainTargetCount + 1));
	Description = Description.Replace(TEXT("{TickInterval}"), *FString::SanitizeFloat(DamageInterval, 1));
	Description = Description.Replace(TEXT("{ManaPerTick}"), *FString::FromInt(FMath::RoundToInt(ManaCostPerTick.GetValueAtLevel(Level))));
	Description = Description.Replace(TEXT("{ChainRadius}"), *FString::FromInt(FMath::RoundToInt(AdditionalTargetRadius)));
	return Description;
}

int32 UAuraBeamSpell::GetManaCost(int32 Level) const
{
	return FMath::Max(0, FMath::RoundToInt(ManaCost.GetValueAtLevel(Level)));
}

float UAuraBeamSpell::GetCooldown(int32 Level) const
{
	return FMath::Max(0.f, CooldownDuration.GetValueAtLevel(Level));
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
	bManaCostPaid = false;
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
	// 技能结束：给所有被电击目标加 2 秒 Tail 眩晕 + 移除 Channel（先 Tail 后移除，Stun 标签计数不落 0）
	ClearStunState();

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

	// 只有真正开始引导的施放才进入冷却。空地取消或目标数据超时不消耗资源。
	// The active cooldown GE supplies both the cooldown tag and duration to the UI.
	// Mana remains custom because this ability consumes it once on start and every tick.
	if (bCastStarted && ActorInfo && ActorInfo->IsNetAuthority() && CooldownGameplayEffectClass)
	{
		ApplyCooldown(Handle, ActorInfo, ActivationInfo);
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

		const float RequiredMana = ManaCost.GetValueAtLevel(GetAbilityLevel());
		if (ASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) < RequiredMana)
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
	if (!bManaCostPaid)
	{
		if (!ConsumeMana(ManaCost.GetValueAtLevel(GetAbilityLevel())))
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
			return;
		}
		bManaCostPaid = true;
	}

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
		ApplyDamageToCurrentTargets();
	}
	// 初始命中：链内目标进入眩晕（Channel）
	SyncStunWithCurrentTargets();
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

	// 目标进入/离开链时同步眩晕状态：新进入 → Channel，离开 → Tail + 移除 Channel
	SyncStunWithCurrentTargets();
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
	if (!ConsumeMana(ManaCostPerTick.GetValueAtLevel(GetAbilityLevel())))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	ApplyDamageToCurrentTargets();
}

void UAuraBeamSpell::ApplyDamageToCurrentTargets()
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

bool UAuraBeamSpell::ConsumeMana(float Cost)
{
	if (Cost <= 0.f)
	{
		return true;
	}

	UAbilitySystemComponent* ASC = OwnerASC ? OwnerASC.Get() : GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) < Cost)
	{
		return false;
	}

	ASC->ApplyModToAttribute(UAuraAttributeSet::GetManaAttribute(), EGameplayModOp::Additive, -Cost);
	return true;
}

void UAuraBeamSpell::SyncStunWithCurrentTargets()
{
	// 眩晕 GE 只由服务器施加/移除：对 Minimal 复制的敌人 ASC，客户端预测应用/移除不可靠
	// （会触发 "RemoveActiveGameplayEffect called without Authority"）。标签由服务器复制驱动客户端表现。
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	// 当前链内有效目标
	TArray<AActor*> CurrentTargets;
	if (IsValid(TargetActor))
	{
		CurrentTargets.Add(TargetActor);
	}
	for (const TObjectPtr<AActor>& Target : AdditionalTargets)
	{
		if (IsValid(Target))
		{
			CurrentTargets.Add(Target);
		}
	}

	// 离开链（含死亡被刷新剔除）的目标：Tail + 移除 Channel
	TArray<AActor*> TargetsToRemove;
	for (const auto& Pair : StunChannelHandles)
	{
		AActor* ChanneledTarget = Pair.Key.Get();
		if (!ChanneledTarget || !CurrentTargets.Contains(ChanneledTarget))
		{
			TargetsToRemove.Add(ChanneledTarget);
		}
	}
	for (AActor* Target : TargetsToRemove)
	{
		RemoveStunFromTarget(Target);
	}

	// 进入链的新目标：施加 Channel
	for (AActor* Target : CurrentTargets)
	{
		if (!StunChannelHandles.Contains(Target))
		{
			ApplyStunToTarget(Target);
		}
	}
}

void UAuraBeamSpell::ApplyStunToTarget(AActor* Target)
{
	if (!IsValid(Target) || StunChannelHandles.Contains(Target))
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	UAbilitySystemComponent* CasterASC = GetAbilitySystemComponentFromActorInfo();
	if (!TargetASC || !CasterASC || !ElectrocuteStunChannelClass)
	{
		return;
	}

	const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();

	// 重新进入链：移除本技能之前施加的尚未结束的 Tail（避免 4 秒叠加）
	if (FActiveGameplayEffectHandle* TailHandle = StunTailHandles.Find(Target))
	{
		if (TailHandle->IsValid())
		{
			TargetASC->RemoveActiveGameplayEffect(*TailHandle);
		}
		StunTailHandles.Remove(Target);
	}

	// 施加无限时长 Channel：眩晕 + 标记来源电击
	const FGameplayEffectContextHandle Context = GetContextFromOwner(FGameplayAbilityTargetDataHandle());
	const FGameplayEffectSpecHandle Spec = CasterASC->MakeOutgoingSpec(ElectrocuteStunChannelClass, GetAbilityLevel(), Context);
	if (Spec.Data.IsValid())
	{
		FGameplayEffectSpec* MutableSpec = Spec.Data.Get();
		MutableSpec->DynamicGrantedTags.AddTag(Tags.Effects_Debuff_Stun);
		MutableSpec->DynamicGrantedTags.AddTag(Tags.Effects_Debuff_Electrocute);
		MutableSpec->AddDynamicAssetTag(Tags.Effects_Debuff); // 死亡时统一清理（RemoveActiveEffectsWithTags）
		const FActiveGameplayEffectHandle Handle = CasterASC->ApplyGameplayEffectSpecToTarget(*MutableSpec, TargetASC);
		StunChannelHandles.Add(Target, Handle);
	}
}

void UAuraBeamSpell::RemoveStunFromTarget(AActor* Target)
{
	FActiveGameplayEffectHandle* ChannelHandle = StunChannelHandles.Find(Target);
	if (!ChannelHandle)
	{
		return;
	}

	// 死亡目标：Channel 已被死亡时 Effects.Debuff 清理移除，不给尸体加 Tail，只清 handle
	const bool bTargetDead = !IsValid(Target) || (Target->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Target));

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	if (TargetASC && ChannelHandle->IsValid() && !bTargetDead)
	{
		// 先加 2s Tail 再移除 Channel：Stun 标签计数不落 0，眩晕动画不闪断
		ApplyStunTail(Target);
		TargetASC->RemoveActiveGameplayEffect(*ChannelHandle);
	}
	StunChannelHandles.Remove(Target);
}

void UAuraBeamSpell::ApplyStunTail(AActor* Target)
{
	if (!IsValid(Target) || (Target->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Target)))
	{
		return; // 尸体不需要 2 秒 Tail
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	UAbilitySystemComponent* CasterASC = GetAbilitySystemComponentFromActorInfo();
	if (!TargetASC || !CasterASC || !ElectrocuteStunTailClass)
	{
		return;
	}

	const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();
	const FGameplayEffectContextHandle Context = GetContextFromOwner(FGameplayAbilityTargetDataHandle());
	const FGameplayEffectSpecHandle Spec = CasterASC->MakeOutgoingSpec(ElectrocuteStunTailClass, GetAbilityLevel(), Context);
	if (Spec.Data.IsValid())
	{
		Spec.Data->DynamicGrantedTags.AddTag(Tags.Effects_Debuff_Stun);
		Spec.Data->DynamicGrantedTags.AddTag(Tags.Effects_Debuff_Electrocute);
		Spec.Data->AddDynamicAssetTag(Tags.Effects_Debuff);
		const FActiveGameplayEffectHandle Handle = CasterASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		StunTailHandles.Add(Target, Handle);
	}
}

void UAuraBeamSpell::ClearStunState()
{
	// 客户端没有施加过眩晕 GE（SyncStunWithCurrentTargets 服务器专用），这里只清本地记录
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		StunChannelHandles.Empty();
		StunTailHandles.Empty();
		return;
	}

	TArray<AActor*> Targets;
	for (const auto& Pair : StunChannelHandles)
	{
		if (AActor* Target = Pair.Key.Get())
		{
			Targets.Add(Target);
		}
	}
	for (AActor* Target : Targets)
	{
		RemoveStunFromTarget(Target);
	}
	StunChannelHandles.Empty();
	StunTailHandles.Empty();
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
