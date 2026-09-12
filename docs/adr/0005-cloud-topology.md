# ADR-0005: AWS single-region, multi-AZ launch topology

- Status: Accepted
- Date: 2026-09-12
- Owners: CTO, online/platform lead

## Context

A startup needs production reliability without operating a premature service mesh, Kubernetes platform, or global database. Launch hardware/users are initially centered on macOS, with the supplied timezone in Korea. Solo must remain independent from the region, while races need measured latency.

## Decision

- Launch in AWS `ap-northeast-2` across at least two availability zones and separate production/non-production accounts.
- Use ECS Fargate for control API, realtime gateway, race worker, and async worker initially.
- Use RDS PostgreSQL Multi-AZ for durable transactional state, ElastiCache Redis Multi-AZ for reconstructible ephemeral state, SQS/DLQ for asynchronous work, S3/CloudFront for objects/content/releases, and WAF/ALB for public APIs/WSS.
- Keep the control plane a modular monolith plus worker. Separate realtime deployables for socket and tick isolation.
- Provision through Terraform and instrument with OpenTelemetry.
- Preserve a provider boundary at domain adapters but do not pursue theoretical cloud portability at the expense of managed-service value.
- Add race regions on measured latency/demand; do not claim multi-region account writes before a separate data-consistency ADR.

## Consequences

- Small operational surface, durable managed services, and clear initial failure domain.
- Users far from Seoul may be warned/unranked until another race region exists.
- Region outage removes online features under the initial RTO while local solo stays usable.
- AWS service semantics appear in infrastructure/adapters and team skills.
- Fargate scheduling jitter/cost is measured; race workers can move to dedicated ECS capacity at a defined trigger.

## Alternatives considered

- **Kubernetes/EKS:** rejected initially; no workload requires its operational cost.
- **Microservices per domain:** rejected; team size and transactional workflows favor a modular monolith.
- **Serverless per request/message:** useful for some jobs but rejected as the primary WSS/tick execution model.
- **Multi-region active-active immediately:** rejected due identity/data/result consistency and operational complexity without demand evidence.
- **Single VM:** rejected for fault isolation, release, scaling, and managed durability.

## Validation and revisit

Staging must exercise AZ/task/worker/Redis/database failures, restore, and twice-forecast load. Revisit region, compute, and module extraction at the triggers in the delivery plan.
