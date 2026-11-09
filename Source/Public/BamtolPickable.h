// 다시 자라는 줍기 점 (2026-11-05, 기획 Docs/Plan_SmallGaps_1105.md 1절 — 발헤임 Pickable 식)
//   걷다 반짝이는 버섯·산딸기 발견 → [F] 쏙 → 주머니 +1 (가끔 덤 +1) → 스르르 작아져 사라짐 → 다음 날(또는 N초 뒤) 통통 튀며 다시 자람.
//   서버가 판정·저장. 주운 자리는 나갔다 와도 빈 자리 (UBamtolPickableSave, 방장 컴퓨터).
//   모양·이름은 채집 표 BamtolAnimals::Items() 의 Fg* 그대로 (새 아이템 없음). 소리 이름 Pick_Pop / Pick_Regrow (기획 문서 소리 이름표).
//   ABamtolPickable = 점 하나 · ABamtolPickableField = 맵에 놓는 뿌리개(무리로 심음).
#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/WorldSubsystem.h"
#include "BamtolExpedition.h"
#include "BamtolPickable.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UDrawSphereComponent;
class ABamtolPlayer;
class ABamtolPickable;

UENUM(BlueprintType)
enum class EBamtolPickRespawn : uint8
{
	NextDay,   // 날짜키가 바뀌면 (침대로 넘긴 날 포함)
	Seconds,   // RespawnSeconds 초 뒤 (방장 컴퓨터 시계, 껐다 켜도 이어짐)
};

USTRUCT(BlueprintType)
struct FBamtolPickKind
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id = TEXT("FgShroom");   // 채집 표 Id
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Weight = 1.f;
};

USTRUCT()
struct FBamtolPickableRow
{
	GENERATED_BODY()
	UPROPERTY() uint32 Key = 0;     // 맵 이름 + 위치(10cm 반올림) 해시
	UPROPERTY() int32 Day = 0;      // 주운 날짜키 (0 = 처음 섞기 대기)
	UPROPERTY() int64 Unix = 0;     // 주운 Unix 초 (처음 섞기 대기는 "이때부터 준비")
};

UCLASS()
class BAMTOLCHASE_API UBamtolPickableSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TArray<FBamtolPickableRow> Rows;
	UPROPERTY() TArray<FString> InitMaps;   // 처음 섞기를 이미 한 맵
};

namespace BamtolPickable
{
	struct FPlan { FVector At = FVector::ZeroVector; FName Id; float Yaw = 0.f; };

	// 이 월드에 줍기 점이 있나 ([F] 고르기가 일반 맵에서도 이걸 잡게 함 — BamtolInteraction::UsesCraftPick 의 고리)
	BAMTOLCHASE_API bool Present(const UWorld* W);
	// 저장 칸 (시험: 다른 칸으로 바꾸기, 빈 글 = 진짜)
	BAMTOLCHASE_API FString SlotName();
	BAMTOLCHASE_API void UseTestSlot(const FString& Slot);
	// 무리 계획: 같은 (월드 땅, 씨앗, 값) = 같은 자리·같은 종류. 바닥 라인 트레이스·경사<30도·물·겹침 0·점끼리 1.2m. 땅이 없으면 덜 나옴
	BAMTOLCHASE_API void PlanClusters(UWorld* W, const FVector& Center, float Radius, int32 Count, int32 Clusters, const TArray<FBamtolPickKind>& Kinds, int32 Seed, TArray<FPlan>& Out);
	BAMTOLCHASE_API TArray<FBamtolPickKind> DefaultKinds();
}

// ---- 저장·다시 자람 관리 (월드마다 하나, 서버만 저장) ----
UCLASS()
class BAMTOLCHASE_API UBamtolPickableWorld : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	static UBamtolPickableWorld* Get(const UWorld* W) { return W ? W->GetSubsystem<UBamtolPickableWorld>() : nullptr; }

	void Register(ABamtolPickable* P);
	void Unregister(ABamtolPickable* P);
	int32 Num() const { return Spots.Num(); }
	// 서버: 저장 줄 하나 (없으면 false)
	bool Find(uint32 Key, FBamtolPickableRow& Out) const;
	void Put(const FBamtolPickableRow& Row);
	void Erase(uint32 Key);
	bool IsFirstRun() const { return bFirstRun; }
	// 시험
	void SaveNow();
	void ReloadFromFile();   // 메모리 비우고 파일에서 다시
	void PollNow() { Poll(); }
	int32 SavedNum() const { return Rows.Num(); }
	bool HasRow(uint32 Key) const { return Rows.Contains(Key); }

