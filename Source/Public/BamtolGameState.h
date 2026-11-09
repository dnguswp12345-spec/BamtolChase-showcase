#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "BamtolHighlights.h"
#include "BamtolPing.h"
#include "BamtolRoomRules.h"
#include "BamtolGameState.generated.h"

UENUM(BlueprintType)
enum class EBamtolRoundPhase : uint8
{
	Playing,
	RunnersWin,
	BossWin,
};

// 알림 피드 종류 (오른쪽 위 줄마다 그림·색이 다름)
UENUM(BlueprintType)
enum class EBamtolFeed : uint8
{
	Info,
	Caught,
	Escaped,
	Rescue,
	Ticket,
	Hit,
	// 승부 순간 (BamtolGamble.h, 2026-10-12): 대박 · 당첨 · 꽝 딱지
	Jackpot,
	Lucky,
	Bust,
};

// 화면에 쌓이는 알림 한 줄 (각 화면에만 있음, 복제 안 함)
struct FBamtolChatLine
{
	FString Name;
	FString Text;
	int32 Color = -1;
	double Time = 0.0;
};

struct FBamtolFeedLine
{
	FString Text;
	EBamtolFeed Kind = EBamtolFeed::Info;
	int32 Color = -1;
	double Time = 0.0;          // 피드: 들어온 때 · 공지: 사라질 때 (0 = 아직 차례 안 옴)
	// 공지 줄 세우기 (2026-10-06 "알림 띠가 3개씩 겹쳐 뜸" → 한 번에 하나, 우선순위, 같은 내용 합치기)
	uint8 Priority = 1;         // 0 작은 소식 · 1 보통 · 2 판 시작 목표 · 3 급함(출구 닫힘·버닝·표 다 모음 — 지금 것을 밀어냄)
	double Born = 0.0;          // 들어온 때
	float Show = 3.f;           // 보여 줄 초
};

