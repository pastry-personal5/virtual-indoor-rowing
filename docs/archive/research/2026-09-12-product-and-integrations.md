# Research: product benchmark, integrations, and macOS distribution

Research date: 2026-09-12  
Purpose: Translate “like EXR” into capability categories and verify integration/distribution constraints.  
Authority: Point-in-time evidence only; this is not a commitment to copy a competitor or implement every feature.

## EXR benchmark findings

EXR's [product overview](https://exrgame.com/support/about-exr) positions it as an exercise game combining 3D worlds, training modes/workouts, real-time feedback, third-party synchronization, and online rowing. Its [gameplay help catalog](https://exrgame.com/support-category/gameplay-features) currently includes:

- Just Row/routes/segments and leaderboards;
- structured workouts/plans, training editor, performance tests/zones;
- multiplayer/group events and competition/custom races;
- ghosts, real rowers, bots, pace partners, minigames;
- goals, challenges, badges, levels/XP/streaks/customization;
- training reports and social following;
- FIT export and third-party connections.

EXR's [competition documentation](https://exrgame.com/support/competition) describes synchronized online races, private codes, competitors/ghosts, false-start consequences, results, and spectator intent. The current product details are not an architecture authority; they are a market capability benchmark.

Research implication: VIR's foundational domains should accommodate routes, workouts, ghosts, presence, races, results, progression, and integrations, but launch should prioritize device/session credibility, solo training, one excellent world, and bounded racing. Gamification breadth and UGC/chat are deferred because they add content/moderation load rather than proving the core product.

## Concept2 Online Logbook

