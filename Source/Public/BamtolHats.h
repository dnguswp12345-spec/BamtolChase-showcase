#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "GameFramework/Actor.h"
#include "BamtolReactions.h"
#include "BamtolHats.generated.h"

class UVerticalBox;
class UTextBlock;
class UStaticMeshComponent;

// 모자 능력 (모자 모양 = 능력, 색·무늬는 꾸미기 — 나중에 파는 건 꾸미기만)
// 숫자는 Data/Hats.csv (엑셀) → DT_BamtolHats. 줄 이름 = 모자 ID (ABamtolPlayer::HatIds 순서와 같음)
UENUM(BlueprintType)
enum class EBamtolHatAbility : uint8
{
	None,
	Stamina,    // 도토리: 질주 게이지 +Value
	Bouncy,     // 빨간 버섯: 점프 +Value, 착지 때 통
	Sturdy,     // (옛 솔방울: 어지러움 ×Value — 2026-10-12 부터 쓰는 모자 없음. 체감이 안 되고 투구와 겹쳐 Scatter 로 바꿈)
	Glider,     // 나뭇잎: 활강 더 낮은 데서, 천천히 내려감
	Shield,     // 장수풍뎅이 투구 (2026-10-08 호두 껍질 대신): 총·대포·덫·통나무·차 한 번 막기 (Cooldown 마다)
	Spiky,      // 밤송이: 부딪힌 친구·작은 밤톨을 튕겨 냄
	Dash,       // 단풍잎: R 빙글 대시
	Lure,       // 산딸기: R 산딸기 던지기 → 밤톨이 그 소리로
	Hide,       // 달팽이 집: [R] Value 초 근처 물건으로 변신 (ABamtolPlayer::StartDisguise)
	Cheer,      // 꽃 화관: 근처 친구(나 포함) 질주 게이지 회복 ×Value
	Scout,      // 새둥지: Cooldown 초마다 새가 밤톨 위치를 잠깐 알려 줌
	Bell,       // 도라지꽃 (2026-10-08): 밤톨이 Radius 안에 오면 딸랑 → 나와 Value 안 친구에게 방향 알림, Cooldown 초에 한 번
	Scatter,    // 솔방울 (2026-10-12): [R] 뒤에 솔방울 3개 (Value 초 남음, 밟는 반지름 Radius) → 밟은 술래·친구 미끄덩 (BamtolHatFx.h ABamtolPineDrop)
};

USTRUCT(BlueprintType)
struct FBamtolHatRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	EBamtolHatAbility Ability = EBamtolHatAbility::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	FString AbilityName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	float Cooldown = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	float Radius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	bool bActive = false;            // R 키로 쓰는 능력

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	int32 MaxPerMatch = 2;           // 한 판에 같은 모자 최대 몇 명

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hat")
	FString Detail;                  // 자세한 설명 (고르기 창에서 마우스 올리면 둘째 줄). Description 은 "무엇을 하면 → 무슨 일이" 한 줄 (2026-10-12)
};

namespace BamtolHats
{
	// 표(없으면 코드 기본값)에서 한 줄
	BAMTOLCHASE_API const FBamtolHatRow* Find(FName Id);
	BAMTOLCHASE_API const TArray<FName>& Ids();
	// 2026-11-03 (13.795): 모자는 꾸미기(스킨)만. 능력은 기본 꺼짐 — 옛 능력 시험·떼 모드 재사용 때만 Bamtol.HatAbilities=1
	BAMTOLCHASE_API bool AbilitiesOn();
	// 치장 표(Data/Cosmetics.csv)의 모자 줄 이름 "Hat_<모자 Id>" ↔ 모자 번호(Ids 순서)
	BAMTOLCHASE_API FName CosId(FName HatId);
	BAMTOLCHASE_API int32 IndexOfCos(FName CosId);   // 모자 줄이 아니면 -1
	BAMTOLCHASE_API FName DefaultCosId();            // 아무것도 안 썼을 때 = 도토리 모자
}

// 모자 고르기 버튼 (눌리면 몇 번째인지 알려 줌)
UCLASS()
class BAMTOLCHASE_API UBamtolHatButton : public UButton
{
	GENERATED_BODY()

public:
	int32 Index = 0;
	TWeakObjectPtr<class UBamtolHatSelectWidget> Owner;

	void Setup();

private:
	UFUNCTION()
	void HandleClick();

	UFUNCTION()
	void HandleHover();
};

// 판 시작 준비 시간에 뜨는 모자 고르기 창. 모양은 WBP_HatSelect(이 클래스 자식)에서,
// "HatList"(VerticalBox)·"Title"(TextBlock) 이 없으면 코드가 기본 모양을 만든다.
UCLASS()
class BAMTOLCHASE_API UBamtolHatSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Bamtol|Hat")
	TObjectPtr<UVerticalBox> HatList;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Bamtol|Hat")
	TObjectPtr<UTextBlock> Title;

	void Choose(int32 Index);
	void Hover(int32 Index);
	void Refresh();
	// 꾸미기 [C] 창 모자 탭 안에 들어 있음 (2026-10-30): 밝은 판 위 글 색 · 고르면 대기실에선 창을 그대로 둠
	bool bEmbedded = false;

	// 아이콘 (Art/UI/Icons → /Game/Bamtol/UI/Icons/T_Hat_<ID>)
	static class UTexture2D* HatIcon(int32 Index);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Labels;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBamtolHatButton>> Cards;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Desc;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DescMore;   // 자세한 설명 (작게)
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Tags;   // 카드 딱지: "[R] 누르기" / "늘 켜짐"
	UPROPERTY(Transient)
	TObjectPtr<class UHorizontalBox> Brief;   // "이번 판" 한 줄: 대장·숲 친구·맵 사건·깜짝 규칙·날씨 (BamtolRoundBrief.h)
	FString BriefSig;
	int32 Hovered = -1;
	float RefreshClock = 0.f;
};

// 산딸기 미끼: 떨어진 곳에서 몇 초 동안 소리 → 밤톨 AI 가 그리로 (모양은 BP_BamtolLure)
UCLASS()
class BAMTOLCHASE_API ABamtolLure : public AActor
{
	GENERATED_BODY()

public:
	ABamtolLure();
	virtual void Tick(float DeltaSeconds) override;
	void Init(const FVector& Velocity, float NoiseRadius, float Seconds);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool IsLanded() const { return bLanded; }
	void SetThrower(class ABamtolPlayer* P);

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
	FVector Vel = FVector::ZeroVector;
	UPROPERTY(Replicated)
	bool bLanded = false;            // 복제 (2026-10-12): 손님인 사람 술래 화면에도 가짜 발소리·발자국이 나오게
	TWeakObjectPtr<class ABamtolPlayer> Thrower;
	bool bWorked = false;            // 술래가 속아서 가까이 옴 (판 기록 한 번)
	float StepClock = 0.f;           // 가짜 발소리 (모든 화면)
	bool bStepLeft = false;
	float Radius = 7000.f;
	float Life = 8.f;
	float NoiseClock = 0.f;
	BamtolReact::FDrift Drift;       // 떨어진 뒤 바람·쓸림에 굴러감 (서버, 얽힘 표 Light)
};
