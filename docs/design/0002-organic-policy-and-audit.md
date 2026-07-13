# Living Realm 0002: Organic policy and synthetic-action audit

- **Status:** Draft authoritative child design
- **Target:** Phase 0 and Living Realm 0.1
- **Depends on:** [0001](0001-living-realm.md)

## 1. Decision

Organic safety is enforced by a proposed `OrganicProgressionPolicy` called at every known shortcut/fabrication boundary. Configuration validation computes an effective policy, but config flags alone are not the security boundary. An explicitly enabled Organic realm fails closed and never silently selects legacy randomization.

The complete mechanism classification is in [the compatibility matrix](0002-organic-policy-compatibility-matrix.md). The durable request/apply/reconcile protocol and schema are in [the audit protocol](0002-organic-policy-audit-protocol.md).

## 2. Policy interface

Every guarded path submits immutable context and receives one decision:

```cpp
enum class OrganicDecision { AllowGameplay, AllowAutomation, RequireAudit, Deny };
struct OrganicRequest {
    uint32 characterGuid;
    ActionKind kind;
    SourceKind source;
    bool protectedRealPlayerCommitment;
    StateFingerprint before;
};
OrganicDecision Evaluate(OrganicRequest const& request);
```

`AllowGameplay` uses ordinary core handlers/rules. `AllowAutomation` chooses ordinary gameplay but cannot bypass eligibility/cost. `RequireAudit` is a named, bounded compatibility/recovery action. `Deny` has no side effect and returns a reason code. Unknown action kinds default to `Deny` in Organic mode.

Enforcement occurs at four layers:

1. startup validation rejects incompatible effective settings;
2. construction/bootstrap creates only `ORGANIC_CREATED` characters;
3. runtime call sites guard shortcuts, cheats, console paths, sync, transport, revive, and recovery;
4. tests and telemetry detect unclassified mutations.

The policy applies to managed random bots. Player-owned alts remain outside Living Realm by default. GM commands directly implemented by the core are not intercepted magically; an authenticated Living Realm admin surface must route progression bypasses through a classified audited action. Broad Playerbots `init`, `upgrade`, or `refresh` mutations are prohibited in 0.1 because they cannot be reconciled as one bounded action.

## 3. Effective configuration

On startup, Organic mode validates all relevant keys and emits an effective-policy report containing configured value, effective value, classification, and reason. Strict mode blocks startup on every conflict; non-strict mode may apply documented runtime overrides only when the result remains fail closed. The operator file is never silently rewritten.

Required 0.1 outcomes include:

- random/instant level and gear randomization disabled;
- random money, bags, consumables, spells, skills, reputation, taxi, mounts, pets, and quest initialization denied;
- XP multiplier fixed to ordinary realm rules for managed Organic bots;
- prequests, globally rewarded bot quests, free spell learning, and level sync denied;
- random relocation, teleport-near-player, RPG camp teleport, dock-to-dock transport, free summon, and silent revive denied;
- configured item/taxi/breath cheats removed from managed Organic bots;
- normal trainer purchases, talents from earned points, quest rewards, loot, vendors, mail, trade, taxis, hearthstones, eligible portals/summons, corpse release, and ordinary resurrection allowed;
- only named audited recovery actions allowed.

0.1 uses `BootstrapPolicy=require_fresh`: existing legacy random bots are not silently certified. The operator must reset/recreate a managed Organic population. A future adoption mode requires its own proof/migration design.

## 4. Transport decision

Autonomous bots cannot use ship/zeppelin shortcuts and cannot select a goal whose only supported route requires them. Routine transport teleportation is denied.

For a protected real-player commitment only, `TRANSPORT_GROUP_SYNC` may synchronize a bot after revalidating owner, group, source/destination context, and distance. It is `RequireAudit`, rate-limited, and never available to ambient/autonomous goals. A later child design must implement real boarding/riding/disembarking before autonomous use.

## 5. Fail-closed behavior

| Failure | Required result |
|---|---|
| Unknown profile/action/config conflict | Block managed Organic startup or deny action |
| Required migration missing/dirty | Block managed random-bot startup |
| Audit store unavailable | Deny all `RequireAudit` actions; ordinary gameplay may continue |
| Malformed policy row/report | Quarantine affected bot or block globally according to scope |
| Unclassified mutation path observed | Emit critical diagnostic, deny when interceptable, fail acceptance |
| Unsupported route | Block/invalidate goal; do not teleport |
| Worker/queue unavailable | Defer or safe idle; no fabrication |
| Operator asks to return to legacy | Require explicit config change and clean restart |

## 6. Minimal event surface

Phase 0/0.1 emits synthetic action `REQUESTED`, `APPLIED`, `FAILED`, `RECONCILED`, and `CANCELLED` events plus the minimal lifecycle/goal events in 0001. Events are telemetry; the audit row and observed core state are reconciled truth.

## 7. Testing and acceptance

Required tests:

- one policy test for every row in the compatibility matrix;
- startup reports for valid, overridden, conflicting, unknown, missing-schema, and disabled configurations;
- fresh-provenance bootstrap/reset/delete/recreate tests;
- attempts through config, runtime, console, transport, revive, and recovery paths;
- all audit crash windows and duplicate delivery;
- audit database outage and malformed row behavior;
- disabled-mode parity;
- Classic/TBC/WotLK compilation.

Acceptance requires: every inventoried path classified and guarded; no unknown action fails open; fresh managed bots receive no synthetic progression; every successful permitted synthetic action is eventually associated with one applied/reconciled audit action; incomplete requests reconcile deterministically; autonomous transport shortcuts remain impossible; and disabling Living Realm preserves legacy behavior.
