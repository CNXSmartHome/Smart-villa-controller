# Logic Engine

## V1 Rule Model

Basic rule:

```text
IF condition
FOR duration
THEN action
```

## Example Rules

- IF presence = false FOR 5 min THEN light relay = off
- IF presence = false FOR 20 min THEN AC relay = off
- IF manual override = true THEN disable auto-off

## Safety

- Rules must never override fault safe state.
- Invalid config must not energize relay.
