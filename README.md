# ESPressio Adapters

`ESPressio-Adapters` is the family-neutral ownership and capacity substrate between typed Primitive families and physical/logical transports. It does **not** implement Event, Command, State, Mesh or Radio semantics.

The `primitives_redesign` implementation is being built around three locked contracts:

- **A1 owned bytes:** fixed size-class arenas, one contiguous independently reclaimable slot per payload, move-only generation-safe leases, and an explicit mutable-write → immutable-commit boundary. There is no chaining, live resizing or heap fallback.
- **Q1 protected capacity:** six non-lendable private service-class domains per enabled direction, opportunistic `SharedOverflow`, plus `UntrustedIngress` only on inbound. Admission reserves a complete record+bytes bundle transactionally and rolls every partial claim back on failure.
- **A2 generic runtime:** frozen family bindings, shared queue-less T1 workers, exact Primitive M1 evidence, bounded provenance and finite P2 pursuit. A2 is layered on the A1/Q1 substrate in the remaining Tranche-6 change sets.

## Current A1/Q1 surface

```cpp
#include <ESPressio_Adapters.hpp>
using namespace ESPressio::Adapters;

using Bytes = StaticByteArena<ByteClass<64, 4>, ByteClass<256, 2>, ByteClass<1024, 1>>;
using Domain = StaticCapacityDomain<256, 4, Bytes>;
using Outbound = CapacityPlane<AdapterDirection::Outbound,
    Domain, Domain, Domain, Domain, Domain, Domain, Domain>;

Outbound capacity;
capacity.Initialize();
CapacityReservation reservation;
if (capacity.TryAcquireTrusted(AdapterServiceClass::Responsive, 120, reservation)
        == AdapterResourceStatus::Success) {
    auto bytes = reservation.Bytes().MutableView();
    // bounded encoder writes directly into bytes.Data ...
    (void)reservation.Bytes().Commit(42);
    // Construct the adapter work record only after bytes are sealed.
}
```

A trusted class tries only its own private domain first, then `SharedOverflow`; it never borrows another class's private reserve. An outbound plane has no usable `UntrustedIngress` domain. An inbound plane adds a physically distinct quarantine domain for bytes that have not yet established trusted classification.

Every release of a complete capacity bundle advances a non-wrapping capacity generation and may coalesce a fixed infrastructure wake. The wake is never a reservation: consumers retry the nonblocking acquisition transaction.

## Dependency boundary

Direct dependencies are exactly `ESPressio-System`, `ESPressio-Primitive`, and `ESPressio-Task`. The package does not import family or transport libraries. This boundary is enforced by host configuration tests.

The repository had no package manifest before this tranche; its initial package version is `0.1.0`. Release/version work remains separate from the structural redesign.
