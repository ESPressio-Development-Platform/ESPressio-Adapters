# ESPressio-Adapters Resource Accounting

This report records the Tranche-6 A6-23 resource-accounting evidence for the generic Adapter substrate. Resource values are target-specific: `ESPressio_AdapterResources.hpp` derives `sizeof` facts and configured reserve multiplications at compile time for the actual deployment profile. Component object sizes intentionally overlap `RuntimeObjectBytes` and therefore must not be summed with it.

## Canonical validation profile

The host validation profile used by `tests/test_adapter_resources.cpp` is:

- byte arena: 16 × 64-byte slots, 8 × 256-byte slots, 4 × 1024-byte slots;
- capacity domain: eight 1024-byte record slots plus the byte arena;
- inbound plane: six private service-class domains + SharedOverflow + UntrustedIngress;
- outbound plane: six private service-class domains + SharedOverflow;
- maximum family bindings: 4;
- queue depth: 8 per service-class/family queue cell;
- T1 workers: 2 inbound + 2 outbound;
- retained outbound records: 16;
- worker-stack example: 4096 bytes per worker (reported separately from the runtime object).

GitHub Actions run `34707561642`, commit `ac2c0de413a55b41dff31608f0d738f57a4de51a`, host Ubuntu 24.04 / GCC 13.3, reported:

| Resource fact | Host bytes / count |
| --- | ---: |
| Whole `AdapterRuntime` object | 274,096 bytes |
| Binding table | 272 bytes |
| Binding capacity | 4 |
| Queue bank, each direction | 13,376 bytes |
| Queue cell object | 552 bytes |
| Queue lease slot | 56 bytes |
| Queue cells per direction | 24 |
| Reserved queue lease slots per direction | 192 |
| Retained table | 968 bytes |
| Retained capacity | 16 |
| `AdapterWorkRecord` object | 328 bytes |
| Inbound T1 worker pool | 504 bytes |
| Outbound T1 worker pool | 504 bytes |
| Capacity wake target | 16 bytes |
| Capacity generation scalar | 8 bytes |
| SharedOverflow semantic reserve, both directions | 30,720 bytes |
| UntrustedIngress semantic reserve, inbound only | 15,360 bytes |
| Four worker stacks at 4096 bytes each | 16,384 bytes, provider-owned and separate |

## Per-service private domain

The canonical validation profile deliberately configures every service class identically, so Infrastructure, Clock, Critical, Responsive, Convergent and BestEffort each report the same private-domain footprint in both directions:

| Resource fact | Per private domain |
| --- | ---: |
| Domain object | 16,256 bytes |
| Record slot | 1,024 bytes |
| Record count | 8 |
| Record semantic reserve | 8,192 bytes |
| Byte-payload semantic reserve | 7,168 bytes |
| Maximum one retained bundle reserve | 2,048 bytes |

The 7,168-byte payload reserve is the exact configured slot multiset (`16×64 + 8×256 + 4×1024`). The maximum one-bundle reserve is the configured record slot plus the largest fitting payload slot (`1024 + 1024`).

## Accounting boundaries

`RuntimeObjectBytes` is the authoritative static/preallocated object footprint for a concrete `AdapterRuntime<...>` instantiation. It already contains its capacity planes, queue banks, retained table, binding table, synchronization objects, worker-pool objects and fixed metadata. The component sizes above are diagnostic breakdowns, not additive memory on top of the runtime object.

Worker stacks are not embedded in `AdapterRuntime`; they are owned by the configured Task provider. `AdapterRuntimeResourceAccounting::WorkerStackBytes()` therefore reports them separately from static runtime storage.

`RecordReservedBytes`, `BytePayloadReservedBytes`, SharedOverflow/UntrustedIngress reserve totals and maximum bundle values describe semantic configured capacity. They overlap the containing domain/runtime object and exist to make deployment guarantees auditable; they are not extra heap allocations.

The canonical runtime requires no post-`Initialize` heap allocation on its admitted/worker/release hot paths, as enforced by the host no-heap tests. Resource accounting introduces no growable container or allocation path.

## Target use

Include `ESPressio_AdapterResources.hpp` (also exported by `ESPressio_Adapters.hpp`) and instantiate `AdapterRuntimeResourceAccounting` with the exact inbound plane, outbound plane, binding count, queue depth, worker counts and retained capacity used by the deployment. The resulting constants are computed for that compiler/ABI/target and should be used instead of copying the host exemplar above into embedded sizing decisions.
