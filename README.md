# Virtual Indoor Rowing

Greenfield architecture baseline for a macOS virtual indoor-rowing product powered by Unreal Engine and a Concept2 Model D fitted with a PM5 Performance Monitor.

The repository currently contains design documentation rather than an implementation. Start with [the documentation index](docs/README.md) and [executive product and architecture review](docs/architecture/00-executive-review.md), then read the [product scope](docs/architecture/01-product-scope.md) and [system architecture](docs/architecture/02-system-architecture.md).

## Target baseline

- Reference computer: MacBook Pro with M5 Max and 128 GB unified memory.
- Operating system: macOS Tahoe **26.6.2 or later**.
- Game engine: Unreal Engine 5.8, pinned to a tested patch release.
- Rower: Concept2 Model D with a PM5 monitor, current production firmware, connected over Bluetooth Low Energy.
- Distribution: Apple-silicon (`arm64`) Developer ID application, hardened, notarized, and initially distributed outside the Mac App Store.

## Architecture status

This is an initial reference architecture. Decisions marked **Accepted** establish the implementation default. Assumptions and external dependencies must still pass the risk-reduction spikes in [the delivery plan](docs/architecture/10-delivery-plan.md) before production commitments are made.