// 한 판 상태 (모두에게 복제). 화면 표시(HUD/WBP)는 여기 값을 읽는다.
UCLASS()
class BAMTOLCHASE_API ABamtolGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// 마을 침대 (BamtolBed.h, 2026-11-05): 모두 누우면 서버가 하루를 넘김 → 방 사람 모두의 BamtolDaily::TodayKey 가 같이 하루 뒤로
	UPROPERTY(ReplicatedUsing = OnRep_DayShift)
	int32 DayShift = 0;
	UFUNCTION()
	void OnRep_DayShift();
	void AdvanceDay();   // 서버

	// 등반 놀이(PlayMode 2): 출발한 서버 시각 (-1 = 아직). 화면 등반 시계
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float ClimbStartServerTime = -1.f;

	// 등산 모드(산 코스, BamtolClimbMode.cpp): 대장이 산 아래에서 굴러 나오는 서버 시각 (-1 = 대장 없음). 화면 "대장 출발까지 N초"
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float ClimbBossAt = -1.f;

	// 데굴산 원정 판 (2026-10-21, BamtolExpedition.h): 등반 코스 + 재료 덩어리·가방·말뚝. 정상 출구 없음, 시계는 지난 시간
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bExpedition = false;

	// 대기실 자리 (2026-10-04): 줄 번호마다 0 빈자리 · 1 AI 친구 · 2 닫힘 (사람이 앉은 줄은 무시). 방장이 눌러서 바꿈
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	TArray<uint8> LobbySlots;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float TimeLeft = 300.f;

	// 방 코드 (방장이 만든 온라인 방, 대기실에 보임)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FString RoomCode;

	// 방 이름 (방장이 만들기 창에서 고른 것, 빈칸 = 자동) — 손님 대기실 제목에도 (2026-10-28)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FString RoomName;

	// 방 연결 (2026-10-12 빠른 참가, UBamtolOnline::TickHost 가 채움): 방장의 PlayerId(손님 화면의 "방장" 딱지) ·
	//   방장이 나가면 방을 이어 열 사람(연결 좋은 손님, -1 = 없음 — 모두 같은 답을 갖게 방장이 미리 정해 둠) · 빠른 참가를 받는 방인가(모르는 사람이 올 수 있음)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Net")
	int32 NetHostPid = -1;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Net")
	int32 NetHeirPid = -1;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Net")
	bool bQuickRoom = false;
	// 판 도중 참가를 받는 방인가 (2026-10-28, UBamtolOnline::HostAcceptsMidJoin — TickHost 가 채움): 대기실 카드에 "도중 참가 받음/안 받음"
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Net")
	bool bMidJoinRoom = true;

	// 방 음성 (방장 설정): false 면 방 전체 게임 음성 끔
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bRoomVoice = true;

	// 판 시간(분, 방장 설정) — 대기실에 보여 줌
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 RoundMinutes = 8;

	// 술래 정하기 (방장 설정 BossMode) · 방장이 지정한 사람 이름 (대기실 표시용)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 BossMode = 0;

	// 날씨 (BamtolWeather: 0 맑음 1 흐림 2 노을 3 안개 4 비) · 실시간(유닉스 30분 칸)인지 방장 고정인지
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 Weather = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bWeatherLive = true;

	// 마을 하늘 보상 (2026-10-27, BamtolSky.h): 방장이 고른 하늘 (하늘 표 줄 이름, 없음 = 그날 날씨). 마을에서만 정함
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FName VillageSky;

	// 빈자리 AI 친구: 이 인원까지 채움 (0 = 안 채움, 방장 설정) — 대기실 표시용
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 BotFill = 8;

	// AI 대장 난이도 0 쉬움 · 1 보통 · 2 어려움 · 3 악몽 (방장 설정, 사람 대장에겐 상관없음)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 BossDifficulty = 1;

	// 방 규칙 (방장 설정 → 대기실·판 시작 때 옮김, BamtolRoomRules.h): 1 아이템 · 2 친구 던지기 · 4 산의 기분
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 RuleFlags = 191;   // 128 숲 친구(2026-10-12, BamtolFriendGimmicks.h) · 64 우리 탈락하면 관전 · 1 아이템 · 2 던지기 · 4 산의 기분 · 8 도망자 핑 · 16 대장 핑 · 32 쿼터뷰 시야 가림(쿼터뷰만 방, 2026-10-06 BamtolFogView.h)

	// 방장 규칙 공유 코드 (손님도 복사해 갈 수 있게) — 2026-10-12: 손님은 이 코드를 풀어 방 규칙 전체(RuleSet)를 읽음
	UPROPERTY(ReplicatedUsing = OnRep_RuleCode, BlueprintReadOnly, Category = "Bamtol|Round")
	FString RuleCode;

	UFUNCTION()
	void OnRep_RuleCode();

	// 방 규칙 값 (2026-10-12, BamtolRoomRules.h — Data/RoomRules.csv 줄 순서, 복제 안 함: 서버 = CopyRules · 손님 = RuleCode 를 풀어서)
	//   판 중 값은 BamtolRules::Live(this, TEXT("Id")) 로 읽는다 (판이 아니면 기본값)
	TArray<int32> RuleSet;
	// 방장이 고른 규칙 그대로 (깜짝 규칙을 덮기 전) — 기록 제외 판정·방 코드는 이쪽
	TArray<int32> RuleBaseSet;
	// 깜짝 규칙 (2026-10-12, Data/SurpriseRules.csv): 이 판에 뽑힌 후보 Id (없으면 None) — 서버가 판 시작 전에 한 번 뽑음, RuleSet 에 덮여 있음
	UPROPERTY(ReplicatedUsing = OnRep_RuleCode, BlueprintReadOnly, Category = "Bamtol|Round")
	FName SurpriseId;
	bool bSurprisePicked = false;
	int32 RuleSerial = 0;   // RuleSet 이 바뀔 때마다 +1 (녀석·대장이 자기 몸에 다시 적용)
	// 방 규칙이 먹는 판인가: 술래잡기 판 중만 (마을·대기실·점프 숲·사격장·등반·알밤이 잡기는 늘 기본값)
	bool RulesLive() const { return !bTitle && !bLobby && !bPlayground && PlayMode == 0; }
	// 녀석·대장이 "규칙이 바뀌었나" 볼 때 쓰는 값 (판 여부까지)
	int32 RuleStamp() const { return RuleSerial * 2 + (RulesLive() ? 1 : 0); }

	// 방 규칙 "술래 눈 가리기"(숨바꼭질): 남은 초 (0 = 아님). 그동안 큰 술래는 못 움직이고 화면이 가려짐
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 BlindLeft = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 BossSpeedPct = 100;

	// 방 설정: 맵 크기 0 작게 · 1 중간 · 2 크게(산 전체) / 미션 수 0 적게 · 1 보통 · 2 많이
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 MapSize = 2;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 MissionLevel = 1;

	// 방 규칙: 시점 0 자유 · 1 3인칭만 · 2 쿼터뷰만 (판에서만, 대기실은 자유 — BamtolComfort::View)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 ViewRule = 0;

	// 방 규칙: 판 구조 0 새(추격) · 1 옛 (대기실 표시) / 이번 판이 새 구조인가 (작은 맵 4개 + 기본 놀이 + 새, 판 시작 때 서버가 정함 — BamtolPace.h)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	uint8 RoundStyle = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bChaseRound = false;

	// 이번 판이 "갇힘 + 구출 + 횟수 제한" 규칙인가 (새 구조 판, 2026-10-07 BamtolCage.h)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bCageRound = false;

	// 이번 판 대장 종류 (Data/Characters.csv 대장 줄 Id, 비면 기본 = 첫 줄) — 2026-10-10 꼬마 술래 = 그 판 대장을 닮은 작은 몸
	//   판 시작 때 서버가 정함(사람 술래 = 그 사람이 고른 대장 몸 · AI 대장 = cvar Bamtol.BossKind 또는 기본) → BamtolChar::RoundBoss
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FName RoundBossKind;

	// 판 맵 (BamtolMaps.h, 비면 데굴산) — 대기실 표시용
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FName RoundMap;

	bool RuleItems() const { return (RuleFlags & 1) != 0; }
	bool RulePingRunners() const { return (RuleFlags & 8) != 0; }
	bool RulePingBoss() const { return (RuleFlags & 16) != 0; }
	bool RuleQuarterFog() const { return (RuleFlags & 32) != 0; }
	bool RuleCageSpectate() const { return (RuleFlags & 64) != 0; }   // 우리 탈락하면 관전 (아니면 작은 밤톨, 2026-10-07)

	// 핑 (BamtolPing.h): 서버가 넣고 8초 뒤 뺌
	UPROPERTY(Replicated)
	TArray<FBamtolPing> Pings;
	// 목표 깃발 (배그 지도 마커처럼): 한 사람 1개, 안 사라짐. Kind = Target, Color = 몸 색 (2026-10-05)
	UPROPERTY(Replicated)
	TArray<FBamtolPing> Targets;
	bool RuleThrow() const { return (RuleFlags & 2) != 0; }
	bool RuleMood() const { return (RuleFlags & 4) != 0; }
	bool RuleFriends() const { return (RuleFlags & 128) != 0; }   // 숲 친구 판 중 기믹 (기본 켬)

	// 방 규칙: 판 안 승부 순간(복불복 뽑기 통·두 배 북·박 터뜨리기·우리 제비뽑기 — BamtolGamble.h, 2026-10-12). 끄면 그 판에 아무것도 안 놓이고 [F] 도 안 먹음.
	//   RuleFlags 8비트가 다 차서 따로 둔 칸. 방장 설정 → 이 값으로 옮기는 줄은 방 규칙 쪽(CopyRules)에서. 시험 cvar Bamtol.GambleRule(0/1)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bGambleOn = true;
	// 방장 설정 → 규칙 칸 (서버)
	void CopyRules(const class UBamtolGameUserSettings* S);

	// 방 크기(8·16, 방장 설정) · 이번 판 큰 대장 수(1·2)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 RoomSize = 8;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 BossCount = 1;

	// 놀이 (방장 설정 PlayMode): 0 기본 · 1 알밤이 잡기
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 PlayMode = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FString DesignatedBoss;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 Runners = 0;          // 시작할 때 도망자 수

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 Escaped = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 Caught = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	EBamtolRoundPhase Phase = EBamtolRoundPhase::Playing;

	// 버스표: 모은 수 / 막차 부르는 데 필요한 수
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 Tickets = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	int32 TicketsNeeded = 0;

	// 버닝 타임: 판 마지막 N초. 밤톨이 빨라지고 도망자 위치가 다 보임
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bBurning = false;

	// 벽 투시 줄이기가 켜진 방 (BamtolHideFar.h): 술래 손님 화면에 먼 도망자 몸이 안 올 수 있음 → 몸이 안 온 사람의 근접 음성은 끔
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bHideFar = false;

	// 결과 이유 ("탈출 1 + 생존 2 × 0.5 = 2 / 필요 2")
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FString ResultReason;

	// 이번 판 이야기 (판 끝, 명장면 기록에서 재밌는 일 몇 개를 시간 순서로 "1:12 민트, 32m 날아감!")
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	TArray<FString> RoundStory;

	// 모자 고르기 남은 시간 (-1 = 아직 시작 전, 0 = 끝)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float HatPickLeft = -1.f;

	// 마지막 1명: 5초마다 위치가 드러남 (밤톨 화면에 잠깐 표시)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FVector RevealPos = FVector::ZeroVector;

	// 마지막 1명 (연출: 음악·카드·붉은 맥박) — 없으면 nullptr
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	TObjectPtr<APlayerState> LastRunnerPS;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float RevealServerTime = -100.f;

	// 버닝 때 곧 닫히는 출구 이름과 남은 초 (HUD)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	FString ClosingExit;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float ClosingIn = 0.f;

	// 타이틀 화면 중 (판 시계·밤톨 멈춤). 시작하기 → GameMode::StartFromTitle
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bTitle = false;

	// 대기실 중 (여럿이 할 때: 방 만들기·참가 → 모두 준비 → 시작). 판 시계·밤톨 멈춤, 산에서 뛰어다니며 기다림
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bLobby = false;

	// 이번 판 끝에 명장면을 보여 주나 (방장 설정 "판 끝 명장면 보기", 2026-10-09) — 꺼지면 손님도 건너뜀
	UPROPERTY(Replicated)
	bool bHighlightsOn = false;

	// 놀이 레벨 (2026-10-08): 점프 숲(L_JumpForest)·축제 — 판·준비·대기실 판 없이 뛰어놀기만 (bLobby 도 켬 = 판 시계 멈춤)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	bool bPlayground = false;

	// 대기실: 서버 출발 시각 한 번만 복제. 남은 초는 각 화면의 동기화된 서버 시계로 계산.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float LobbyDepartureAt = -1.f;
	float LobbyCountdownRemaining() const { return LobbyDepartureAt < 0.f ? -1.f : FMath::Max(0.f, float(LobbyDepartureAt - GetServerWorldTimeSeconds())); }

	// 판이 아직 안 시작됨 (타이틀·대기실)
	bool IsWaiting() const { return bTitle || bLobby; }

	// 판이 끝난 뒤 다음 판까지 남은 초 (-1 = 아직 판 중)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bamtol|Round")
	float NextRoundIn = -1.f;

	// 판 끝 넘기기 (BamtolEndSkip.h, 2026-10-05): 단계 0 끝 장면 · 1 명장면 · 2 단체 사진 · 3 결과
	//   EndStage = 서버가 보는 지금 단계, EndSkipTo = 넘겨서 모두가 적어도 이 단계로 가야 함, 표 = 넘기기 누른 사람 / 사람 수(AI 빼고)
	UPROPERTY(Replicated)
	uint8 EndStage = 0;
	UPROPERTY(Replicated)
	uint8 EndSkipTo = 0;
	UPROPERTY(Replicated)
	uint8 EndSkipVotes = 0;
	UPROPERTY(Replicated)
	uint8 EndSkipNeed = 0;

	// 3판 이어달리기·재판소 (2026-10-26, BamtolRelay.h · BamtolRelayLeg.h · BamtolCourt.h)
	//   RelayRound = 묶음 몇 판째(0 = 이어달리기 아님 · 1 술래잡기 · 2 점프 숲 · 3 탑) / RelayRounds = 한 묶음 판 수(3)
	//     바뀌면 내 화면에 이어달리기 안내 줄(남은 시간·도토리)을 띄움 (OnRep_RelayRound — 몸통 BamtolSpectatorAcorn.cpp)
	//   RelayGoal = 2·3판 목표 단계 · RelayEndsAt = 2·3판이 끝나는 서버 시각(-1 = 안 셈) · RelayThrowers = 3판에 먼저 도착해 도토리를 던질 수 있는 사람
	//   CourtDefendant = 이번 판 피고(없으면 재판 없음) · 표 수 · CourtVerdict 0 표 받는 중 · 1 용서 · 2 감옥
	UPROPERTY(ReplicatedUsing = OnRep_RelayRound)
	uint8 RelayRound = 0;
	UPROPERTY(Replicated)
	uint8 RelayRounds = 0;
	UPROPERTY(Replicated)
	uint8 RelayGoal = 0;
	UPROPERTY(Replicated)
	float RelayEndsAt = -1.f;
	UPROPERTY(Replicated)
	TArray<TObjectPtr<APlayerState>> RelayThrowers;
	UFUNCTION()
	void OnRep_RelayRound();
	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> CourtDefendant;
	UPROPERTY(Replicated)
	uint8 CourtJail = 0;
	UPROPERTY(Replicated)
	uint8 CourtForgive = 0;
	UPROPERTY(Replicated)
	uint8 CourtVerdict = 0;

	// 오른쪽 위 피드 한 줄 (잡힘·탈출·구출·버스표). Color = 몸 색 번호(그림 동그라미)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFeed(EBamtolFeed Kind, const FString& Text, int32 Color);

	// 글자 채팅 한 줄 (모두에게). 화면은 UBamtolChatWidget
	UFUNCTION(NetMulticast, Reliable)
	void MulticastChat(const FString& Name, int32 Color, const FString& Text);
	TArray<FBamtolChatLine> ChatLog;
	bool bStrangerChatNoted = false;   // 이 화면에서 "모르는 사람 글 숨김 중" 안내를 이미 띄웠나 (복제 안 함)

	// 각 화면에 쌓인 피드·큰 공지 (UI 가 읽고 시간이 지나면 지움)
	TArray<FBamtolFeedLine> FeedLog;
	TArray<FBamtolFeedLine> AnnounceLog;
	void PushAnnounce(const FString& Text, float Seconds, uint8 Priority = 1);

	// 이기려면 탈출해야 하는 수 (절반 올림)
	UFUNCTION(BlueprintPure, Category = "Bamtol|Round")
	int32 NeedToWin() const { return FMath::Max(1, (Runners + 1) / 2); }

	// 모두의 화면에 큰 글자 안내
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(const FString& Text, float Seconds = 3.f, uint8 Priority = 1);

	// 판 끝: 오늘의 명장면 (BamtolHighlights) — 각 화면이 끝 장면 뒤에 다시 재생
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHighlights(const TArray<FBamtolClip>& Clips);

	// 장면 하나씩 보냄 (2026-10-09: 한 번에 여러 장면이면 네트워크 묶음 한도(64KB)를 넘어 ensure IsBunchTooLarge) — Index 0 이 오면 비우고 시작
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHighlightClip(const FBamtolClip& Clip, uint8 Index, uint8 Count);

	// 나무 쓰러짐 (각 화면에서 UBamtolTreeFall::Fell)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastTreeFall(FVector_NetQuantize Base, FVector_NetQuantizeNormal Dir);
};
