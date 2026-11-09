// 밤송이 우리 — "잡히면 갇힘 + 친구가 구출 + 잡힌 횟수 제한" (2026-10-07 사용자: "갇힘+구출로 하자. 데바데 같이. 카운터 제한 두고.")
//   작은 맵 새(추격) 구조 판에서만 (산·옛 구조·알밤이 잡기는 예전 그대로 = 잡히면 작은 밤톨).
//   흐름:
//     잡힘 → 가까운 빈 우리로 '통' 날아가 갇힘 (대장이 들고 가지 않음 — 속도감, 대신 대장은 2초 숨 고르기)
//       → 갇힌 동안 시계: 1단계 45초 → 2단계 45초 → 다 되면 탈락
//       → 잡힌 횟수로 시작 단계: 1번째 = 1단계부터 · 2번째 = 2단계부터 · 3번째 = 바로 탈락 (횟수 제한)
//     친구가 우리 앞에서 [F] 꾹 2초 → 문 열림: 갇힌 녀석 3초 빨라짐 + 5초 못 잡힘, 구한 녀석 기록·명장면
//     대장이 우리 8m 안에 오래 있으면(캠핑) 시계가 느려짐 (데바데 반캠핑)
//     갇힌 우리 = 모두(대장 포함) 화면·미니맵에 자리 + 남은 시간 → 구하러 오는 녀석을 대장이 노림 (추격이 생김)
//     탈락 = 작은 밤톨(대장 편, 약하게) — 방 설정 "탈락하면: 관전" 이면 관전
//   우리 놓기: 판 시작 때 게임 모드가 6~8개를 맵에 흩어 놓음 (서로 멀게, 열린 곳, 자판기·출구와 떨어지게 — 맵 파일은 안 고침)
//   모델: Tools/Art/build_cage.py → Tools/UE/import_cage.py (/Game/Bamtol/Props/Cage/SM_ChestnutCage + 문 SM_ChestnutCageDoor)
//   시험: Bamtol.CageTest=1 (내 몸을 일부러 잡히게 → 갇힘 → AI 구출 → 다시 잡힘 ×3 → 탈락, 로그 [Cage]) · Bamtol.Cage=0 (끄기, 전/후 비교)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BamtolCage.generated.h"

class ABamtolPlayer;
class ABamtolGameMode;
class UCapsuleComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

namespace BamtolCage
{
	// 이번 판이 갇힘 규칙인가 (모두 — GS 복제 값)
	BAMTOLCHASE_API bool On(const UWorld* W);
	// 서버: 새 구조 판에서 갇힘 규칙을 쓸까 (cvar Bamtol.Cage, 기본 1)
	BAMTOLCHASE_API bool Setting();
	// 탈락하면 관전인가 (방 설정, GS 복제)
	BAMTOLCHASE_API bool OutSpectate(const UWorld* W);
	// 숫자 (시험 cvar 로 덮어 조정)
	BAMTOLCHASE_API float StageSeconds();      // 단계마다 초 (45)
	BAMTOLCHASE_API int32 MaxCatches();        // 이 번째 잡히면 바로 탈락 (3)
	BAMTOLCHASE_API int32 CageCount(int32 Runners);   // 놓을 우리 수 (6~8)
	BAMTOLCHASE_API bool Test();
	// 판 측정 (UBamtolPaceMeter): 0 갇힘 · 1 구출 · 2 탈락 · 3 잡힘(잡힌 일 모두)
	BAMTOLCHASE_API void Note(const UWorld* W, int32 Kind);
	BAMTOLCHASE_API int32 CatchCount(const UWorld* W);
	BAMTOLCHASE_API void Counts(const UWorld* W, int32& OutCaged, int32& OutRescued, int32& OutOut);
	BAMTOLCHASE_API void ResetCounts(const UWorld* W);
	// 시험 (Bamtol.CageTest=1): 게임 모드가 판 중 매 틱 부름
	BAMTOLCHASE_API void TickTest(ABamtolGameMode* GM, float Dt);
}

