# Living Realm 0002: Organic policy and synthetic-action audit

- **Status:** Draft authoritative child design
- **Target:** Phase 0 and Living Realm 0.1
- **Depends on:** [0001](0001-living-realm.md)
- **Appendices:** [0002A compatibility matrix](0002-organic-policy-compatibility-matrix.md),
  [0002B audit protocol](0002-organic-policy-audit-protocol.md),
  [0002C recovery/transport](0002-recovery-and-transport.md)

## 1. Decision

Organic safety is enforced by a proposed `OrganicProgressionPolicy` called at
every known shortcut/fabrication boundary. Configuration validation computes an
effective policy, but config flags alone are not the security boundary. An
explicitly enabled Organic realm fails closed and never silently selects legacy
randomization or legacy timed rotation.

The complete mechanism classification is in 0002A. Durable
request/apply/reconcile behavior is in 0002B. The two permitted 0.1 autonomous
compatibility actions—modeled public transport and last-resort stuck
recovery—and protected group transport synchronization are specified in 0002C.

## 2. Policy interface

Every guarded path submits immutable context and receives one decision:

```cpp
enum class OrganicDecision
{
    AllowGameplay,
    AllowAutomation,
    RequireAudit,
    Deny
};

struct StateFingerprint
{
    uint16 version;
    ActionKind kind;
    std::string canonicalPayload;
};

struct OrganicRequest
{
    uint32 characterGuid;
    std::array<uint8, 16> identityNonce;
    ActionKind kind;
    SourceKind source;
    bool protectedRealPlayerCommitment;
    StateFingerprint before;
};

OrganicDecision Evaluate(OrganicRequest const& request);
```

`StateFingerprint` is an action-specific, versioned canonical field set. It
contains no pointer or process-local timestamp. 0002B defines its storage and
equality rules.

`AllowGameplay` uses ordinary core handlers/rules. `AllowAutomation` chooses
ordinary gameplay but cannot bypass eligibility or cost. `RequireAudit` is a
named, bounded compatibility/recovery action with an action-specific reconciler.
`Deny` has no side effect and returns a stable reason code. Unknown action kinds
default to `Deny` in Organic mode.

Enforcement occurs at five layers:

1. startup validation rejects incompatible effective settings;
2. managed bootstrap creates only `ORGANIC_CREATED` characters;
3. legacy login/rotation paths skip managed identities;
4. runtime call sites guard shortcuts, cheats, commands, sync, transport, revive,
   hotfix, and recovery;
5. tests and telemetry detect unclassified mutations.

The policy applies to managed random bots. Player-owned alts remain outside
Living Realm by default. GM commands directly implemented by the core are not
intercepted magically; an authenticated Living Realm administration surface must
route progression bypasses through a classified audited action. Broad Playerbots
`init`, `upgrade`, `refresh`, `maintenance`, `autogear`, or `hotfix` mutations are
prohibited in 0.1 because they cannot be reconciled as one bounded action.

## 3. Effective configuration and startup contract

On startup, Organic mode validates all relevant keys and emits an
effective-policy report containing configured value, effective value,
classification, enforcement point, and reason. Startup blocks on every
conflict. The operator file is never silently rewritten.

Mandatory 0.1 outcomes include:

- `AiPlayerbot.AsyncBotLogin=1`;
- no mixed managed/legacy population under the random-bot account prefix;
- legacy `AddRandomBots`/`ProcessBot` login decisions skip managed identities;
- managed login/logout does not create or consume legacy `add`/`logout` timers;
- `RandomBotTimedLogout` and `RandomBotTimedOffline` are ineffective for managed
  identities;
- random/instant level and gear randomization disabled;
- random money, bags, consumables, ammo, spells, skills, reputation, taxi nodes,
  mounts, pets, buffs, enchantments, guild/arena bootstrap, and quest
  initialization denied;
- XP multiplier fixed to ordinary realm rules for managed Organic bots;
- prequests, global bot quest rewards, quest sync in either direction, free spell
  learning, level sync, and hotfix progression denied;
- all 13 CMaNGOS `BotCheatMask` bits and per-bot runtime cheat overrides forced to
  zero for managed bots;
- random relocation, teleport-near-player, RPG camp teleport, legacy transport
  modes, free summon, silent revive, and temporary-money tricks denied;
- normal trainer purchases, talents from earned points, quest rewards, loot,
  vendors, mail, trade, taxis, hearthstones, eligible portals/summons, corpse
  release, and ordinary resurrection allowed;
- only the named audited actions in 0002C allowed in 0.1.

0.1 uses `BootstrapPolicy=require_fresh`: existing legacy random bots are not
silently certified. The managed reset/recreate workflow in 0003 is required. A
future adoption mode requires its own proof and migration design.

## 4. Fail-closed behavior

| Failure | Required result |
|---|---|
| Unknown profile/action/config conflict | Block managed Organic startup or deny action |
| `AsyncBotLogin` false or legacy path still owns managed identity | Block managed startup |
| Required migration missing/dirty | Block managed random-bot startup |
| Audit store/direct-commit path unavailable | Deny all `RequireAudit` actions; ordinary gameplay may continue |
| Malformed effective-policy entry/report | Quarantine affected bot or block globally according to scope |
| Unclassified mutation observed | Emit critical diagnostic, deny when interceptable, fail acceptance |
| Unsupported route/action postcondition | Block/invalidate goal; do not improvise |
| Worker/queue unavailable | Defer or safe idle; no fabrication |
| Managed reset partially complete | Resume/repair managed operation or block; never use raw legacy reset |

## 5. Telemetry

The Phase 0 telemetry surface is a proposed `LivingRealmTelemetrySink` interface
with an in-memory test sink and an optional structured append-only log sink.
Phase 0 emits observations that already exist: bot login/logout, group
join/leave, travel completion/failure, quest accept/reward, and intercepted
mutation decisions. Schedule and goal transitions are added with their 0.1
components.

Telemetry is not durable truth. Synthetic-action phase state lives in the audit
table. No policy or lifecycle decision depends solely on a log event.

## 6. Testing and acceptance

Required tests:

- one policy test for every row in 0002A;
- startup reports for valid, conflicting, unknown, missing-schema,
  mixed-population, and `AsyncBotLogin=false` configurations;
- fresh-provenance bootstrap/reset/delete/recreate tests;
- attempts through config, runtime, console, hotfix, quest sync, world buff,
  enchant, transport, revive, cheat, and recovery paths;
- every 0002B crash window and duplicate delivery;
- audit database outage, direct-commit failure, re-read mismatch, and malformed
  row behavior;
- route-mask, canonical public transport, tram, Teldrassil, group-sync, and stuck
  recovery tests from 0002C;
- Classic/TBC/WotLK compilation.

Acceptance requires: every inventoried path classified and guarded; no unknown
action fails open; managed login cannot bypass schedules through the legacy
path; fresh bots receive no synthetic progression; every successful permitted
synthetic action is eventually associated with one applied/reconciled audit
action; incomplete requests reconcile deterministically; and only canonical
modeled transport is available.
