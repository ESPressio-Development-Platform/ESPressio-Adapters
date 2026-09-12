# ESPressio-Adapters — Tranche 6 Implementation Report

## Status

**COMPLETE — A6-01 through A6-24 and the Section 27.27 completion gate are implemented and validated on `primitives_redesign`.**

Validated implementation head before this report: `d946526de08c9fc17582a39a949f5e30d782db4b`.

Validation workflow: GitHub Actions run `34708407225` (`Adapters architecture contracts`).

- `host-contracts`: **SUCCESS** — complete 18-check host/negative/resource/example matrix.
- `esp32-surface`: **SUCCESS** — full A1/Q1/A2 public surface compiled for ESP32 with `-fno-rtti`.
- Automation classification: `AUTOMATION_AVAILABLE_SUCCESS`.

This report is documentation-only. The implementation head above is the exact source state on which the completion gate was proven.

## Repository and dependency boundary

`ESPressio-Adapters` exists on the authorized `primitives_redesign` branch.

Direct ESPressio dependencies are exactly:

- `ESPressio-System#primitives_redesign`
- `ESPressio-Primitive#primitives_redesign`
- `ESPressio-Task#primitives_redesign`

No direct Event, Command, State, Mesh, Radio, Threads, Timing, Serializable, Persistence, Observable, or Security dependency exists. Host configuration scans the generic source surface for forbidden dependency tokens.

Package version remains `0.1.0`. No version, tag, release, or `main` integration was performed during this structural tranche.

## A6 implementation ledger

| Change set | Result | Implemented evidence |
| --- | --- | --- |
| A6-01 | Complete | Repository/branch scaffold and exact System + Primitive + Task dependency boundary. |
| A6-02 | Complete | Family-neutral direction, six service classes, domains, statuses, submission/evidence/deadline vocabulary. |
| A6-03 | Complete | Move-only generation-safe byte and complete-record lease identities; stale release/completion cannot affect replacement generations. |
| A6-04 | Complete | `StaticByteArena<ByteClass<...>>`, smallest-currently-free fitting contiguous slot, no chaining/live growth/heap fallback. |
| A6-05 | Complete | Mutable write view followed by one-way `Commit(actualLength)` seal; borrowed ingress copied before callback return. |
| A6-06 | Complete | Fixed private capacity domains; one transaction claims record + fitting bytes from the same domain. |
| A6-07 | Complete | Independent inbound/outbound planes, six non-lendable private domains, opportunistic SharedOverflow, inbound-only UntrustedIngress. |
| A6-08 | Complete | Additive protected requirements and deterministic largest-demand-first/smallest-fitting-slot proof. |
| A6-09 | Complete | Non-wrapping capacity generation plus fixed infrastructure wake; wake is observation, never reservation. |
| A6-10 | Complete | Bounded `AdapterWorkRecord`, runtime-owned fixed queue/index structures and generation-safe lifecycle state. |
| A6-11 | Complete | One fixed frozen `AdapterBindingDescriptor` per Primitive family; raw noexcept thunks/non-owning context, no RTTI/dynamic callable registry. |
| A6-12 | Complete | Transactional Bind/Initialize/freeze validation including representation, service, evidence, provenance and capacity requirements. |
| A6-13 | Complete | Shared inbound/outbound T1 `IdleWorkerTask` pools; workers own no private work queue. |
| A6-14 | Complete | Rotating service-class and family fairness in runtime-owned queue banks. |
| A6-15 | Complete | Trusted physical ingress performs complete byte ownership only; family admission executes later on shared inbound worker. |
| A6-16 | Complete | Bounded UntrustedIngress classifier/promotion path with finite monotonic quarantine deadline and transactional protected promotion. |
| A6-17 | Complete | Synchronous family encode directly into Adapter-owned `ByteLease`; source object pointer not retained after commit. |
| A6-18 | Complete | Normalized finite P2 monotonic campaign state, attempt bound, retry spacing, earliest service deadline and explicit bounded `ServiceDue()`. |
| A6-19 | Complete | Generic lower-transport submit/validate/cancel/quiesce seam plus generation-correlated deferred completion and lifecycle wake. |
| A6-20 | Complete | M1 result consumed exactly; DestinationPrimitiveAdmission only from Accepted/AlreadyAccepted; immediate peer and validated original source remain distinct; stale deferred completion rejected. |
| A6-21 | Complete | Controlled shutdown stops refill, quiesces transport, drains queued/retained volatile ownership, waits only for executing T1 quanta and invokes no arbitrary application drain. |
| A6-22 | Complete | Host behavioral/negative/stress/no-heap/no-RTTI/hot-path-noexcept matrix plus ESP32 surface compile. |
| A6-23 | Complete | Manifest/CMake/workflow dependency guards, target-specific resource API, measured canonical host resource report. |
| A6-24 | Complete | Full README, dependency-neutral mock integration example compiled in CI, public and non-trivial ownership/state-machine documentation, tranche-wide validation. |

## A1 completion evidence

The implementation proves:

- fixed compile-time size-class arenas only;
- smallest-currently-free fitting contiguous allocation;
- one payload owns one slot, with no chaining;
- move-only generation-safe `ByteLease`;
- independent reclamation and stale-generation rejection;
- immutable actual length established at commit;
- no heap fallback/live resizing in retained paths.

Host tests cover exact boundaries, exhaustion, independent release, stale/double release, commit semantics, generation exhaustion, randomized allocation/reclamation stress, and compile-failure of lease copying.

## Q1 completion evidence

The implementation provides independent inbound and outbound capacity planes. Each enabled direction has six private non-lendable service-class domains and SharedOverflow; inbound additionally has physically distinct UntrustedIngress quarantine.

