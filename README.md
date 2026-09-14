# ESPressio Adapters

`ESPressio-Adapters` is the family-neutral ownership, protected-capacity, scheduling, and transport-handoff substrate between typed ESPressio Primitive families and concrete transports. It deliberately does **not** implement Event, Command, State, Mesh, Radio, Security, or serialization semantics.

The `primitives_redesign` branch implements the closed A1/Q1/A2 contracts for Tranche 6 of the Primitive Platform redesign. All runtime storage is fixed by compile-time deployment types and successful `Initialize()` freezes the binding/resource topology before `Start()` publishes Running.

## Dependency boundary

Direct ESPressio dependencies are exactly:

- `ESPressio-System`
- `ESPressio-Primitive`
- `ESPressio-Task`

There is no direct dependency on Event, Command, State, Mesh, Radio, Threads, Timing, Serializable, Persistence, Observable, or Security. A host configuration guard rejects those dependencies/tokens in the generic runtime surface. Concrete family/transport mapping belongs to integration repositories such as MeshAdapters and RadioAdapters.

## A1 — complete owned bytes

Adapter payload ownership uses `StaticByteArena<ByteClass<...>>`:

- every `ByteClass<N, C>` provides `C` independent contiguous `N`-byte slots;
- allocation chooses the smallest currently available class that fits;
- one payload owns one slot—there is no chaining;
- `ByteLease` is move-only and generation-safe;
- an encoder receives a mutable bounded view only before `Commit(actualLength)`;
- after commit, only the immutable `AdapterByteView` is exposed;
- release is independent, so an older/larger retained payload does not FIFO-pin unrelated slots;
- there is no live growth or heap fallback.

```cpp
using Bytes = ESPressio::Adapters::StaticByteArena<
    ESPressio::Adapters::ByteClass<64, 8>,
    ESPressio::Adapters::ByteClass<256, 4>,
    ESPressio::Adapters::ByteClass<1024, 2>>;
```

`StaticByteArena::Shapes()` exposes the exact compile-time size-class multiset for deterministic Q1 fit validation and resource accounting.

## Q1 — protected complete-bundle capacity

A `StaticCapacityDomain<RecordBytes, RecordCount, ByteArena>` owns both record slots and byte slots. Admission is transactional:

1. claim one generation-safe record slot;
2. claim one fitting byte slot from the **same** domain;
3. write and commit the actual payload length;
4. construct the bounded Adapter record in-place;
5. publish a move-only `CapacityRecordLease`.

Any partial failure rolls back immediately. A record and its bytes are never split across domains.

A directional `CapacityPlane` contains six private non-lendable service domains:

`Infrastructure`, `Clock`, `Critical`, `Responsive`, `Convergent`, and `BestEffort`.

Trusted admission is private-first. Only after that class's own private domain is unavailable may it opportunistically use `SharedOverflow`. It never borrows another service class's private reserve. `SharedOverflow` improves throughput but is not part of a protected guarantee.

Inbound and outbound planes are independent. Inbound additionally contains `UntrustedIngress`; outbound has no quarantine domain. An unauthenticated/early sender-written class therefore cannot consume protected `Clock` or `Critical` capacity merely by claiming that class.

Protected requirements are declared before freeze with `ProtectedCapacityRequirement`. Initialization proves both additive record count and simultaneous byte-slot fit using deterministic largest-demand-first placement into the smallest fitting configured class.

## Capacity generation and wake

Every complete-bundle release that may make admission possible advances that direction's non-wrapping `CapacityGeneration` and emits the fixed `CapacityWakeTarget` when configured.

The wake is **not** a reservation. Consumers re-read generation/state and retry their nonblocking acquisition. The generic library creates no capacity-polling task.

The same fixed infrastructure wake is used when retained outbound pursuit creates or changes relevant due-service work. The owner integrates `EarliestServiceDeadline()` into its existing transport execution/timer machinery and calls `ServiceDue()` for a bounded service quantum. Adapters itself does not own a polling timer.

## A2 — frozen family bindings

`AdapterBindingDescriptor` defines exactly one bounded binding per `PrimitiveFamilyId`. It contains only frozen values, raw `noexcept` function pointers, and non-owning owner context—no RTTI registry and no `std::function`.

A binding can provide:

- an inbound family-admission thunk;
- a synchronous outbound encode thunk;
- optional outbound feedback;
- protocol-version range;
- maximum complete inbound/outbound representation sizes;
- supported neutral service classes;
- required destination-admission/provenance capabilities.

`BindFamily()`, `BindTransport()`, `BindClassifier()`, `BindCapacityWake()`, and `AddProtectedRequirement()` are configuration-time operations. `Initialize()` transactionally validates representation fit, service mappings, required evidence/provenance, transport usability, protected-capacity requirements, synchronization, worker initialization, and family uniqueness. Only a successful initialization freezes the family table.

## Trusted inbound ownership

`AdmitTrustedInbound()` is a nonblocking ownership handoff. The physical/Mesh/transport ingress caller supplies a borrowed byte view plus already-normalized semantic provenance. The Adapter runtime:

1. acquires a complete trusted Q1 bundle;
2. copies the complete payload before returning;
3. commits the immutable length;
4. constructs a bounded work record;
5. queues it for a shared inbound T1 worker.

