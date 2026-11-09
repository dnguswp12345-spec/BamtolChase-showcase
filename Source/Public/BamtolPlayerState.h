#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "BamtolPlayerState.generated.h"

// 도망자 한 사람의 판 상태 (몸이 밤톨로 바뀌어도 남음)
UENUM(BlueprintType)
enum class EBamtolRunState : uint8
{
	Running,     // 아직 도망 중
	Escaped,     // 탈출 (1점)
	Caught,      // 잡혀서 밤톨이 됨
	Boss,        // 판 시작부터 술래(대장) — 도망자 수·점수에 안 셈
};

// 사람마다 남는 판 기록: 몸 색·모자·상태·점수·기록(달린 거리·아이템·맞힘·구출·버스표).
// 결과 화면·위쪽 명단·알림 피드가 읽는다.
UCLASS()
class BAMTOLCHASE_API ABamtolPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 새 PlayerState 로 옮길 때(이동·다시 접속) 마을 기록판 칸(점프 숲·과녁·버섯 달리기 최고)을 넘김 (2026-10-09: 이동하면 늘 "-")
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 LookColor = -1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 LookHat = -1;

	// 모자 꾸미기 고른 것 "Acorn=Acorn_Founder;…" (BamtolHatLook.h) — 바뀌면 그 몸의 모자를 다시 칠함
	UPROPERTY(ReplicatedUsing = OnRep_HatLooks, BlueprintReadOnly, Category = "Bamtol|Looks")
	FString HatLooks;

	// 목소리 흉내 허용 (서버가 그 사람 설정 + 방 공개 여부로 정함)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Voice")
	bool bAllowMimic = false;

	// 내 목소리 효과 (0 끔 · 1 로봇 · 2 확성기 · 3 메아리, 본인 설정)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Voice")
	uint8 VoiceStyle = 0;

	// 쓰는 칭호 (BamtolTitles.h) — 대기실·결과·이름표에 「칭호」
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Looks")
	FName Title;
	// 보이기로 한 나라 두 글자 (BamtolNation.h, 빈칸 = 안 보임) — 결과 화면 카드에만
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol")
	FString Nation;
	UFUNCTION()
	void OnRep_HatLooks();

	// 치장·도토리 (2026-10-12, BamtolCosmetics.h): 입은 치장 Id 몇 개(부위마다 하나) · 도토리 잔액(서버가 판 중 걸기를 판정할 때 봄, 주인은 그 사람 저장)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Looks")
	TArray<FName> CosWear;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Looks")
	int32 Acorns = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	EBamtolRunState RunState = EBamtolRunState::Running;

	// 판이 끝났을 때 받은 점수 (탈출 1, 생존 0.5, 잡힘 0)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	float RoundPoints = 0.f;

	// 어떻게 탈출했나 ("막차", "비밀 출구" …)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	FString EscapeHow;

	// 판 시작부터 탈출·잡힘까지 버틴 초
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	float AliveSeconds = 0.f;

	// ---- 기록 ----
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	float DistanceMeters = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 ItemsUsed = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 Hits = 0;            // 총으로 맞힌 수

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 Rescues = 0;         // 구출해 준 수

	// 밤송이 우리 (2026-10-07, BamtolCage.h): 이번 판 잡힌 횟수(3번째 = 탈락) · 지금 갇힌 단계(0 안 갇힘 · 1 · 2) · 탈락했나
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	uint8 CageHooks = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	uint8 CageStage = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	bool bCageOut = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 TicketsFound = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 Catches = 0;         // 밤톨이 되어 잡은 수

	// 꾸미기: 밤톨이 됐을 때 머리·아래 공 색 (BossPalette 번호, 0 = 기본 밤색)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 BossHead = 0;

	// 엔딩 단체 사진 포즈 (마지막에 한 감정 표현 번호 + 1, 0 = 안 함)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Photo")
	int32 PhotoPose = 0;

	// 몸 종류 (BamtolCharacters.h, 비면 기본) — 녀석일 때 · 밤톨이 됐을 때
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Looks")
	FName RunnerKind;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Looks")
	FName BossKind;

	// 닉네임 (비면 몸 색 이름으로 보임)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	FString Nickname;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 BossBall = 0;

	// 고른 몸 색 (-1 = 아무거나). 다른 사람이 이미 쓰면 안 바뀜
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	int32 PrefColor = -1;

	// 밤톨 색 판 (화면용 sRGB): 0 밤색 · 노랑 · 초록 · 하늘 · 분홍 · 보라 · 빨강 · 검정 · 흰색
	static int32 BossPaletteNum() { return 9; }
	static FLinearColor BossPalette(int32 Index);
	static FString BossPaletteName(int32 Index);

	// 대기실: 준비 완료 (모두 준비되면 5초 뒤 시작)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	bool bReady = false;

	// 대기실: "술래 하고 싶음" ([B]) — 방 설정이 "원하는 사람 중" 일 때 이 사람들 중에서 뽑음
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Player")
	bool bWantBoss = false;

	// 아랫마을 기록 (기록판): 과녁 점수 (버섯 달리기 기록 RaceBest 는 2026-10-28 지움 — 달리기를 10-12 뺌)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Village")
	int32 TargetScore = 0;
	// 과녁 30초 도전 (2026-10-04 "올림픽처럼"): 끝 서버 시각(0 = 안 함)·이번 점수·내 최고
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Village")
	float TargetChalEnd = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Village")
	int32 TargetChalScore = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Village")
	int32 TargetChalBest = 0;
	// 점프 숲 (2026-10-08, BamtolJumpCourse.h): 저장된 단계(1~) · 도전 시작 서버 시각(<0 = 안 함) · 떨어진 횟수 · 끝낸 기록(초, 0 = 아직)
	UPROPERTY(ReplicatedUsing = OnRep_JumpStage, BlueprintReadOnly, Category = "Bamtol|Jump")
	uint8 JumpStage = 1;
	// 단계를 바꿀 때는 이것으로 (서버). 바뀌면 OnJumpStageChanged 가 울림 — 서버는 여기서, 손님은 복제가 도착할 때(OnRep).
	//   듣는 곳: 탑 층 불러오기·층 문 이야기(BamtolTowerFloors.cpp) — 매 프레임 단계를 들여다보지 않게.
	void SetJumpStage(int32 Stage);
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnJumpStage, ABamtolPlayerState* /*누구*/, int32 /*전*/, int32 /*지금*/);
	static FOnJumpStage OnJumpStageChanged;
	UFUNCTION()
	void OnRep_JumpStage(uint8 Old);
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Jump")
	float JumpRunStart = -1.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Jump")
	int32 JumpFalls = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Jump")
	float JumpLastTime = 0.f;

	// 화면에 쓸 이름: 몸 색 이름("민트"). 색이 없으면 접속 이름
	UFUNCTION(BlueprintPure, Category = "Bamtol|Player")
	FString ShowName() const;

	// 몸 색 이름·화면 색 (0 민트 · 1 산호 · 2 하늘 · 3 레몬 · 4 보라 · 5 분홍 · 6 연두 · 7 크림)
	static FString ColorName(int32 Index);

	// 몸 색 목록 (한 곳에서): 16색. 재질 MI_Bamtol_Body_<ColorKey>, 얼굴 아이콘 T_Portrait_<PortraitKey>
	static constexpr int32 NumColors = 16;
	static const TCHAR* ColorKey(int32 Index);
	static const TCHAR* PortraitKey(int32 Index);   // 범위 밖 = Boss2 (밤톨 얼굴)
	static class UTexture2D* PortraitOf(int32 Index);

	// 스트리머 모드(이 화면 설정)면 남의 닉네임을 몸 색 이름으로 (방송에 친구 이름이 안 나가게)
	bool IsNameHiddenHere() const;
	// 글자 속 남의 닉네임 → 몸 색 이름 (피드·공지·채팅처럼 서버가 이름을 넣어 보낸 글)
	static FString MaskNames(const UObject* Ctx, const FString& Text);
	static FLinearColor ColorOf(int32 Index);

	// 컨트롤러(또는 폰)의 화면 이름
	static FString NameOf(const class AController* C);
	static FString NameOf(const class APawn* P);
};
