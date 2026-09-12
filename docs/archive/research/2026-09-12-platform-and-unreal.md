# Research: macOS, Unreal Engine, and build platform

Research date: 2026-09-12  
Purpose: Validate the platform baseline and identify current Unreal/macOS constraints.  
Authority: Point-in-time evidence only; accepted decisions live in ADRs.

## Executive findings

1. Apple identifies Tahoe as **macOS 26**. The architecture baseline is macOS Tahoe 26.6.2 or later.
2. Epic's current Unreal Engine 5.8 macOS requirements identify Sonoma 14.5 as minimum, latest Sequoia 15 as recommended, Xcode 26.0 as minimum, and Xcode 26.1.1 as recommended. The same table explicitly says Xcode 26.4 is incompatible with UE 5.8.
3. Apple's Xcode compatibility table says Xcode 26.1.1 supports macOS Sequoia 15.6 through Tahoe 26.x. This makes UE 5.8 + Xcode 26.1.1 + Tahoe 26.6.2 a documented intersection, though only a real build/package test can approve it.
4. Epic lists Apple silicon M3 and 32 GB or more as recommended macOS hardware. The requested M5 Max/128 GB exceeds those broad recommendations, but actual renderer/thermal budgets still need measurement.
5. Epic describes Nanite/Virtual Shadow Maps on Apple silicon M2+ as beta and says hardware ray tracing/MegaLights are not currently supported in the macOS requirements. Lumen software ray tracing and TSR are supported on Apple silicon. Therefore experimental features should not be required for launch content.
6. Epic has supported native Apple-silicon editor/game binaries for several versions. Since only the supplied Apple-silicon target is promised, an arm64-only launch reduces native dependency/package testing.
7. Unreal BuildGraph is Epic's supported graph system around UnrealBuildTool/AutomationTool and supports macOS shell execution/build-farm decomposition. It is suitable for the repeatable Shipping pipeline.
8. Unreal is downloadable/free to begin, but the current standard game terms are not necessarily royalty-free: Epic's release page states 5% on worldwide gross revenue over the first USD 1 million, with a possible 3.5% program rate under its stated conditions. Finance/legal must use the actual EULA and product facts.
9. Apple recommends Developer ID signing and notarization for direct macOS distribution. Notarization requires valid signatures, Hardened Runtime, secure timestamps, and appropriate Developer ID identity; `get-task-allow` must not be in distribution entitlements.

## Platform version

