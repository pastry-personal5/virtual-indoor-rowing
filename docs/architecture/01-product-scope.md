# Product scope and quality bar

Status: Initial reference  
Owner: Product and architecture  
Last reviewed: 2026-09-13

## Product statement

Virtual Indoor Rowing (working name, abbreviated **VIR**) turns live output from a supported Concept2 rower into a responsive boat in a persistent 3D world. It combines credible rowing data, structured training, solo routes, ghosts, and fair online races. The product must still record a trustworthy workout when the internet fails or the 3D renderer is under load.

The launch configuration is deliberately narrow: a MacBook Pro with M5 Max/128 GB running macOS Tahoe 26.6.2 or later, and a Concept2 Model D fitted with a supported-firmware PM5. Narrow hardware scope lets the team earn reliability before widening compatibility.

## Product principles

1. **The PM is the measurement authority.** Never invent distance, power, pace, or stroke data from animation or keyboard input.
2. **Exercise continuity beats spectacle.** A render, content, cloud, or integration failure must not silently lose an in-progress workout.
3. **Local feedback is immediate; competition is server authoritative.** The client predicts presentation, while ranked results come from validated server state.
4. **Offline is a supported mode.** Solo routes and workouts remain usable without an account or network.
5. **Performance data is private by default.** Sharing, leaderboards, heart-rate collection, and third-party export are explicit choices.
6. **The machine cannot follow a virtual slope.** A Model D damper is manually set. Hills, wakes, and drafting may affect presentation or unranked game rules, but must not be portrayed as physical resistance control.
7. **A game is not a medical device.** Training guidance uses transparent sport rules and user-configured zones, not diagnosis or treatment claims.

## Primary users and journeys

| User | Core journey | Success condition |
|---|---|---|
| New rower | Pair PM5, learn the HUD, complete a short guided row | First valid session saved with no setup ambiguity |
| Fitness rower | Follow time/distance/interval workouts and review trends | Targets remain legible during exertion; history is accurate |
| Competitive rower | Warm up, join a scheduled race, receive a ranked result | Common start, low-jitter rivals, auditable result |
| Social rower | Meet friends with a code and row the same route | Presence survives transient network trouble |
| Offline rower | Row a downloaded route without signing in | Full local session and later optional sync |
| Support operator | Diagnose connection or session failure with consent | Sanitized bundle identifies firmware, state transitions, and errors |
| Event operator | Schedule, observe, and adjudicate a race | Reproducible event state and immutable audit trail |

## Functional scope

Each identifier is stable and should be referenced by epics, tests, and release evidence.

### First-generation capability envelope

This table describes the intended first commercial product generation, not one MVP backlog. “Phase” means the capitalized product-delivery phases defined in the [delivery plan](10-delivery-plan.md). The earliest delivery phase is the first phase in which the capability may be committed, not a promise that it is funded or complete; later work remains contingent on the preceding technical and product investment gates. See also the [executive review](00-executive-review.md) and proposed [ADR-0007](../adr/0007-evidence-gated-product-delivery.md).

