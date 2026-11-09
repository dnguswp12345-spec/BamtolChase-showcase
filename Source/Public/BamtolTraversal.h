#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BamtolTraversal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class ABamtolPlayer;

// ---- 바람 길: 계곡에 부는 바람. 들어가면 바람 방향으로 쭉 밀림 (도망자·밤톨 모두), 하얀 바람 줄기가 흘러감 ----
UCLASS()
class BAMTOLCHASE_API ABamtolWindZone : public AActor
{
	GENERATED_BODY()

public:
	ABamtolWindZone();

	// 길이·폭·높이 (cm). 바람은 +X 방향
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	FVector Size = FVector(4000.f, 900.f, 500.f);

	// 미는 힘 (cm/s², 땅 위). 공중(점프·활강)이면 AirMult 배
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	float Strength = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	float AirMult = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	int32 StreakCount = 22;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	float StreakSpeed = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	TSoftObjectPtr<UStaticMesh> StreakMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_WindStreak.SM_WindStreak")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	TSoftObjectPtr<UMaterialInterface> StreakMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Bamtol/FX/M_WindStreak.M_WindStreak")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Wind")
	TSoftObjectPtr<UStaticMesh> SignMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_WindSign.SM_WindSign")));

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	TArray<TWeakObjectPtr<ACharacter>> Candidates;
	float CandidateClock = 0.f;
	float ReactClock = 0.f;   // 가벼운 것 밀기 간격 (BamtolReactions.h)
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Box;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Streaks;
	TArray<FVector> StreakSeed;   // x = 진행(0~1), y = 옆, z = 높이
	TArray<FQuat> StreakRot;      // 줄기마다 세운 각도 (처음 한 번)
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Signs;
	float VisClock = 0.f;
	float VisDt = 0.f;        // 안 보이는 동안 모인 시간
	float VisCheck = 0.f;     // 0.2초마다 "화면에 그려졌나" 확인
	bool bVisNow = true;
	float DebugClock = 0.f;
};

// ---- 미끄럼틀: 비탈을 따라 놓인 반들반들한 길. 들어가면 엎드려 썰매처럼 쭉 (점프로 빠져나옴) ----
UCLASS()
class BAMTOLCHASE_API ABamtolSlide : public AActor
{
	GENERATED_BODY()

public:
	ABamtolSlide();

	// 길 점 (월드, 땅 위). 게임 모드가 비탈 따라 만들어 줌 (서버) → 복제
	UPROPERTY(ReplicatedUsing = OnRep_Points)
	TArray<FVector_NetQuantize> Points;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Slide")
	TSoftObjectPtr<UStaticMesh> SegmentMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_SlideSegment.SM_SlideSegment")));

	// 탈 수 있는 폭(반) · 최고 속도 (cm/s)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Slide")
	float HalfWidth = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Slide")
	float MaxSpeed = 2400.f;

	// 비탈 따라 점 만들기 (서버). 충분히 길면 true
	bool BuildDownhill(const FVector& Start, int32 MaxSegments = 28, float Step = 400.f);

	// 오르막 관 (2026-10-04): 아래 입구로 들어가면 위까지 일정한 속도로 올려 줌. 점 순서 = 아래 → 위
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Slide")
	bool bTube = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Slide")
	TSoftObjectPtr<UStaticMesh> TubeMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_TubeSegment.SM_TubeSegment")));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Slide")
	float TubeSpeed = 1700.f;
	// 아래 입구에서 이 거리 안에서만 탐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Slide")
	float TubeEntry = 700.f;
	bool BuildUphill(const FVector& Start, int32 MaxSegments = 70, float Step = 400.f);
	// 점 바로 주기 (서버, 2026-10-08 점프 숲): 땅 안 보고 그대로 — 미끄럼틀은 위 → 아래, 관은 아래 → 위
	void SetPath(const TArray<FVector>& InPoints, bool bInTube)
	{
		bTube = bInTube;
		Points.Reset();
		for (const FVector& P : InPoints)
		{
			Points.Add(P);
		}
		OnRep_Points();
	}
	// 길이 땅에 묻히지 않게 점 사이 땅을 보고 들어 올림
	void LiftAboveGround(float Clear);
	// 길 둘레 Radius 안의 놀이터 물건(상자·소품·버섯·가속판·대포·차) 치움 (서버, 묻히거나 길을 막던 것)
	int32 ClearAround(float Radius) const;

	float Length() const;
	// 길 따라 거리 → 위치·방향
	FVector PointAt(float Dist) const;
	FVector DirAt(float Dist) const;
	// 가장 가까운 길 위 거리와 옆으로 떨어진 정도 (cm)
	float Nearest(const FVector& At, float& OutSide, float& OutUp) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void OnRep_Points();
	void BuildMeshes();
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;
	TArray<float> CumLen;
	float CheckClock = 0.f;
};

// ---- 짚라인: 높은 기둥에서 낮은 기둥으로 이어진 줄. 위 기둥에서 [F] → 손잡이 잡고 휙 (점프로 놓음, 밤톨은 못 탐) ----
UCLASS()
class BAMTOLCHASE_API ABamtolZipline : public AActor
{
	GENERATED_BODY()

public:
	ABamtolZipline();

	// 위·아래 기둥 자리 (땅). 게임 모드가 정함 → 복제
	UPROPERTY(ReplicatedUsing = OnRep_Ends)
	FVector_NetQuantize Top;

	UPROPERTY(ReplicatedUsing = OnRep_Ends)
	FVector_NetQuantize Bottom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	float TopAnchorHeight = 420.f;     // 위 기둥에서 줄이 걸리는 높이

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	float BottomAnchorHeight = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	float HangDepth = 150.f;           // 줄 아래로 매달리는 깊이 (캡슐 가운데까지)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	float MaxSpeed = 2800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	float BoardRadius = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	TSoftObjectPtr<UStaticMesh> PoleMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_ZipPole.SM_ZipPole")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	TSoftObjectPtr<UStaticMesh> CableMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_ZipCable.SM_ZipCable")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bamtol|Zip")
	TSoftObjectPtr<UStaticMesh> HandleMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Bamtol/Props/Gameplay/SM_ZipHandle.SM_ZipHandle")));

	FVector AnchorA() const { return FVector(Top) + FVector(0.f, 0.f, TopAnchorHeight); }
	FVector AnchorB() const { return FVector(Bottom) + FVector(0.f, 0.f, BottomAnchorHeight); }
	float Length() const { return FVector::Dist(AnchorA(), AnchorB()); }
	FVector Dir() const { return (AnchorB() - AnchorA()).GetSafeNormal(); }

	// 줄에 닿을 수 있는 높이인지 확인해서 자리 정하기 (서버). 땅에 막히지 않으면 true
	bool Setup(const FVector& InTop, const FVector& InBottom, bool bForce = false);   // bForce: 줄 밑 막힘 검사 안 함 (점프 숲 — 코스 표가 정한 자리)

	// 손잡이 (매달린 사람을 따라감)
	void SetHandle(int32 Index, const FVector& At);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_Ends();
	void BuildMeshes();
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Handle;
};
