# Han River third-party bitmap and geographic-reference notices

Provenance schema version: 2. Every promoted package asset is recorded with
license name/version and URL, creator, canonical source URL, source SHA-256,
modification description, and share-alike release designation. The signed
package also carries exactly one `licenses/NOTICE.txt`; CC BY-SA records must
be repeated there with the applicable license text or a stable version-specific
reference.

This directory contains original VIR route data and generated concept art, plus
the two specifically listed third-party reference bitmaps below. The third-party
photos are **art-reference inputs only**: they are not shipped as a route
texture, are not shown in the product, and may not be promoted to a cooked
asset without a fresh content/legal review.

## Creative Commons photos

| Local file | Depicted subject | Creator | License | Source page | Original SHA-1 | Local SHA-256 | Retrieval |
|---|---|---|---|---|---|---|---|
| `ReferencePhotos/banpo-bridge-moonlight-rainbow-fountain-wvdp-2023.jpg` | Banpo Bridge Moonlight Rainbow Fountain, Seoul | Wvdp | [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/deed.en) | [Wikimedia Commons file page](https://commons.wikimedia.org/wiki/File:Banpo_Bridge_Moonlight_Rainbow_Fountain_at_night_-_2023-08-14.jpg) | `b3f8a8004b3d599e5c8abee1a7c15db232f9ebc9` | `76863c14e05e15367da7dbcbaab959933d33d08f041763bcccaf759392bb24f7` | 2026-09-20 |
| `ReferencePhotos/nodeul-station-view-aspere.jpg` | View from Nodeul Station, Seoul | Aspere | [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/deed.en) | [Wikimedia Commons file page](https://commons.wikimedia.org/wiki/File:Nodeul_Station_View.jpg) | `a0ab97a5977a6f2cb9e62b26f06c827cd8f62dd6` | `e525ea9fb3e96b86c06d8faf75120e6a9e257442c53b7df2058d6c71d6d7fe08` | 2026-09-20 |
| `ReferencePhotos/han-river-daytime-skyline-pickpik.jpg` | Han River daytime skyline with 63 Building | Not stated on source page | Public Domain (as labelled by the source page) | [PickPik image page](https://www.pickpik.com/scenery-han-river-seoul-sky-river-cloud-122032) | Not provided | `42f16f4e30762e26d561f491b6ffaef34392990c4d19ed842cc14ec92ab87388` | 2026-09-20 |
| `ReferencePhotos/yeouido-skyscraper-pickpik.jpg` | Yeouido high-rise beside the Han River | Not stated on source page | Public Domain (as labelled by the source page) | [PickPik image page](https://www.pickpik.com/seoul-skyscraper-building-architecture-yeoido-sky-39939) | Not provided | `6c56e9d96a2b5ac8e587a2a01b7ed662ca2767369ef83908d9fae353e1726765` | 2026-09-20 |

Both source pages report the uploader's work and CC0 dedication. CC0 does not
require attribution, but this record preserves provenance and makes the scope
of reuse auditable.

## PickPik reference photo

The two PickPik bitmaps are third-party reference images supplied from the
source pages above. Each page labels its image "Public Domain" and offers an
original download (5964×2912 for the daytime skyline; 3839×5870 for the
Yeouido high-rise); the checked-in bitmaps are their 728×355 and 728×1113
previews. Preserve the source-page records and hashes in any review. Like the
Creative Commons photos, they are **not shipped**, may not be baked into route
textures, and need a fresh content/legal review before any promoted or cooked
use.

## Geographic landmark records

`han-river-5k.geojson` is an original, coordinate-registered *virtual*
waterline. Its named landmark coordinates were checked on 2026-09-20 against
Wikidata's public structured-data records:

- [Banpo Bridge (Q255506)](https://www.wikidata.org/wiki/Q255506)
- [Some Sevit / Sebitseom (Q7451660)](https://www.wikidata.org/wiki/Q7451660)
- [Dongjak Bridge (Q624089)](https://www.wikidata.org/wiki/Q624089)
- [Nodeulseom (Q626140)](https://www.wikidata.org/wiki/Q626140)
- [Hangang Bridge (Q623843)](https://www.wikidata.org/wiki/Q623843)
- [Wonhyo Bridge (Q484660)](https://www.wikidata.org/wiki/Q484660)

Wikidata structured data is available under [CC0](https://creativecommons.org/publicdomain/zero/1.0/).
The virtual centerline between anchors is VIR-authored; it is neither a copy of
a navigation chart nor a statement that physical rowing is allowed or safe.

## VIR-generated concept art

`ConceptArt/han-river-blue-hour-key-art-v1.png` was generated on 2026-09-20
with OpenAI image generation for this project. It is original visual direction,
not a photograph, not an exact building replica, and carries no third-party
Creative Commons obligation. It is a review/key-art bitmap only until an Unreal
asset-import and performance review approves a cooked use.
