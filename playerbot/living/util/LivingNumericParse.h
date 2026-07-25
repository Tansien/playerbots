#pragma once

#include <cstdint>
#include <string>

namespace living
{
    // Pure, dependency-free parsing helpers shared by runtime code and the
    // host-side test target. They live under playerbot/living/ because that is the
    // module's home for code with no core/world dependency, which is exactly what
    // lets the host tests compile - and therefore regression-test - the same
    // functions the runtime calls.
    //
    // Safety invariant: these never throw and never narrow silently. The legacy
    // idiom they replace (isNumeric/isValidNumberString + std::stoi) accepts a lone
    // sign, ignores trailing garbage, throws std::out_of_range above INT_MAX, and
    // leaves range checks to the caller - which chat-command handlers then perform
    // after narrowing to uint8, mutating the wrong slot.

    // Parses text as an unsigned decimal integer. Requires: non-empty, digits only
    // (no sign, no whitespace, no suffix), full consumption, and a value that fits
    // in uint32. Returns false and leaves `out` untouched otherwise.
    bool TryParseUInt32(std::string const& text, uint32_t& out);

    // Same contract for uint64; used for chat/console-supplied GUID values.
    bool TryParseUInt64(std::string const& text, uint64_t& out);

    // Signed variant for fields that are legitimately negative in the wire format
    // (an item link's random-property id is negative when it denotes a random
    // SUFFIX). Accepts an optional leading '-' and rejects '+', matching the other
    // helpers' "digits only, full consumption, no silent narrowing" rule.
    bool TryParseInt32(std::string const& text, int32_t& out);

    // True when text is exactly a uint32 by the rule above. Use instead of an
    // isNumeric()-style check when the value will be parsed afterwards.
    bool IsExactUInt32(std::string const& text);

    // Parses an explicit slot index and validates it against `slotCount` BEFORE any
    // narrowing, so an out-of-range value can never alias a valid slot (e.g. 256
    // becoming 0 in a uint8). Returns false for any input TryParseUInt32 rejects
    // and for every value >= slotCount.
    bool TryParseSlotIndex(std::string const& text, uint32_t slotCount, uint8_t& out);

    // TryParseUInt32 plus an inclusive range check, so callers validate the domain
    // in the same step as the parse instead of narrowing first and checking later.
    bool TryParseUInt32InRange(std::string const& text, uint32_t minValue, uint32_t maxValue, uint32_t& out);

    // Parses text as a finite decimal float. Requires: non-empty, full consumption,
    // a finite result within float range. Rejects inf/nan/hex forms, whitespace and
    // trailing garbage. Accepts an optional leading sign. Returns false and leaves
    // `out` untouched otherwise.
    bool TryParseFloatExact(std::string const& text, float& out);

    // Result of parsing a chat-supplied money amount. The distinction matters at
    // transactional call sites: NoMoney means "this text is not a money amount"
    // (callers may fall through to item handling), Invalid means "this text LOOKS
    // like a money amount but is malformed or out of range" and must never be
    // executed as a zero-value (or wrapped small-value) transaction.
    enum class MoneyParseStatus
    {
        NoMoney,
        Ok,
        Invalid,
    };

    // Parses the leading whitespace-delimited token of `text` as a money amount in
    // the documented NgNsNc grammar: up to one gold, one silver and one copper
    // component, each "digits+unit", in that order, at least one component, and
    // nothing else in the token. Text after the first space is left for the caller
    // (trade/mail commands carry item links after the amount).
    //
    // The total is accumulated with checked uint64 arithmetic and validated against
    // maxCopper (the core money cap) BEFORE narrowing to uint32, so an input like
    // "4294967297g" is Invalid instead of wrapping to a small real transaction.
    // maxCopper must fit in uint32; larger caps are clamped to uint32 range.
    MoneyParseStatus TryParseMoneyPrefix(std::string const& text, uint64_t maxCopper, uint32_t& outCopper);
}