| ID | Capability | Earliest delivery phase | Notes |
|---|---|---|---|
| FR-001 | First-run onboarding and accessibility setup | Phase 2 | Large type, high contrast, audio levels, units, privacy choices; minimal setup begins in Phase 1 |
| FR-002 | PM5 discovery, explicit pairing, reconnect, and diagnostics | Phase 1 | BLE launch transport; latest supported firmware matrix |
| FR-003 | Live rowing HUD | Phase 1 | Distance, time, pace/500 m, watts, stroke rate, heart rate when supplied, connection quality |
| FR-004 | Just Row | Phase 1 | Select a downloaded route and row without an account or network |
| FR-005 | Structured workouts | Phase 2 | Fixed time, fixed distance, intervals, rests, targets, warm-up/cool-down |
| FR-006 | Responsive virtual boat and avatar | Phase 1 | Gray-box response in Phase 1; production presentation in Phase 2; normalized PM data only |
| FR-007 | Durable local history | Phase 1 | Session summary, intervals, samples, crash recovery, export |
| FR-008 | Account and cloud synchronization | Phase 3 | Passwordless/OIDC account; guest-to-account migration |
| FR-009 | Personal best and ghost replay | Phase 2 | Versioned, privacy-aware ghosts derived from completed sessions |
| FR-010 | Friends/private group row | Phase 4 | Join code, presence, predefined emotes; no free-text or voice at launch |
| FR-011 | Ranked race | Phase 4 | Lobby, warm-up, synchronized countdown, false-start rule, provisional and finalized result |
| FR-012 | Training history and trends | Phase 2 | Volume, pace, power, stroke rate, intervals, configurable zones |
| FR-013 | Content catalog and secure updates | Phase 2 | Versioned worlds/routes, compatibility checks, resumable download |
| FR-014 | Concept2 Online Logbook export | Phase 3 | Explicit link and per-session opt-out; server-side delivery with retries |
| FR-015 | FIT activity export | Phase 2 | User-owned file suitable for compatible fitness services |
| FR-016 | Subscription entitlement | Phase 3 | Provider adapter; implementation contingent on pricing evidence; never interrupt an active workout |
| FR-017 | Support and operator tooling | Phase 1 | Redacted local diagnostics first; account operations arrive in Phase 3 and race operations in Phase 4 with their supported workflows |
| FR-018 | Account data export and deletion | Phase 3 | Ships with account storage; includes integration-token revocation and retention status |

### Deferred scope

- PM3/PM4 and USB support; the adapter interfaces permit it, but it is not a launch promise.
- Windows, iOS, iPadOS, Android, Apple TV, visionOS, and VR.
- User-authored 3D worlds or executable mods.
- Open text chat, voice chat, public user-generated content, and direct messaging.
- Automatic mechanical resistance, steering hardware, or motion-platform control.
- Elite-event certification, prize-money anti-cheat guarantees, or a claim that ordinary BLE proves the identity of a physical rower.
- Apple Health writing from macOS. Apple documents that macOS has no HealthKit store; a future iPhone/watch companion is a separate product.
- Medical plans, injury diagnosis, calorie prescriptions, or emergency monitoring.

## Quality attributes and service objectives

The reference Mac and PM5 hardware-in-loop rig are the acceptance environment. Unless noted, percentiles are measured over a 60-minute session after shader warm-up.

| ID | Attribute | Launch objective |
|---|---|---|
| QA-001 | Render responsiveness | Sustained 60 fps at 2560×1600 High preset; p95 frame time ≤16.7 ms; no sustained thermal throttling |
| QA-002 | Telemetry-to-HUD latency | p95 ≤50 ms from parsed BLE notification to visible HUD update; physical stroke-to-display p95 ≤200 ms at PM 100 ms sampling |
| QA-003 | Local durability | Checkpoint within 1 s; a process crash loses at most the uncommitted second and marks recovery explicitly; sudden power-loss behavior is measured and reported separately |
| QA-004 | Device recovery | In-range transient disconnect reconnects p95 ≤5 s; all gaps are visible in data quality flags |
| QA-005 | Online latency | Choose a region with pre-race median RTT ≤100 ms and p95 ≤150 ms; otherwise warn and permit unranked play |
| QA-006 | Race simulation | 20 Hz authoritative tick; p99 tick work ≤25 ms; 10 Hz state snapshots |
| QA-007 | Availability | Monthly control-plane and race-entry SLO 99.9%, excluding announced maintenance; offline solo remains available |
| QA-008 | Crash-free sessions | ≥99.5% in public beta and ≥99.8% at GA, measured per started workout |
| QA-009 | Session correctness | No duplicate finalized sessions; distance/time summary matches accepted PM source within source resolution |
| QA-010 | Accessibility | Full setup/menu keyboard navigation, scalable HUD, color-independent target feedback, subtitle/cue equivalents |
| QA-011 | Startup | Warm start to local menu p95 ≤8 s with cached shaders/content; usable offline if cloud is unreachable |
| QA-012 | Update safety | From Phase 4 (ADR-0008): every executable is signed/notarized; update feed and archive are independently signed; rollback artifact retained |
| QA-013 | Privacy operations | Export request begins immediately; deletion completes within the published policy window and is auditable |
| QA-014 | Observability | Every cloud request and race has a correlation ID; client logs contain no access token, raw email, or raw PM serial |

