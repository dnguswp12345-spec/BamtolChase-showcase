// 다시 쓰는 길목 액터 (2026-11-03) — 광산·Dark Ruins·하늘섬 어디든 놓고 씀. 설명서 Docs/Systems/Combat.md "길목 액터"
//   ABamtolRestFire   군밤 화로 쉼터: 옆에 1초 서 있으면 하트·군밤 꽉 + "여기서 다시" 자리로 기억 (ABamtolRestFire::LastFor)
//   ABamtolFogGate    솜사탕 안개: 보스 방 문. 싸우기 전엔 몽실몽실 지나갈 수 있음 · 싸움 중엔 진하게 부풀고 안에서 못 나감 (방이 밀어냄) · 이기면 걷힘
//   ABamtolShortcutDoor 지름길: 빗장 문(Style 0) · 밧줄 사다리(Style 1). **반대편(앞 +X 쪽 / 사다리는 위)에서만 열림** — 한 번 열리면 계속 열림
//   전부 서버가 상태를 정하고(복제), 겉모습은 각 화면이 그 상태로 그림. 키 입력 없음 (가까이 가서 잠깐 서 있으면 됨 → 원래 [F] 길을 안 건드림)
//   이름·화면은 소울류를 따라 하지 않음: 글자는 "군밤 화로 — 따끈" 같은 우리 말, 화면 가운데 큰 글씨 없음.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BamtolCheckpoint.generated.h"

class ABamtolPlayer;
class AHUD;
class UCanvas;
class UFont;
class UStaticMeshComponent;
class UPointLightComponent;
class UBoxComponent;

// 군밤 화로 쉼터
UCLASS()
class BAMTOLCHASE_API ABamtolRestFire : public AActor
{
	GENERATED_BODY()

public:
	ABamtolRestFire();
	static ABamtolRestFire* Spawn(UWorld* W, const FVector& Ground, float Yaw, AActor* Owner = nullptr);   // 서버
	static ABamtolRestFire* Near(const AActor* Who, float Extra = 0.f);                                     // 모든 화면
	static bool LastFor(const ABamtolPlayer* P, FVector& OutAt);   // 서버: 이 사람이 마지막으로 쉰 화로 자리 (캠핑카 침대에서 쉬었으면 그 차의 지금 자리)
	static void SetCarRest(const ABamtolPlayer* P, AActor* Car, const FVector& Local);   // 서버: 캠핑카 침대 = 부활 자리 (Local = 차 기준 자리, 차를 따라다님)
	static bool CarRestFor(const ABamtolPlayer* P);               // 지금 부활 자리가 살아 있는 차인가
	static void DrawHud(AHUD* Hud, UCanvas* Canvas, UFont* Font);   // 가까이 가면 아래쪽 작은 안내 (BamtolCombat::DrawHud 가 부름)
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	bool IsNear(const FVector& At, float Extra = 0.f) const;
	FVector StandAt(int32 Slot) const;   // 둘레에 서는 자리 (전멸 뒤 옮겨 놓기)

	UPROPERTY(EditAnywhere, Category = "Rest") float Radius = 260.f;
	UPROPERTY(EditAnywhere, Category = "Rest") float StandTime = 1.f;
	UPROPERTY(Replicated) uint8 Lit = 0;        // 한 번이라도 누가 쬐었나 (불이 커짐)
	TFunction<void(ABamtolPlayer*)> OnRested;   // 서버: 채워 줬을 때 (보스 방 등)
	int32 NumRests = 0;

private:
	void Build();
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Fire;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Nuts;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> Light;
	TMap<TWeakObjectPtr<ABamtolPlayer>, float> StandSec;
	float Acc = 0.f;
};

// 솜사탕 안개 (보스 방 문)
UCLASS()
class BAMTOLCHASE_API ABamtolFogGate : public AActor
{
	GENERATED_BODY()

public:
	ABamtolFogGate();
	static ABamtolFogGate* Spawn(UWorld* W, const FVector& Door, const FVector& Inward, float Width, AActor* Owner = nullptr);   // 서버
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 이 자리가 안개 띠 안인가 (Depth = 문 앞뒤 두께)
	bool InGate(const FVector& At, float Depth = 140.f) const;
	float SideOf(const FVector& At) const;      // + 안쪽 · - 바깥

	enum : uint8 { Open = 0, Sealed = 1, Cleared = 2 };
	UPROPERTY(Replicated) uint8 State = Open;   // 0 몽실 (지나감) · 1 싸움 중 (진하게, 안에서 못 나감) · 2 걷힘
	UPROPERTY(Replicated) float Width = 420.f;
	UPROPERTY(Replicated) float Height = 330.f;

private:
	void Build();
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
	TArray<FVector> PuffBase;
	TArray<float> PuffSize, PuffSeed;
	float Shown = 0.f;      // 0 걷힘 ~ 1 몽실 ~ 1.5 진함
	// 나이아가라 솜사탕 (공 메시 흉내 대신): 칸마다 하나씩, 상태 바뀌면 다시 깖
	void BuildFx(uint8 ForState);
	UPROPERTY(Transient) TArray<TObjectPtr<class UNiagaraComponent>> FogFx;
	uint8 FxState = 0xFF;
};

// 지름길 (빗장 문 · 밧줄 사다리)
UCLASS()
class BAMTOLCHASE_API ABamtolShortcutDoor : public AActor
{
	GENERATED_BODY()

public:
	ABamtolShortcutDoor();
	// 서버. 문: At = 문 가운데 땅, Yaw = 여는 쪽(앞 +X)을 향함. 사다리: At = 위 모서리, Yaw = 내려가는 쪽, Size = 높이
	static ABamtolShortcutDoor* Spawn(UWorld* W, uint8 Style, const FVector& At, float Yaw, float Size, AActor* Owner = nullptr);
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	bool CanOpenFrom(const FVector& At) const;  // 여는 쪽에 가까이 있나
	void OpenNow(ABamtolPlayer* By);            // 서버

	enum : uint8 { BarDoor = 0, RopeLadder = 1 };
	UPROPERTY(EditAnywhere, Replicated, Category = "Shortcut") uint8 Style = BarDoor;
	UPROPERTY(EditAnywhere, Replicated, Category = "Shortcut") float Size = 300.f;     // 문 너비 · 사다리 높이
	UPROPERTY(EditAnywhere, Category = "Shortcut") bool bStartOpen = false;
	UPROPERTY(EditAnywhere, Category = "Shortcut") FName SaveKey;                       // 비지 않으면 이 판(세션) 동안 열린 채 기억
	UPROPERTY(ReplicatedUsing = OnRep_Open) bool bOpen = false;
	UPROPERTY(Replicated) double OpenAt = -100.0;
	int32 NumLifts = 0, NumLockedHints = 0;

private:
	UFUNCTION() void OnRep_Open();
	void Build();
	void ServerTick(float Dt);
	void ApplyLook(float Dt);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Block;     // 닫힌 문 막기 (사다리는 없음)
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeafL;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeafR;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Bar;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Rungs;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Roll;
	TMap<TWeakObjectPtr<ABamtolPlayer>, float> NearSec, HintAt, LiftSec;
	TMap<TWeakObjectPtr<ABamtolPlayer>, double> Climbing;   // 사다리: 위로 솟는 중 → 턱 위로 올라서면 안쪽으로 살짝 밀어 줌
	float Acc = 0.f;
};
