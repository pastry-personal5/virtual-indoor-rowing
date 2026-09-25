# Han River course-content plan

Status: In progress — implementation gates passed; canary evidence pending  
Owner: Client/content  
Last reviewed: 2026-09-20

## Purpose and decision

This plan turns the already-scoped `route.han-river.5k` into a restrained,
realistic **vertical slice**. It is an implementation plan for course content
only; it does not authorize implementation, alter the Phase 2 Milestone 1
gate, or introduce a new delivery milestone.

The course remains a 5 km, open, westbound virtual waterline from Banpo toward
Wonhyo Bridge, at blue hour after rain. It is a presentation route, not a
navigational chart or a representation that physical rowing is permitted or
safe on the Han River. Water, wind, wakes, and scenery never alter PM-backed
distance, pace, power, workout completion, or the local journal.

The creative direction is **restrained realistic immersion**:

- convey the scale, light, bridges, banks, and urban calm of the river without
  landmark replicas, tourist spectacle, exaggerated weather, or a game-like
  obstacle course;
- prioritize a stable forward row line, readable exertion HUD, and visual
  rhythm across repeated workouts; and
- use original meshes, materials, audio, and authored set dressing. Reference
  images and geographic data remain reference inputs unless separately
  approved for a cooked asset with complete provenance.

Milestone 1 covers a functional 5 km route with representative authored
content at its six named beats and deliberately economical transition space.
It does not deliver a production-quality full Han River environment: repeated
kit use, low-detail distant scenery, and selectively detailed near-field
silhouettes are intentional. A later, separately scoped milestone would be
required to raise the whole route to production-environment quality.

## Fixed course contract

| Item | Decision |
|---|---|
| Stable ID / version | `route.han-river.5k` / `1.0.0` until a compatible, versioned replacement is released |
| Route type / length | Open point-to-point / 5,000 m presentation length |
| Direction | Westbound from Banpo toward Wonhyo |
| Selected time and weather | Blue hour; clear after rain; light wind ripple |
| Completion presentation | At 5 km, retain the boat at the endpoint and show `Route complete — continue rowing`; official PM-backed activity continues unchanged |
| Fallback | Cooked-in Standard 2 km route is always a peer selectable course and remains usable when Han content is unavailable |
| Content boundary | Data-only, signed external UE 5.8 IoStore content; never Blueprints, code, scripts, plug-ins, or raw-package-path selection |

## Route composition

Build the vertical slice from a small number of reusable visual kits placed at
the named beats along the authored virtual waterline. Connect them with
economical water, bank, skyline, and haze treatment rather than attempting
continuous production-grade city detail. Use the given WGS84 anchors solely to
maintain geographic plausibility; do not import chart geometry or claim
real-world navigation accuracy. Keep the central rowing channel visually
clear, with a generous safety offset from every bank, bridge pier, buoy, and
background boat.

| Distance | Beat | Content intent | Required restraint |
|---:|---|---|---|
| 0 m | Banpo start | Broad bridge-span reveal, calm water, lit piers, park-bank silhouettes, distant housing lights | No fountain show, crowds, branded signage, or dense foreground traffic |
| 650 m | Some Sevit look-back | Three understated contemporary island volumes, warm reflected light, low promenade, sparse buoys | Suggest form and glow; do not construct an exact architectural replica |
| 1,450 m | Dongjak span | Repeating paired bridge silhouette, practical lights, reeds, and riverside paths | Preserve open sky and channel; bridge must read as landscape rhythm rather than a tunnel |
| 3,000 m | Nodeulseom | Low island tree canopy, subdued cultural-venue glow, layered far-bank lights | Keep foliage off the route and avoid bright isolated hotspots behind the HUD |
| 3,650 m | Hangang Bridge | Long-deck approach as the route’s quiet visual crescendo, controlled water highlights and broad skyline | No intensified wake, current, or visual “challenge” treatment |
| 5,000 m | Wonhyo finish | Wide downstream vista, receding bridge lights, quiet skyline, endpoint hold | No podium, forced stop, or implication that the workout is complete |