No family decode, Event dispatch, Command execution, State mutation, or application callback occurs merely to accept physical ingress bytes.

The inbound worker invokes exactly one frozen family admission thunk and consumes the returned `PrimitiveAdmissionDisposition` directly. `DestinationPrimitiveAdmission` evidence is established only when Primitive says `Accepted` or `AlreadyAccepted`; link acceptance or byte ownership cannot manufacture that semantic evidence.

## Untrusted ingress and promotion

`AdmitUntrustedInbound()` copies borrowed bytes into the physically distinct `UntrustedIngress` domain and records a finite local monotonic quarantine deadline. A configured `QuarantineClassifierBinding` later performs bounded classification off the physical callback.

If classification establishes trusted family/service/provenance facts, the worker transactionally acquires a complete trusted private/shared bundle, copies the complete retained bytes, commits them, then releases quarantine ownership. The runtime checks quarantine expiry before classification and again before protected promotion. It never holds quarantine and protected capacity indefinitely.

## Provenance boundary

Inbound context preserves two distinct identities:

- `ImmediateTransportPeer` — relay/link/routing evidence;
- `ValidatedOriginalSemanticSource` — a validated `System::DeviceRuntimeIdentity` for the original Primitive source.

The immediate peer can never substitute for the original semantic source. A family that requires original-source evidence is rejected unless that evidence is present and validated.

## Shared T1 workers and fairness

The runtime owns bounded queue banks; workers do **not** own private queues. Each direction has a fixed shared `AdapterWorkerPool` built from T1 `IdleWorkerTask` contexts.

Queue selection rotates across both neutral service class and frozen family binding. One refill/worker quantum accepts at most one selected record before fairness rotates. Worker release triggers another bounded refill attempt.

This protects CPU fairness separately from Q1 memory isolation: private storage guarantees do not become permanent strict CPU priority.

## Outbound direct serialization handoff

`SubmitOutbound()` acquires a complete outbound Q1 bundle first, then calls the family's fixed encode thunk synchronously with the mutable `ByteLease` view. On successful encode:

1. the actual byte length is committed;
2. the bounded Adapter record is constructed;
3. the record is queued;
4. the call returns `Accepted`.

No pointer to the source Event/Command/State object is retained after the encode call. Once accepted, the family may release its source object according to its own semantics while Adapters independently owns the immutable complete representation.

## Lower transport and finite P2 pursuit

The generic lower-transport binding is transport-agnostic. It receives immutable complete bytes, neutral service class, generation-safe Adapter record identity, family/protocol/policy metadata, and an opaque bounded route token.

Immediate transport results may be accepted, transient/resource unavailable, or permanently rejected. Deferred completion returns through `CompleteTransport()` using both the exact Adapter record identity and lower-transport generation; stale completions are rejected and counted diagnostically.

`AdapterPursuitState` retains only normalized finite P2 runtime campaign state using local monotonic time: campaign start/hard residence deadline, logical attempt count, per-attempt admission deadline, retry-spacing envelope, next eligible time, and terminal state. Transport-internal fragment/link retry does not consume a Primitive logical attempt.

Lower-transport acceptance establishes only `LowerTransportAccepted`. Destination Primitive admission remains a stronger separate evidence boundary.

## Controlled shutdown

`Shutdown()`:

1. stops new submissions/refill;
2. quiesces lower ingress when the transport provides a quiesce thunk;
3. drains queued inbound/outbound/quarantine ownership;
4. waits according to T1 semantics only for already-executing bounded worker quanta;
5. cancels retained deferred transport work where supported;
6. releases all remaining volatile Adapter bundles.

Shutdown does not invoke arbitrary application callbacks to drain, force-kill executing workers, borrow another class's protected storage, or retain volatile payload bytes silently.

## Resource accounting

`ESPressio_AdapterResources.hpp` exposes target-specific compile-time accounting for a concrete `AdapterRuntime<...>` deployment. It reports runtime object size, per-domain record/byte reserves, queue topology, bindings, retained table, wake/generation storage, worker-pool objects, SharedOverflow/UntrustedIngress totals, and worker-stack configuration separately.

See [`RESOURCE_ACCOUNTING.md`](RESOURCE_ACCOUNTING.md) for the canonical host validation profile and measured CI evidence. Component diagnostic sizes overlap the aggregate runtime object and must not be summed as additional allocation.

## Minimal integration example

`examples/MockAdapterRuntime/mock_adapter_runtime.cpp` defines a tiny mock Primitive-family binding and lower-transport binding without importing any family or transport library. CI syntax-compiles this example with the same System + Primitive + Task dependency boundary.

Concrete integrations map these neutral contracts onto their own semantics. MeshAdapters binds Event/Command/State families to Mesh; RadioAdapters explicitly maps the neutral Adapter service taxonomy and Primitive metadata to Radio's wire-stable transport boundary. Neither concern belongs in this generic repository.

## Versioning

This new repository currently carries its initial package version `0.1.0`. The Primitive Platform structural redesign does not change version numbers, create tags/releases, or integrate to `main`; release/version work remains a separate authorized phase.
