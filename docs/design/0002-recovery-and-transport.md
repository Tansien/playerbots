# Living Realm 0002C: recovery and transport compatibility

[Back to design 0002](0002-organic-policy-and-audit.md)

## C.1 Scope and decision

Organic 0.1 permits three named synthetic compatibility actions:

- `PUBLIC_TRANSPORT_TRANSFER` for a canonical public boat, zeppelin, or tram
  route when Playerbots cannot physically perform the route;
- `TRANSPORT_GROUP_SYNC` for a bot already protected by a real-player group whose
  owner is on a live transport context; and
- `STUCK_EMERGENCY_TELEPORT` after the complete normal recovery ladder fails.

These are not random relocation. Every action is bounded, directly audited,
rate-limited, and reconciled from action-specific state.

## C.2 Travel capability and routing API

Current route selection does not expose link filtering. 0.1 therefore requires a
small TravelNode extension:

```cpp
enum class LivingTravelCapability : uint32
{
    Walk            = 1 << 0,
    Taxi            = 1 << 1,
    Portal          = 1 << 2,
    PublicTransport = 1 << 3
};

TravelPath FindRoute(
    WorldPosition const& from,
    WorldPosition const& to,
    uint32 capabilityMask);
```

The actual integration may use the existing `TravelNodePathType` mask, but it
MUST support these semantics:

1. select a route without compatibility transport when possible;
2. if none exists, select a route using only allowlisted public-transport links;
3. return the chosen link types and route IDs to the goal adapter;
4. reject unknown or non-allowlisted transport links;
5. never infer "only route requires transport" by examining only one untyped
   best path.

The route registry maps stable Living Realm `route_id` values to expansion,
origin and destination travel nodes, core transport template/path identifiers,
arrival regions, minimum modeled wait/travel duration, and bidirectional policy.

The registry is validated from world data at startup. Missing or ambiguous
identifiers disable that route and emit a startup diagnostic.

## C.3 Canonical public transport policy

`GAMEOBJECT_TYPE_MO_TRANSPORT` alone is not a sufficient classification because
the Deeprun Tram and ships/zeppelins share transport machinery.

0.1 decisions:

- the Deeprun Tram is an allowed canonical public transport route;
- the Rut'theran Village–Auberdine route MUST be represented so a fresh Night Elf
  population can leave Teldrassil;
- all enabled Classic routes MUST be enumerated in the route registry and covered
  by tests;
- an unknown transport, private scripted vehicle, instance transport, or route
  lacking a verified origin/destination is unsupported;
- `AiPlayerbot.TransportTeleportType` legacy modes 1 and 2 are not used for
  managed bots.

`PUBLIC_TRANSPORT_TRANSFER` is allowed only when the bot:

1. has a goal whose typed route contains the allowlisted link;
2. reaches the exact origin node through normal movement;
3. is not in combat, trade, BG/arena, instance transition, or incompatible
   protected commitment;
4. waits at the origin for the route's deterministic minimum delay;
5. has a durable verified `REQUESTED` action; and
6. transfers only to the registry's destination arrival region.

The transfer changes location only. It grants no taxi node, XP, money, item,
quest progress, or cooldown reset.

A future physical transport child design may replace modeled transfers. Until
then, disabling modeled transfer is supported but the startup report MUST name
stranded race/zone consequences, including Teldrassil.

## C.4 Public transport action state and reconciliation

The adapter phases are:

```text
TRAVEL_TO_ORIGIN
WAIT_FOR_DEPARTURE
REQUEST_TRANSFER
ARRIVE
VERIFY
```

The audit fingerprint includes:

```text
route_id
direction
origin_node_id
destination_node_id
origin_map/position
destination_map/arrival_region
wait_started_at_ms
minimum_transfer_at_ms
goal_id/generation
group/commitment context
```

Reconciliation:

- observed in destination arrival region with the expected route/direction and
  no conflicting state: applied;
- unchanged at origin before expiry: retryable or cancellable;
- unchanged at origin after expiry: cancelled/failed;
- on a verified physical transport for the same route: already satisfied or
  continue physical travel;
- elsewhere: ambiguous and quarantined.

## C.5 Protected group transport synchronization

`TRANSPORT_GROUP_SYNC` is available only when:

- a valid protected commitment exists;
- the owner is live, in the same group, and aboard a verified transport;
- the bot is close to the boarding context or has just failed the corresponding
  group-follow transition;
- the action is not used to enter an instance, BG, arena, or unsupported map;
- per-owner and per-bot rate limits pass.

Its fingerprint and postcondition are **transport-relative**, not a moving world
radius:

```text
owner_real_guid
group_id
transport_entry
transport_guid/counter where stable
transport_route_id
owner_relative_offset
bot_expected_relative_region
source transport/position
```

A successful sync is observed by the bot being attached to the expected
transport context with valid relative offsets, or by both owner and bot being in
the verified destination arrival region after disembark. The reconciler also
inspects durable `transguid` and transport-relative saved offsets when available.

A moving ship's former world coordinates are never used as the sole success
test.

## C.6 Stuck-recovery ladder

`STUCK_EMERGENCY_TELEPORT` has this mandatory escalation order:

1. recalculate the path;
2. choose a different reachable approach point;
3. clear/rebuild the current movement generator;
4. temporarily blacklist the failed point or target;
5. pause or block the current goal and choose bounded local ambient work;
6. use a normal hearthstone when eligible and strategically safe;
7. use normal corpse release, graveyard, spirit-healer, or resurrection behavior
   when dead;
8. travel normally to a known safe node when a route exists;
9. only then request `STUCK_EMERGENCY_TELEPORT`.

Default thresholds are configurable within hard validation bounds:

- at least three independent path failures;
- at least 120 seconds without meaningful progress;
- no active combat, trade, taxi, transport transition, BG/arena, or instance;
- no protected real-player commitment unless the owner explicitly authorizes
  recovery and remains notified;
- at most one emergency teleport per bot per 30 minutes and three per UTC day.

"Meaningful progress" is a reduction in remaining path distance beyond a
configured epsilon or completion of a route segment, not animation/movement
flags alone.

## C.7 Safe destination policy

Emergency destinations come from a prevalidated safe-node registry built from
supported inn, graveyard, city, and travel nodes. Selection requires:

- faction and expansion compatibility;
- no instance/BG/arena;
- valid map height and terrain;
- no hostile-only or restricted area;
- same map whenever possible;
- shortest supported safe route class;
- a stable node ID and bounded arrival region.

No arbitrary random coordinate, level-appropriate grind location, player-near
location, or hidden GM teleport is allowed. If no safe destination exists, the
goal is blocked and the bot is quarantined or held in safe idle.

The action fingerprint records path-failure evidence, source, selected safe-node
ID, destination region, rate-limit state, and goal/schedule/commitment versions.

Reconciliation follows source/destination/elsewhere rules. A bot at the selected
safe-node region is applied; unchanged at source within expiry may retry; any
other unexplained location is ambiguous.

## C.8 Tests and acceptance

Tests cover:

- link-mask route selection and unknown transport rejection;
- Deeprun Tram classification;
- Rut'theran–Auberdine Night Elf viability;
- every enabled Classic route in both directions;
- origin wait and no early transfer;
- crash windows for public transfer;
- protected group sync on moving transport and after disembark;
- saved transport-relative reconciliation;
- every stuck-ladder stage;
- rate limits and protected-commitment deferral;
- invalid safe nodes and no-destination behavior;
- disabled modeled transport with explicit stranded-population diagnostics.

Acceptance requires no arbitrary transport or stuck teleport path, no location
mutation before verified audit durability, and deterministic action-specific
reconciliation.