## Art, lighting, water, and audio plan

### Environment kits

Create and review the following reusable, original kits before dressing the
vertical slice:

1. A bridge kit: modular deck, pier, truss/cable silhouette variants, warm
   practical-light instances, and a low-detail distant variant.
2. A bank kit: stepped concrete edge, promenade, path lights, reeds, trees,
   retaining walls, and low residential/commercial skyline cards or meshes.
3. A river kit: a broad, non-interactive water surface; subtle normal/wind
   variation; low, broken reflections; small non-collision navigation buoys;
   and distant, sparse presentation boats only if they do not compromise
   readability or the open rowing line.
4. A city-atmosphere kit: blue-hour sky, distant haze, restrained emissive
   windows, and a wet-after-rain material response. Avoid thunder, heavy rain,
   lens effects, bloom spikes, or high-contrast reflection bands.

The later [Milestone 5 water refinement](12-realistic-water-refinement-plan.md)
adds restrained boat-local hull and oar effects to this presentation kit. They
do not introduce river current or the intensified wake treatment excluded at
the Hangang Bridge beat.

Use camera-facing or low-detail far scenery where it is visually equivalent.
Near-field content needs real parallax and silhouette quality around the boat;
everything else must earn its cost in repeated rowing view. The default camera
remains forward/elevated and must not need user steering, collision, gameplay
physics, or camera interaction to make the course legible.

### Lighting and legibility

- Establish one calibrated blue-hour lighting preset before beat dressing:
  cool ambient sky, warm practical lights, constrained exposure range, and
  subdued specular water.
- Test every beat with the actual HUD at normal and large text/high-contrast
  settings. No bridge lamp, water reflection, or sky highlight may obscure
  pace, connection state, quality labels, or route-complete copy.
- Include accessibility alternatives for visual-only route cues: course
  progression stays available through distance/checkpoint text and optional
  non-urgent cue equivalents; color is never the only meaningful signal.

### Audio

Milestone 1 includes an optional, low-fatigue environmental bed: light water,
distant traffic, subtle city ambience, and occasional non-localized river
sounds. It must duck under workout cues, respect the existing global audio
controls, and contain no timed rhythm, competitive prompt, or cue that could
be mistaken for an official workout instruction. Exact recordings, libraries,
and licenses need provenance approval before import.

## In-app course selection

Course selection is visible in the Unreal app before a workout begins; it is
not an editor-only choice or a raw-package-path control. Present Standard and
Han River as peer course tiles:

- **Standard** is enabled by default and remains selectable offline, during
  catalog failure, and whenever downloaded Han content is unavailable.
- **Han River** is visible on the separate Content page. Before a valid
  download has activated on the next launch, its tile shows its unavailable
  state and a concise, user-safe reason. Once compatible active content is
  verified, the tile enables selection by stable route ID.
- The selected course name, open/closed behavior, length, and short bilingual
  description are shown before the athlete starts Just Row. Selection changes
  presentation only; it never changes official workout facts.
- A user may start a Han download only from the Content page while idle. The
  app does not begin, activate, withdraw, mount, or change selected content
  during an active workout.

## Production sequence and ownership

The owner completed the representative-scene view review for the 500 m
Banpo-to-Sebit section and recorded **SUCCESS** on 2026-09-24 against commit
`b38ff54`. This accepts the viewed scene; it does not establish a measured
performance budget, packaged Shipping evidence, or the full 5 km route review.