Objectives are initial and must be converted into automated release gates where possible. A failed objective is not hidden by changing telemetry; product/engineering either fixes it or explicitly revises this document.

## Product invariants

- One `session_id` represents exactly one physical workout attempt.
- A client restart never turns a recovered session into an apparently uninterrupted verified session.
- Ranked progress is based on accepted PM cumulative distance and PM elapsed time, never on client avatar position.
- Server correction changes presentation gradually but ranking immediately.
- Samples are immutable after finalization; corrections create a new derived version and audit record.
- A session becomes `live_supported_source` only after live data, firmware/capability checks, and integrity rules pass. The label does not mean hardware or human identity attestation.
- Content and ruleset hashes are fixed for the lifetime of an online race.
- Local session completion does not wait for cloud or third-party integrations.
- Entitlement expiry, update prompts, and login refresh never cover or terminate an active workout.

## Assumptions to validate

| Assumption | Validation | Exit criterion |
|---|---|---|
| Tahoe version intent is 26.6.2 | Product owner confirmation and CI host check | Requirement corrected or accepted in ADR-0001 |
| PM5 BLE stays connected for long rows on the target Mac | 2-hour repeated hardware-in-loop soak across supported firmware | No unexplained disconnect; recovery objective met |
| 100 ms PM status notifications are sustainable with subscribed characteristics | Packet capture and notification-loss test | Loss <0.1% in clean RF; decoder handles gaps |
| Unreal 5.8 and Xcode 26.1.1 can build and run on Tahoe 26.6.2 | Packaged vertical slice, not Editor-only | Unsigned, ad-hoc-built arm64 app launches via Gatekeeper right-click-Open and passes the Bluetooth prompt; notarization is a Phase 4 requirement per ADR-0008 |
| Custom Go race protocol meets rowing latency needs | WAN-emulated 16-rower ranked and 64-rower group tests | QA-005/006 and convergence tests pass |
| Full-fidelity world holds 60 fps thermally | Representative route and HUD soak | QA-001 passes without editor overhead |

## Glossary

| Term | Meaning |
|---|---|
| PM | Concept2 Performance Monitor; the data-producing computer attached to the rower |
| PM5 | Bluetooth-capable fifth-generation Concept2 monitor |
| CSAFE | Fitness-equipment command protocol transported by PM interfaces |
| Device sample | Normalized point-in-time metrics derived from one or more PM notifications |
| Session | One attempted physical workout from preparation through finalization |
| Workout plan | Immutable, versioned prescription of work/rest steps and targets |
| Route definition | Engine-independent mapping of canonical course distance to content metadata |
| Race ruleset | Immutable validation, start, finish, ranking, and penalty rules for a race |
| Ghost | A privacy-filtered replay generated from a completed session |
| Control plane | Accounts, catalog, history, match creation, entitlements, and administrative APIs |
| Real-time plane | Short-lived authoritative race rooms and state distribution |
| Delivery phase | One of the capitalized Phase 0–5 product outcomes and exit gates defined in the delivery plan; every phase contains one or more milestones and may contain many |
| Investment level | An evidence-gated funding decision proposed by ADR-0007; Foundation spans delivery Phases 0–1 and each later level maps to one phase |
| Milestone | A bounded unit of work belonging to exactly one delivery phase; identifiers use `Phase <N> Milestone <M>`, and milestone completion does not imply that the delivery-phase exit gate has passed |