private:
	void EnsureLoaded();
	void Poll();
	void Dirty();
	TArray<TWeakObjectPtr<ABamtolPickable>> Spots;
	TMap<uint32, FBamtolPickableRow> Rows;
	TArray<FString> InitMaps;
	FTimerHandle PollTimer, SaveTimer;
	bool bLoaded = false, bFirstRun = false;
};

// ---- 줍기 점 하나 ----
UCLASS()
class BAMTOLCHASE_API ABamtolPickable : public ABamtolUseSpot
{
	GENERATED_BODY()

public:
	ABamtolPickable();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float Dt) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual bool CanUse(const ABamtolPlayer* P) const override;
	virtual FString Prompt(const ABamtolPlayer* P) const override;
	virtual bool Use(ABamtolPlayer* P) override;

	// 채집 표 Id (Fg*) — 복제 (손님이 같은 모양을 지음)
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Item, Category = "Pickable") FName ItemId = TEXT("FgShroom");
	UPROPERTY(EditAnywhere, Category = "Pickable") int32 Amount = 1;
	UPROPERTY(EditAnywhere, Category = "Pickable") EBamtolPickRespawn RespawnMode = EBamtolPickRespawn::NextDay;
	UPROPERTY(EditAnywhere, Category = "Pickable") float RespawnSeconds = 600.f;
	UPROPERTY(EditAnywhere, Category = "Pickable", meta = (ClampMin = "0", ClampMax = "1")) float BonusChance = 0.15f;
	// 처음 켜질 때 0~이 분 사이로 섞어 첫 준비 (0 = 바로 준비). 이미 저장된 자리는 저장대로
	UPROPERTY(EditAnywhere, Category = "Pickable") float StaggerMinutes = 40.f;

	bool IsReady() const { return bReady; }
	uint32 GetKey() const { return Key; }
	// 서버: 이 사람이 줍기 (시험도 이걸 부름). 못 주우면 false
	bool ServerPick(ABamtolPlayer* P, int32* OutGot = nullptr);
	// 서버: 시간이 되면 다시 자람 (월드 관리자가 1초마다 부름). 자랐으면 true
	bool ServerCheckRegrow(int64 NowUnix, int32 TodayKey);

private:
	UFUNCTION() void OnRep_Ready();
	UFUNCTION() void OnRep_Item();
	void BuildLook();
	void ApplyReady(bool bAnimate);
	void SetScale(float S);
	bool ReadyBy(const FBamtolPickableRow& R, int64 NowUnix, int32 TodayKey) const;

	UPROPERTY(ReplicatedUsing = OnRep_Ready) bool bReady = true;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Pivot;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Glint;
	uint32 Key = 0;
	bool bVisInit = false, bVisReady = true, bRegistered = false;
	FName BuiltItem;
	float AnimT = 0.f;   // 0 = 쉬는 중
	uint8 AnimKind = 0;  // 0 없음 · 1 쏙 작아짐 · 2 튀어 오르며 자람
	float Phase = 0.f, Clock = 0.f;
};

// ---- 맵에 놓는 뿌리개: 반지름·개수·종류(가중치)·씨앗 → 서버가 BeginPlay 에 무리지어 심음 ----
UCLASS()
class BAMTOLCHASE_API ABamtolPickableField : public AActor
{
	GENERATED_BODY()

public:
	ABamtolPickableField();
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& T) override;

	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "100")) float Radius = 2500.f;   // cm
	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "1", ClampMax = "200")) int32 Count = 18;
	UPROPERTY(EditAnywhere, Category = "Field") int32 Seed = 1;
	UPROPERTY(EditAnywhere, Category = "Field") TArray<FBamtolPickKind> Kinds;   // 비면 기본(버섯·산딸기·들꽃·쑥·솔방울·알밤)
	UPROPERTY(EditAnywhere, Category = "Field") EBamtolPickRespawn RespawnMode = EBamtolPickRespawn::NextDay;
	UPROPERTY(EditAnywhere, Category = "Field") float RespawnSeconds = 600.f;
	UPROPERTY(EditAnywhere, Category = "Field") float BonusChance = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Field") float StaggerMinutes = 40.f;

	// 서버: 심을 자리 계획만 (시험이 같은 씨앗 두 번 비교)
	void ComputePlan(TArray<BamtolPickable::FPlan>& Out) const;
	// 서버: 계획대로 심기 (BeginPlay 가 부름). 심은 개수
	int32 SpawnAll();
	const TArray<TWeakObjectPtr<ABamtolPickable>>& GetSpawned() const { return Spawned; }

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDrawSphereComponent> Ring;   // 에디터에서 반지름 보이게
	TArray<TWeakObjectPtr<ABamtolPickable>> Spawned;
};