Apple's [macOS Tahoe 26 release notes](https://developer.apple.com/documentation/macos-release-notes/macos-26-release-notes) state that the macOS 26 SDK develops apps for Macs running Tahoe 26. Apple's [Tahoe announcement](https://www.apple.com/newsroom/2025/06/macos-tahoe-26-makes-the-mac-more-capable-productive-and-intelligent-than-ever/) also names it macOS Tahoe 26.

Use supported platform APIs rather than string ordering when checking the runtime version.

## Toolchain intersection

Epic's [UE 5.8 macOS development requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine) are the engine authority. At research time the version table lists:

| UE | Minimum macOS | Recommended macOS | Minimum Xcode | Recommended Xcode | Note |
|---|---|---|---|---|---|
| 5.8 | Sonoma 14.5 | latest Sequoia 15 | 26.0 | 26.1.1 | Xcode 26.4 incompatible |
| 5.6–5.7 | Sonoma 14.0 | latest Sonoma 14 | 15.2 | 15.4+ | historical comparison |

Apple's [Xcode system requirements](https://developer.apple.com/xcode/system-requirements) list Xcode 26.1.1 as runnable on Sequoia 15.6 through Tahoe 26.x. Later Xcode releases exist, but “newer” is not equivalent to “supported by the selected Unreal version.”

Research implication: pin Xcode 26.1.1 and the exact UE 5.8 patch/fingerprint. Maintain a non-blocking compatibility lane for newer macOS patches/Xcode/UE. Promotion needs compile, cook, link, package, code-sign, notarize, clean-machine, BLE, content, and soak evidence.

## Rendering constraints

Epic's macOS requirements say:

- Lumen GI/reflections with software ray tracing supports Apple silicon M1+.
- Hardware ray tracing and MegaLights are not currently supported on Mac.
- Nanite and Virtual Shadow Maps have beta support on Apple silicon M2+.
- TSR supports Apple silicon M1+ with runtime cost.

Epic's broader [desktop rendering-path feature table](https://dev.epicgames.com/documentation/en-us/unreal-engine/supported-features-by-rendering-path-for-desktop-with-unreal-engine) shows that exact feature availability depends on desktop deferred path and shader model. The macOS-specific requirements take precedence for platform promises.

Research implication: establish conventional LOD/HLOD and supported shadowing as the guaranteed path; allow Lumen software/Nanite only as measured scalability features with fallbacks. The M5 Max label does not waive 60-minute thermal/frame testing or the need to test actual display resolution.

## Build and packaging

Epic's [BuildGraph documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-use-buildgraph-for-unreal-engine) describes it as extensible build automation runnable through `RunUAT.sh` on Mac and distributable across a build farm. Use it for build/cook/stage/package orchestration while CI controls signing, provenance, publication, and environment promotion.

Epic's [packaging documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project) confirms macOS as a desktop target. A Shipping configuration, not Development, is the release artifact.

Apple's [direct distribution overview](https://developer.apple.com/documentation/technologyoverviews/distribution) explains that macOS apps may be distributed outside the store and that Gatekeeper validates known developer identity/integrity. Apple's [notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) cover Developer ID, Hardened Runtime, secure timestamp, valid nested signatures, and distribution entitlements.

Research implication: test the exact final DMG and updater bytes. Sign nested code correctly, notarize/staple, inspect entitlements, and run Gatekeeper on a clean standard user. Build reproducibility is a toolchain fingerprint/provenance requirement even if Unreal cook output is not byte-for-byte deterministic initially.

## Licensing finding

Epic's current [release/licensing page](https://www.unrealengine.com/release) says the standard EULA for qualifying royalty products uses 5% of worldwide gross revenue over the first USD 1 million, and describes a conditional 3.5% “Launch Everywhere with Epic” rate. Terms, exclusions, reporting, store revenue, advances, and eligibility can change and depend on the business.

Research implication: track Unreal as a commercial dependency in the financial model. Before monetization/release, submit the appropriate release information and have counsel/finance review the then-current EULA; this document is not a license interpretation.

## Open questions for Phase 0

- Does the exact selected UE 5.8 patch compile/package cleanly under Xcode 26.1.1 on a fully patched Tahoe 26.6.2 machine?
- Do all bundled native dependencies contain arm64 slices and pass library validation/notarization?
- Which Metal shader model/renderer settings are stable on the physical M5 Max for the representative route?
- Does Nanite/Lumen software improve quality enough to justify its thermal/frame/cook risk compared with conventional fallbacks?
- Does the Unreal app bundle integrate Bluetooth usage text and Sparkle without entitlement/signature surprises?
- What is the exact M5 Max GPU/display configuration used for acceptance, and is 2560×1600 the intended render-output target or an internal resolution?

## Source list

- Apple, [macOS Tahoe 26 release notes](https://developer.apple.com/documentation/macos-release-notes/macos-26-release-notes)
- Apple, [Xcode SDK and system requirements](https://developer.apple.com/xcode/system-requirements)
- Apple, [Distribution](https://developer.apple.com/documentation/technologyoverviews/distribution)
- Apple, [Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- Apple, [Hardened Runtime](https://developer.apple.com/documentation/security/hardened-runtime)
- Epic Games, [macOS Development Requirements for Unreal Engine 5.8](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine)
- Epic Games, [Supported Features by Rendering Path for Desktop](https://dev.epicgames.com/documentation/en-us/unreal-engine/supported-features-by-rendering-path-for-desktop-with-unreal-engine)
- Epic Games, [BuildGraph](https://dev.epicgames.com/documentation/en-us/unreal-engine/buildgraph-for-unreal-engine)
- Epic Games, [Packaging Your Project](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project)
- Epic Games, [Releasing products / Unreal licensing](https://www.unrealengine.com/release)
