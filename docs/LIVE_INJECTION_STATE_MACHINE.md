# Live injection state machine

The deterministic publisher separates profile lifecycle from live signal lifecycle.

```text
NO_PROFILE
   |
   v
PROFILE_VALIDATED
   |
   v
ARMED
   |
   +---- START ----> RUNNING
   |                   |
   |                   +---- live signal generation N -> N+1
   |                   +---- live signal generation N+1 -> N+2
   |                   |
   |                   +---- STOP ----> STOPPED
   |                                      |
   +--------------------------------------+
```

Profile identity/layout changes are rejected while RUNNING. Live signal-state changes are allowed and committed coherently at a sample boundary. A development serial transport is currently used to prove this behavior; it will later be replaced by the versioned host/device control protocol without changing the realtime state semantics.