UCLASS()
class BAMTOLCHASE_API ABamtolCage : public AActor
{
	GENERATED_BODY()

public:
	ABamtolCage();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- 서버 ----
	// 잡힌 녀석을 넣을 우리 고르기: 빈 우리 중 잡힌 곳에서 가까운 것 (12m 안 = 대장 바로 옆은 되도록 피함)
	static ABamtolCage* PickFor(UWorld* World, const FVector& CatchAt);
	// 가둠: Stage 1 또는 2부터
	void Lock(ABamtolPlayer* P, int32 InStage);
	// [F] 누름: 갇힌 우리 앞이면 문 열기 시작 (true = [F] 처리 끝)
	bool TryStartOpen(ABamtolPlayer* By);
	void StopOpen(ABamtolPlayer* By);
	// 이 자리 앞의 (누가 갇힌) 우리
	static ABamtolCage* FindNear(const UWorld* World, const FVector& At, float Extra = 0.f);
	// 감옥 (2026-10-26, 판 끝 재판소가 부름 — 서버): P 자리에 우리를 새로 세워 Seconds 초 동안 못 움직이게 가둠 → 끝나면 풀고 우리를 지움.
	//   판 규칙과 따로: 잡힘(IsCaught)·탈락·점수·알림 없음, 다른 잡힘이 이 우리를 고르지 않음. 판 밖(마을)에서도 됨.
	//   [F] 구출은 받음 (2026-10-27 사용자): 다른 사람이 우리 앞에서 [F] 를 OpenSeconds 초 꾹 → 바로 풀림 + "누가 누구를 구함" 피드 한 줄
	//   이미 감옥에 있으면 그 우리의 남은 시간을 Seconds 로 다시 맞춤. 실패(P 없음·서버 아님) = nullptr
	static ABamtolCage* JailFor(ABamtolPlayer* P, float Seconds);
	bool IsJail() const { return bJail; }
	// 감옥 구출 (서버): 이 자리 앞 감옥 우리 · [F] 누름 = 꾹 누르기 시작 (손 뗌은 StopOpen 그대로)
	static ABamtolCage* FindJailNear(const UWorld* World, const FVector& At);
	bool TryStartJailRescue(ABamtolPlayer* By);
	ABamtolPlayer* GetJailed() const { return Jailed.Get(); }
	// 판 끝: 갇힌 채면 그대로 (0점) — 정리만
	void ClearForEnd();
	// 우리 제비뽑기 (2026-10-12, BamtolGamble.h): 당첨 = 갇힌 녀석이 스스로 나옴(구한 사람 없음) · 꽝 = 이번에 갇힌 동안 문 여는 시간이 Extra 초 늘어남(우리가 비면 원래대로)
	bool SelfFree();
	// 구출 리본 (2026-10-12): 멀리서 바로 엶 — 손으로 연 것과 같은 구출(기록·명장면·풀려난 뒤 덤 그대로)
	bool RibbonOpen(ABamtolPlayer* By);
	// 얼음 도토리 시간 다 됨 (2026-10-27, BamtolIceAcorn.cpp): 구출 아님 — 우리를 비우고 갇힌 녀석을 문 앞에 꺼냄(못 잡히는 시간 없음).
	//   꺼낸 녀석 = 돌려줌(바로 보통 잡힘으로 넘김) · 얼음땡 얼음이면 우리는 곧 사라짐 · 빈 우리 = nullptr
	ABamtolPlayer* TakeOutQuiet();
	void AddOpenSeconds(float Extra);
	float GetOpenExtra() const { return OpenExtra; }
	double GetLockedAt() const { return LockedAt; }   // 서버: 이번에 갇힌 때 (갇힘마다 한 번만 정할 일에 씀)

	// ---- 모두 ----
	ABamtolPlayer* GetPrisoner() const { return Prisoner; }
	bool IsOccupied() const { return Prisoner != nullptr; }
	int32 GetStage() const { return Stage; }
	float GetStageLeft() const { return StageLeft; }
	float GetOpenProgress() const { return OpenProgress; }
	bool IsOpening() const { return Openers > 0; }
	bool IsCamped() const { return bCamped; }
	bool IsOpenedBy(const ABamtolPlayer* P) const;   // 서버
	// 남은 시간 전체 (1단계면 2단계까지 더함). 얼음 도토리처럼 따로 끝나는 시각이 있으면 그것과 더 짧은 쪽
	float TotalLeft() const;
	// 얼음 도토리 (서버): 이 초 뒤 보통 잡힘 → AI 가 남은 시간을 보고 급하면 먼저 구하러 옴. 비면 지워짐
	void SetTimeLimit(float Seconds);
	// 우리 안 (갇힌 녀석 발 자리) · 문 앞 (풀려난 녀석이 나오는 곳)
	FVector InsideFeet() const;
	FVector DoorFront() const;

	// 단계마다 초 · 문 여는 초 · 여는 거리(cm, 우리 가운데부터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float StageSeconds = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float OpenSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float UseRadius = 260.f;

