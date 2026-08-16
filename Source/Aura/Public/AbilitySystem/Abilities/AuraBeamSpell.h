// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "ActiveGameplayEffectHandle.h"
#include "AuraBeamSpell.generated.h"

class UAbilitySystemComponent;
class UAnimMontage;

/**
 * 光束法术（电击）基类：从施法者向目标方向发射光束。
 * 施法期间阻塞玩家的其他技能（ActivationBlockedTags）和移动（Player.Block 标签）。
 * GA_Electrocute 蓝图应继承此类：播放蒙太奇 → StoreOwnerVariables → TraceFirstTarget → 施加伤害。
 */
UCLASS()
class AURA_API UAuraBeamSpell : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraBeamSpell();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	/** 引导释放：按住时保持施法，松开输入结束技能 */
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;

	UFUNCTION(BlueprintCallable)
	void StoreOwnerVariables();

	UFUNCTION(BlueprintCallable)
	void StoreSingleTarget(const FVector& InTargetLocation);

	/** 从施法者向 BeamTargetLocation 做线检测，找到第一个目标（记录光束终点与目标 Actor）。返回是否命中敌人 */
	UFUNCTION(BlueprintCallable)
	bool TraceFirstTarget(const FVector& BeamTargetLocation);

	/** 是否已追踪到有效目标（纯函数，供蓝图分支） */
	UFUNCTION(BlueprintPure)
	bool HasBeamTarget() const { return IsValid(TargetActor); }

	/** 对 TraceFirstTarget 记录的目标施加伤害（目标为空时安全跳过） */
	UFUNCTION(BlueprintCallable)
	void CauseBeamDamage();

	/** 从第一个目标开始链式扩散：每个目标找最近的下一个敌人，存入 AdditionalTargets（有序链） */
	UFUNCTION(BlueprintCallable)
	void StoreChainTargets();

	/** 对 TargetActor 及所有 AdditionalTargets 施加伤害 + 挂电击循环 GC */
	UFUNCTION(BlueprintCallable)
	void ApplyBeamDamage();

	/** 给所有目标挂 GameplayCue.Electrocute（触发 GC_Notify 的光束/循环音） */
	UFUNCTION(BlueprintCallable)
	void AddShockLoopCue();

	/** 移除所有目标的电击 GC（技能结束时调用） */
	UFUNCTION(BlueprintCallable)
	void RemoveShockLoopCues();

	/** 寻找附加目标的半径 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Beam")
	float AdditionalTargetRadius = 300.f;

	/** 链式扩散目标数上限（实际 = min(技能等级, MaxChainTargets)，类似火球） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Beam")
	int32 MaxChainTargets = 5;

	/** 电击冷却时长（施放结束后进入冷却，防止按住快速重激活） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat")
	FScalableFloat CooldownDuration;

protected:
	UPROPERTY(BlueprintReadWrite, Category="Beam")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TObjectPtr<AActor> OwnerActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TObjectPtr<UAbilitySystemComponent> OwnerASC = nullptr;

	/** 追踪时忽略的 Actor（自己、已死亡目标等） */
	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TArray<TObjectPtr<AActor>> ActorsToIgnore;

	/** 电束传导到的额外目标（第一个目标之后） */
	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TArray<TObjectPtr<AActor>> AdditionalTargets;

	/** 是否已开始施法（安全网用：若激活后超时未施法则强制结束，防止卡住） */
	bool bCastStarted = false;

	/** 本次技能实例是否添加了 Player.Block，用于保证 loose tag 成对移除 */
	bool bBlockTagAdded = false;

	/** 施法安全超时定时器 */
	FTimerHandle CastSafetyTimer;

	/** 安全超时回调：未开始施法则强制结束技能 */
	void OnCastSafetyTimeout();

	/** 持续伤害间隔（秒），引导期间周期性对连锁目标掉血 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Beam")
	float DamageInterval = 0.5f;

	/** 持续伤害定时器 */
	FTimerHandle DamageTimer;

	/** 持续伤害定时器是否在跑 */
	bool bDamageTimerRunning = false;

	/** 鼠标与目标刷新间隔。视觉每帧跟随，目标判定按该频率更新。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Beam")
	float BeamUpdateInterval = 0.05f;

	/** Character yaw interpolation speed while channeling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Beam", meta=(ClampMin="0.0"))
	float BeamTurnSpeed = 12.f;

	FTimerHandle BeamUpdateTimer;
	bool bBeamUpdateTimerRunning = false;

	UPROPERTY()
	TObjectPtr<UAnimMontage> CachedChannelingMontage = nullptr;

	FName CachedChannelingSection = NAME_None;
	bool bChannelingMontageLoopConfigured = false;

	/** 对 TargetActor 及所有 AdditionalTargets 施加伤害（含周期性调用） */
	UFUNCTION(BlueprintCallable)
	void DamageChainTargets();

	// <眩晕（Electrocute Stun）>
	/** 电击期间持续存在的眩晕 GE（Infinite，授予 Effects.Debuff.Stun + Effects.Debuff.Electrocute） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stun")
	TSubclassOf<UGameplayEffect> ElectrocuteStunChannelClass;

	/** 电击离开后 2 秒的眩晕 GE（Has Duration） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stun")
	TSubclassOf<UGameplayEffect> ElectrocuteStunTailClass;

	/** 目标 → 本技能施加的 Channel GE Handle（存在 = 目标正被本技能电击眩晕中） */
	TMap<TWeakObjectPtr<AActor>, FActiveGameplayEffectHandle> StunChannelHandles;

	/** 目标 → 本技能施加的 Tail GE Handle（目标重新进链时移除，避免 4 秒叠加） */
	TMap<TWeakObjectPtr<AActor>, FActiveGameplayEffectHandle> StunTailHandles;

	/** 按当前链内目标同步眩晕状态：新进入 → Channel；离开/死亡 → Tail + 移除 Channel */
	void SyncStunWithCurrentTargets();

	/** 目标进入链：移除本技能上次的 Tail，施加无限时长 Channel */
	void ApplyStunToTarget(AActor* Target);

	/** 目标离开链：先施加 2 秒 Tail 再移除 Channel（Stun 标签计数不落 0，动画不闪断）；死亡目标跳过 Tail */
	void RemoveStunFromTarget(AActor* Target);

	/** 施加 2 秒 Tail 眩晕 GE 并记录 handle */
	void ApplyStunTail(AActor* Target);

	/** 技能结束：给所有被电击目标加 Tail + 移除 Channel，清空 handle */
	void ClearStunState();
	// </眩晕>

	void UpdateBeamFromCursor();
	void RefreshBeamTarget(const FVector& CursorLocation);
	void RemoveShockLoopCuesForTargets(AActor* PrimaryTarget, const TArray<TObjectPtr<AActor>>& ChainTargets);
	bool HaveSameCueTargets(AActor* OldPrimaryTarget, const TArray<TObjectPtr<AActor>>& OldChainTargets) const;
	void EnsureChannelingMontage();
	void UpdateOwnerFacing(const FVector& CursorLocation);
};