The official [Concept2 Logbook API documentation](https://log.concept2.com/developers/documentation/) says:

- developers must register an application/API key;
- all requests use HTTPS and the current API version can be requested explicitly;
- Authorization Code and Refresh OAuth grants are available to all registered applications;
- explicit `user:read`, `user:write`, `results:read`, and `results:write` scopes exist;
- access tokens expire and refresh tokens rotate/extend use;
- write development must use `log-dev.concept2.com`, followed by Concept2 approval before live API writes;
- results support simple and detailed workout/split forms and documented HTTP errors including duplicate `409`.

The docs also describe password and client-credentials grants for certain trusted applications. They are unnecessary and inappropriate for the standard user-link flow.

Research implication: perform OAuth in the system browser; exchange/store client secret and refresh token server-side; request explicit minimum scope; write asynchronously with an idempotent job; treat duplicates as reconciliation; never ask for a Concept2 password. Production export remains disabled until Concept2 approval.

## FIT export

Garmin's official [FIT file types](https://developer.garmin.com/fit/file-types/) describe Activity files as records of sensor/events, Workout files as structured instructions, and Course files as course reconstruction. FIT uses a header, message definitions/messages, and CRC, while its SDK handles serialization and endianness.

Research implication: generate a standard Activity export through a pinned/reviewed FIT SDK, preserve timestamp order, populate rowing-relevant messages conservatively, and test files with independent consumers. FIT is an export projection from VIR's canonical session, not its source-of-truth storage.

## Apple Health on a Mac-only product

Apple's [HealthKit platform guidance](https://developer.apple.com/design/human-interface-guidelines/healthkit) says HealthKit is not supported as a health-store product on macOS. Apple's [HealthKit framework overview](https://developer.apple.com/documentation/healthkit/about-the-healthkit-framework) clarifies that while framework code may be present on macOS, Macs do not have a HealthKit store and `isHealthDataAvailable()` returns false.

Research implication: direct Apple Health write from the macOS launch app is not a viable feature. A later iPhone/watch companion can receive authenticated VIR sessions and write with separate user permission/product architecture. Do not market Apple Health support for Mac through a non-working framework call.

## Native Bluetooth and sandbox

Apple's [Core Bluetooth overview](https://developer.apple.com/documentation/corebluetooth) exposes `CBCentralManager`/`CBPeripheral` for discovering and communicating with BLE peripherals and warns that apps need the applicable Bluetooth usage description or can fail at runtime.

Apple's [App Sandbox documentation](https://developer.apple.com/documentation/security/app-sandbox) says Mac App Store apps require App Sandbox and documents `com.apple.security.device.bluetooth`. Its [sandbox configuration guidance](https://developer.apple.com/documentation/xcode/configuring-the-macos-app-sandbox) also calls out Bluetooth, USB, and outgoing network entitlements as explicit resources.

Research implication: the direct Developer ID build still supplies clear Bluetooth privacy text and minimizes capabilities. If an App Store variant is introduced, it needs a separate sandbox entitlement/package/update/billing verification; do not assume the direct package can be uploaded unchanged.

## Direct macOS distribution

Apple's [Distribution overview](https://developer.apple.com/documentation/technologyoverviews/distribution) supports distributing macOS apps outside the store and recommends notarization so Gatekeeper can verify identity/integrity. Apple's [Developer ID page](https://developer.apple.com/developer-id/) and [notarization documentation](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) establish Developer ID signing, Hardened Runtime, secure timestamp, notarization, and stapled-ticket workflow.

Research implication: Developer ID distribution is a supported first-party path, not an unsigned sideload. The final Unreal app, nested native libraries/updater helpers, and DMG need an automated codesign/notary/Gatekeeper acceptance chain.

## Sparkle update framework

Sparkle's official [documentation](https://sparkle-project.org/documentation/) says Sparkle 2 supports Developer ID-signed app updates and EdDSA archive signing, with optional signed feeds and verification before extraction. It supports DMG/ZIP/tar/Apple Archive/pkg forms and can generate delta updates/appcasts.

Sparkle's [security and reliability history](https://sparkle-project.org/documentation/security-and-reliability/) documents multiple past security fixes, including fixes in 2.9.6, 2.7.3, 2.6.4 and earlier. This is evidence to pin and actively update a reviewed release, not to avoid updates.

Sparkle's [sandbox guidance](https://sparkle-project.org/documentation/sandboxing/) explains that sandboxed applications use bundled XPC services/entitlements and require correct code signing. Direct non-sandboxed integration is simpler but still needs consistent Developer ID signing.

Research implication: use latest approved Sparkle 2, Developer ID/notarization plus a separate EdDSA key, HTTPS immutable objects, signed feed/verification-before-extraction where the pinned version supports it, isolated release keys, and adversarial update/rollback tests. Maintain a manual notarized full-DMG recovery route.

## Unreal multiplayer and licensing

Epic's [Networking Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) describes authoritative client/server play and dedicated-server fairness/efficiency. It supports the architectural principle that competitive truth belongs on a server.

VIR does not adopt Unreal dedicated servers because its official simulation is PM distance/time/rules rather than Actor/physics state. That is a product-specific inference, not a claim that Unreal networking is deficient.

Epic's [release/licensing page](https://www.unrealengine.com/release) currently describes a standard 5% royalty above the first USD 1 million in worldwide product gross revenue, with conditions and possible program rate differences. Commercial launch requires the actual then-current EULA/reporting/legal analysis.

## Unresolved product/business questions

- Launch monetization: subscription, one-time purchase, free beta, trial, family/club/event plans.
- Initial markets/languages and the privacy, subscription, tax, accessibility, rating, child-safety, and consumer-support obligations attached to them.
- Exact Concept2 trademark language, developer relationship, protocol support, and production Logbook approval.
- Whether public leaderboards are opt-in and which profile attributes they show.
- How long dense samples and heart rate create user value versus privacy/storage cost.
- Whether competitive ratings/ability matching are valuable enough for launch complexity.
- Which integration follows Concept2/FIT; avoid building many brittle providers before retention is proven.
- Whether the Mac App Store adds enough acquisition/trust to justify a separately maintained sandbox/StoreKit distribution.

## Source list

- EXR, [What is EXR?](https://exrgame.com/support/about-exr)
- EXR, [Gameplay and features help catalog](https://exrgame.com/support-category/gameplay-features)
- EXR, [Competition mode](https://exrgame.com/support/competition)
- EXR, [Integrations](https://exrgame.com/support/integrations)
- Concept2, [Logbook API](https://log.concept2.com/developers/documentation/)
- Garmin, [FIT file types](https://developer.garmin.com/fit/file-types/)
- Apple, [HealthKit platform guidance](https://developer.apple.com/design/human-interface-guidelines/healthkit)
- Apple, [About the HealthKit framework](https://developer.apple.com/documentation/healthkit/about-the-healthkit-framework)
- Apple, [Core Bluetooth](https://developer.apple.com/documentation/corebluetooth)
- Apple, [App Sandbox](https://developer.apple.com/documentation/security/app-sandbox)
- Apple, [Distribution](https://developer.apple.com/documentation/technologyoverviews/distribution)
- Apple, [Developer ID](https://developer.apple.com/developer-id/)
- Apple, [Notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- Sparkle Project, [Documentation](https://sparkle-project.org/documentation/)
- Sparkle Project, [Security and reliability changes](https://sparkle-project.org/documentation/security-and-reliability/)
- Epic Games, [Networking Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)
- Epic Games, [Releasing products / Unreal licensing](https://www.unrealengine.com/release)