	// 반캠핑: 대장이 이 안(cm)에 CampGrace 초 넘게 있으면 시계가 CampSlow 배로 느려짐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float CampRadius = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float CampGrace = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float CampSlow = 0.35f;

	// 풀려난 녀석: 빨라짐(더하는 속도·초) · 못 잡히는 초
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float FreeSpeedBonus = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float FreeSpeedSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Cage")
	float FreeSafeSeconds = 5.f;

	UPROPERTY(VisibleAnywhere, Category = "Bamtol|Cage")
	TObjectPtr<UCapsuleComponent> Block;

	UPROPERTY(VisibleAnywhere, Category = "Bamtol|Cage")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Bamtol|Cage")
	TObjectPtr<UStaticMeshComponent> Door;

	UPROPERTY(VisibleAnywhere, Category = "Bamtol|Cage")
	TObjectPtr<UStaticMeshComponent> Ring;

	UPROPERTY(VisibleAnywhere, Category = "Bamtol|Cage")
	TObjectPtr<UPointLightComponent> Glow;

	static constexpr float HalfHeight = 145.f;   // 뿌리(캡슐 가운데)가 땅 위 이만큼

	// 방 규칙 "얼음땡" (2026-10-12): 잡힌 그 자리에 생긴 우리 — 시계 없음 · 풀리면 사라짐 (서버, ABamtolGameMode::SpawnFreezeCage)
	//   2026-10-13 복제함: 모든 화면에서 밤송이 우리 대신 얼음 모양(SM_IceCage + 옅은 얼음 껍질)으로 보이게
	UPROPERTY(ReplicatedUsing = OnRep_FreezeCage)
	bool bFreezeCage = false;

	UFUNCTION()
	void OnRep_FreezeCage();

	// 감옥 우리 (JailFor) — 복제: 모든 화면에서 빈 우리가 아니라 불 켜진 우리로 보이게
	UPROPERTY(Replicated)
	bool bJail = false;

protected:
	virtual void BeginPlay() override;

	// 소리·효과 (모두): 0 갇힘 · 1 2단계 · 2 문 열림(구출) · 3 탈락 · 4 여는 중 덜컹
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFx(uint8 Kind);

private:
	UPROPERTY(Replicated)
	TObjectPtr<ABamtolPlayer> Prisoner;

	// 감옥: 갇힌 녀석(판 규칙의 Prisoner 와 따로 — 그래서 다른 코드가 "갇힌 우리"로 안 봄) · 풀 때 · 다시 묶는 타이머
	TWeakObjectPtr<ABamtolPlayer> Jailed;
	FTimerHandle JailEndTimer;
	FTimerHandle JailHoldTimer;
	void JailHold();
	void JailEnd();
	void TickJail(float DeltaSeconds);   // 서버: 감옥 [F] 꾹 누르기 셈 → 다 차면 구출

	UPROPERTY(Replicated)
	uint8 Stage = 0;

	UPROPERTY(Replicated)
	float StageLeft = 0.f;

	float LimitAt = -1.f;   // 서버 시계로 이때 끝남 (음수 = 없음, SetTimeLimit)

	UPROPERTY(Replicated)
	float OpenProgress = 0.f;

	UPROPERTY(Replicated)
	uint8 Openers = 0;

	UPROPERTY(Replicated)
	bool bCamped = false;

	// 제비뽑기 꽝으로 늘어난 문 여는 초 (0 = 안 늘어남) — 화면: 제비 안내를 감춤
	UPROPERTY(Replicated)
	float OpenExtra = 0.f;

	TArray<TWeakObjectPtr<ABamtolPlayer>> Holders;   // 서버: 지금 여는 녀석
	float CampTime = 0.f;
	float NoiseClock = 0.f;
	float RattleClock = 0.f;
	double LockedAt = 0.0;
	float DoorLift = 0.f;    // 화면: 문 들림 (0~1)
	float OpenFlash = 0.f;   // 화면: 열린 뒤 잠깐 문 올라가 있음
	float Clock = 0.f;
	FVector DoorBase = FVector::ZeroVector;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMID;
	// 얼음땡 얼음 모양 (각 화면): 몸 = 얼음 덩어리, 문 = 숨김, 껍질 = 옅은 하늘빛 공
	void ApplyFreezeLook();
	bool bFreezeLook = false;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> IceShell;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> IceShellMID;

	void TickServer(float DeltaSeconds);
	void Release(ABamtolPlayer* By);
	void Eliminate();
	void Empty();
	void BuildFallbackLook();
};
