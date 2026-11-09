# 밤톨 술래잡기 — 코드 샘플 (Unreal Engine 5 C++)

귀여운 협동 술래잡기 모험 게임. 스팀 출시 준비 중이며 1인 개발입니다.
포트폴리오 사이트: https://dnguswp12345-spec.github.io

> 이 저장소는 전체 프로젝트(비공개)에서 고른 발췌본이라 **단독으로 빌드되지 않습니다.** 코드 읽기용입니다.

## 프로젝트 구조 (전체 프로젝트 기준)
1. 단일 게임 모듈 `BamtolChase`, UE 5.8 C++. 헤더 약 390개·소스 약 640개. EnhancedInput, StateTree, Niagara, UMG, EOS, Steamworks 사용.
2. 판 진행: GameMode(시작·종료), Lobby(술래 정하기), Catch(잡기·구출·탈출). 복제되는 GameState·PlayerState, CSV로 정하는 방 규칙.
3. 캐릭터: 도망자 `ABamtolPlayer`(활공·헤엄·오르기·들기)와 술래 대장 `ABamtolBoss`(능력·AI·StateTree).
4. 월드 상호작용: 우리·출구·표 자판기·버스·대포 등. 아이템·모자는 데이터 표 구동.
5. 서버 권한 복제·RPC, 로컬라이즈, 헤드리스 자동 시험 145개.

## 이 저장소의 파일 (Source/Public, Source/Private)
| 파일 | 무엇을 보여 주나 | 읽을 포인트 |
|---|---|---|
| BamtolGameState | 판 전체 상태를 모든 클라이언트에 복제 | `Replicated`/`ReplicatedUsing=OnRep_DayShift` 변수 다수, 신뢰 전송 `MulticastFeed`·`MulticastChat`·`MulticastAnnounce` |
| BamtolPlayerState | 플레이어별 복제 정보(겉모습·목소리·점수) | `OnRep_HatLooks`로 모자 모양 동기화, `GetLifetimeReplicatedProps` |
| BamtolCage | 잡힌 친구를 가두는 우리와 구출 흐름 | 서버 권한 상태 + `OnRep_FreezeCage`, 비신뢰 연출 `MulticastFx` |
| BamtolTraversal | 바람 구역·미끄럼틀·짚라인 이동 장치 | 에디터 배치 점들을 `OnRep_Points`/`OnRep_Ends`로 복제 (`ABamtolSlide`, `ABamtolZipline`) |
| BamtolPickable | 주울 수 있는 아이템과 채집 밭, 저장 | 데이터 표 행 `FBamtolPickableRow`, `UBamtolPickableWorld` 월드 서브시스템, `USaveGame` |
| BamtolHats | 모자 데이터 표와 선택 UI, 미끼 | `FBamtolHatRow : FTableRowBase`, `FindRow`로 데이터 구동, UMG 위젯 |
| BamtolCheckpoint | 모닥불 체크포인트·안개 문·지름길 문 | `ABamtolFogGate`의 상태값(`State`) 복제, `ABamtolRestFire` |

## 개발 방식
Claude Code와 AI 페어 프로그래밍으로 구현했습니다. 설계·기획·검수·디버깅, 그리고 자동 시험 구성은 제가 직접 주도했습니다.

## 저작권
All rights reserved. 채용 검토용 열람만 허용합니다.

---
**English summary**
- Excerpts from "Bamtol Chase", a co-op tag adventure game (solo dev, Steam release in preparation), written in Unreal Engine 5 C++.
- Shows server-authoritative replication (OnRep, multicast RPCs), data-table-driven items/hats, and world-interaction actors.
- Not buildable standalone (excerpt of a private project). All rights reserved; for hiring review only.