| Order | Owner | Deliverable / review gate |
|---:|---|---|
| 1 | Content lead + product | Lock this direction, the six beats, bilingual display copy, and the no-navigation/safety wording; record reviewer in provenance. |
| 2 | Technical artist | Establish a representative 500 m Banpo-to-Sebit scene with the bridge, bank, water, lighting, boat, and HUD together; approve look, legibility, and performance budget before full-route production. |
| 3 | Content team | Build the four reusable environment kits with LOD/HLOD, texture, material, collision, and emissive budgets documented in the content validator. |
| 4 | Level artist + technical artist | Dress the six beats and economical transition space along the authored waterline; use route ID/checkpoint metadata, not package paths, for runtime selection. |
| 5 | Client + content | Cook Han as external data-only IoStore content, generate inventory/hash/provenance outputs, sign the manifest, and stage it through the Milestone 1 delivery path. |
| 6 | QA + release/platform | Run editor validation, packaged Shipping canary, content failure/rollback cases, and a human route review on the reference Mac. Promote only after all Milestone 1 evidence passes. |

Use Unreal-MCP for bounded, reviewed asset inspection and editor work; keep
source-controlled route data, provenance, validator rules, and release
artifacts in the repository pipeline.

## Asset naming, provenance, and validation

Place authored source assets beneath `Content/Phase2/HanRiver/` using
purpose-first folders such as `Environment/`, `Materials/`, `Meshes/`,
`Audio/`, and `Maps/`. Follow the project’s eventual content-validator naming
and inventory schema rather than treating this suggested folder layout as a
runtime contract. Runtime selection continues through `RouteDefinition` stable
IDs.

For each source asset or library import, the provenance inventory must record
asset ID, kind, author/source, license, source identifier or URL, source hash
where available, reviewer, and required attribution. Cooked assets may be
original, public-domain, or CC BY-SA of any version after a per-asset
compatibility review; other third-party licenses require the existing
legal/provenance review. A CC BY-SA record additionally requires the exact
license/version and license URL, author/creator, canonical source URL, source
SHA-256, modification description, and compatible share-alike release
designation.

Each package includes exactly one `licenses/NOTICE.txt` in the signed
inventory. It lists every CC BY-SA asset and its creator, source, license,
URL, changes, and the applicable license text or stable version-specific
reference. The CC BY-SA image and any crop, retouch, texture, mesh, or other
adaptation remain available under the same or a compatible CC BY-SA license;
the rest of the Han package, including unrelated VIR code and original
assets, is not automatically relicensed. The existing CC0 photos remain
unshipped visual references; OpenAI-generated key art remains review art until
approved as a cooked asset.

The editor validator must reject at least: unknown or undeclared assets,
redirectors, unapproved classes, excessive texture/material/mesh cost,
missing LODs where required, missing provenance, and content outside the
declared route inventory. The delivery validator retains the stricter
Milestone 1 trust boundary: signature, schema, compatibility, hash, size,
path, inventory, and data-only IoStore validation before mount.

## Acceptance evidence

This plan adds no new phase gate. It supplies content acceptance criteria to
the existing Milestone 1 evidence:

- a route review proves each named beat is recognizable through original,
  restrained presentation and the rowing line stays visually open;
- reference-Mac captures at start, each beat, and endpoint prove HUD
  readability, bounded exposure/reflections, camera framing, and the open
  endpoint hold;
- Unreal automation proves route-ID selection and that course presentation has
  no write path to official distance, pace, workout state, or journal data;
- the editor/content validator and provenance inventory pass before cook;
- packaged Shipping evidence proves fresh offline Standard use, Han
  download/restart/play, corruption/expiry/withdrawal/mount-failure fallback,
  rollback, and active-workout content-operation blocking; and
- the Phase 2 required commands pass: `make build && make test`,
  `make format-check`, `make unreal-native-app`, `make unreal-smoke`,
  `make unreal-shipping`, and `make unreal-package-verify`.

QA-001 performance certification remains the deferred Phase 4 visual-
performance gate under ADR-0010. This course work must still stay within the
documented budget and surface material performance regressions immediately;
it may not claim Phase 2 certification by substituting editor observations for
the required packaged evidence.

## Explicit exclusions

This plan excludes real-world route guidance, physical resistance simulation,
currents/waves that affect workout facts, racing rules, remote boats, user
steering, interactive hazards, crowds, user-generated content, executable
mods, automatic downloads, additional polished routes, and executable update
delivery. Changes to these boundaries require separately scoped product and
architecture work.
