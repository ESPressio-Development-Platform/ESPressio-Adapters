# ESPressio-Adapters dependency chart

Direct ESPressio dependencies on `primitives_redesign`:

```text
ESPressio-Adapters
├── ESPressio-System
├── ESPressio-Primitive
└── ESPressio-Task
```

The generic library deliberately has no direct dependency on Event, Command, State, Mesh, Radio, Threads, Timing, Serializable, Persistence, Observable or Security. Family and transport integration belongs to later adapter repositories.