Trusted acquisition is private-first then SharedOverflow only. Another class's private capacity is never borrowed. Complete bundle admission owns record and byte slot from one domain and rolls partial acquisition back immediately.

Protected requirement validation is additive and proves both record count and simultaneous byte-slot fit. Capacity release advances a monotonic generation before/with a fixed infrastructure wake; no capacity polling task exists.

Quarantine cannot self-promote from an unauthenticated claimed class. It is bounded by a local monotonic deadline, and trusted promotion is another complete nonblocking transaction. Promotion never retains both quarantine and protected ownership indefinitely.

## A2 completion evidence

The runtime has one frozen binding per `PrimitiveFamilyId`, with fixed raw noexcept thunks and non-owning context. `Initialize()` validates the complete composition transactionally before publishing initialized/frozen state.

Inbound and outbound use shared fixed T1 worker pools. Work queues belong to the runtime, not workers. Queue selection rotates across service class and family binding.

Trusted physical ingress copies complete bytes into Adapter ownership before returning and performs no family execution merely to accept the transport callback. Outbound serialization writes directly into Adapter-owned mutable bytes synchronously and retains no source family object pointer after commit.

Primitive admission semantics are consumed from `ESPressio-Primitive`; Adapters does not redefine them. Only `Accepted` and `AlreadyAccepted` establish `DestinationPrimitiveAdmission` evidence. Immediate transport peer remains distinct from validated original semantic source identity.

P2 pursuit uses local monotonic time and finite attempt/residence/retry-spacing state. The runtime creates no polling timer task. Retained pursuit emits the fixed infrastructure wake and exposes `EarliestServiceDeadline()` plus one bounded `ServiceDue()` quantum. `Busy` remains a valid nonblocking contention result and the owner retries from its existing wake/execution substrate.

Deferred transport completion is correlated by both generation-safe Adapter record identity and transport generation. A stale completion cannot resolve a reused record.

## Validation matrix

The final host CTest matrix contains 18 checks:

1. `adapter_byte_arena`
2. `adapter_capacity`
3. `adapter_capacity_rollback`
4. `adapter_capacity_fit`
5. `adapter_no_heap`
6. `adapter_binding`
7. `adapter_queue`
8. `adapter_pursuit`
9. `adapter_runtime`
10. `adapter_deferred_retry_wake`
11. `adapter_contract_matrix`
12. `adapter_stress`
13. `adapter_resources`
14. `adapter_shutdown`
15. `adapter_runtime_no_heap`
16. `adapter_hotpath_no_exceptions`
17. `adapter_compile_fail_byte_lease_copy`
18. `adapter_example_mock_runtime_compile`

All 18 passed on run `34708407225` at `d946526de08c9fc17582a39a949f5e30d782db4b`.

The ESP32 job in the same run compiled the complete A1/Q1/A2 surface with `-fno-rtti` successfully.

### Exception-build classification

The canonical Adapter hot-path boundary is validated with compile-time `noexcept` assertions over allocation, complete-bundle reservation, queue admission, binding and pursuit operations. A global dependency-closure `-fno-exceptions` build is not used as the proof because the System provider layer deliberately contains internal exception-containment implementation. Adapters itself does not introduce an exception-based hot path.

### Heap validation

The runtime no-heap fixture denies global heap allocation after successful `Initialize()`/`Start()` and proves trusted inbound, outbound submission, worker execution and release continue entirely through preallocated/fixed storage.

## Resource accounting

`src/ESPressio_AdapterResources.hpp` publishes target-specific compile-time accounting for concrete deployments. `RESOURCE_ACCOUNTING.md` records the canonical host profile measured in GitHub Actions run `34707561642`.

Key host values for that validation profile:

- whole `AdapterRuntime` object: **274,096 bytes**;
- `AdapterWorkRecord`: **328 bytes**;
- queue bank per direction: **13,376 bytes**;
- inbound/outbound worker pool object: **504 bytes each**;
- SharedOverflow semantic reserve across both directions: **30,720 bytes**;
- inbound UntrustedIngress semantic reserve: **15,360 bytes**;
- four example 4096-byte provider-owned worker stacks: **16,384 bytes**, reported separately from the runtime object.

The target-specific API recomputes `sizeof` facts for the compiler/ABI/target and is also compiled by the ESP32 validation job.

## Documentation and integration surfaces

`README.md` now documents the implemented generic substrate rather than the pre-A2 bootstrap state, including A1 ownership, Q1 private/shared/quarantine capacity, generation/wake, frozen bindings, trusted/untrusted ingress, provenance/evidence, shared workers/fairness, outbound encode handoff, finite pursuit, shutdown and resource accounting.

`examples/MockAdapterRuntime/mock_adapter_runtime.cpp` demonstrates a tiny family/transport binding without importing Event, Command, State, Mesh, Radio or another transport library. It is syntax-compiled in the host CI matrix.

Public APIs and non-trivial ownership/state-machine methods have standard documentation comments, including byte/capacity ownership, queue/retained lifecycle, worker-pool behavior and the AdapterRuntime public/private state machine.

## Completion and next tranche

There are no known open Tranche-6 implementation items after the validated `d946526de08c9fc17582a39a949f5e30d782db4b` source checkpoint.

The next dependency-ordered work package is **Tranche 7 — Radio core and physical providers**. Before the first Radio/provider source write, re-query the current `primitives_redesign` tips for `ESPressio-Radio`, `ESPressio-ESP32`, `ESPressio-NRF24` and any other provider repository required by the current source audit, reconcile all drift, then implement the locked Section-28 R1/R2/R3/Q1/K1/K2 work package without compatibility defaults that allow an old provider to appear compliant.
