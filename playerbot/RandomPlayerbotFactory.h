#ifndef _RandomPlayerbotFactory_H
#define _RandomPlayerbotFactory_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include <mutex>

class WorldPacket;
class Player;
class Unit;
class Object;
class Item;

class RandomPlayerbotFactory
{
    public:
        
        enum class NameRaceAndGender : uint8
        {
            // Generic is the category used for human & undead
            GenericMale = 0,
            GenericFemale,
            GnomeMale,
            GnomeFemale,
            DwarfMale,
            DwarfFemale,
            NightelfMale,
            NightelfFemale,
            DraeneiMale,
            DraeneiFemale,
            OrcMale,
            OrcFemale,
            TrollMale,
            TrollFemale,
            TaurenMale,
            TaurenFemale,
            BloodelfMale,
            BloodelfFemale
        };

        static constexpr NameRaceAndGender CombineRaceAndGender(uint8 gender, uint8 race)
        {
            switch (race)
            {
                case RACE_HUMAN: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::GenericMale) + gender);
                case RACE_ORC: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::OrcMale) + gender);
                case RACE_DWARF: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::DwarfMale) + gender);
                case RACE_NIGHTELF: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::NightelfMale) + gender);
                case RACE_UNDEAD: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::GenericMale) + gender);
                case RACE_TAUREN: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::TaurenMale) + gender);
                case RACE_GNOME: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::GnomeMale) + gender);
                case RACE_TROLL: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::TrollMale) + gender);
#ifndef MANGOSBOT_ZERO
                case RACE_DRAENEI: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::DraeneiMale) + gender);
                case RACE_BLOODELF: return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::BloodelfMale) + gender);
#endif
                default:
                    return static_cast<NameRaceAndGender>(static_cast<uint8>(NameRaceAndGender::GenericMale) + gender);
            }
        }

        RandomPlayerbotFactory(uint32 accountId);
		virtual ~RandomPlayerbotFactory() {}

	public:
        bool CreateRandomBot(uint8 cls, uint8 inputRace = 0);
        static void CreateRandomBots();
        static void CreateRandomGuilds();
        static void CreateRandomArenaTeams();
        static std::string CreateRandomGuildName();
        static bool isAvailableRace(uint8 cls, uint8 race);
        static bool isAvailableRole(uint8 cls, BotRoles role = BotRoles::BOT_ROLE_NONE);

        // Joint constrained race+class selection: builds the candidate set of
        // tuples that satisfy the effective team, the requested role, any
        // explicit race/class constraint (0 = unconstrained), a nonzero
        // configured weight, AND a real player-creation template
        // (GetPlayerInfo), then samples once from the FILTERED weight total
        // with an exclusive upper bound. Returns false only when no compatible
        // tuple exists - choosing a class globally and then hunting a race for
        // the team randomly rejected requests (Classic Horde drawing paladin)
        // even though compatible alternatives existed.
        static bool GetRandomTuple(Team team, BotRoles role, uint8 fixedRace, uint8 fixedClass, uint8& outRace, uint8& outClass);
        uint8 GetRandomClass(uint8 race = 0, BotRoles role = BotRoles::BOT_ROLE_NONE);
        static bool isRaceForTeam(uint8 race, Team team = Team::TEAM_BOTH_ALLOWED);
        uint8 GetRandomRace(uint8 cls, Team team = Team::TEAM_BOTH_ALLOWED);
        static std::string CreateRandomBotName(NameRaceAndGender raceAndGender);
        static void EnsureNamesInitialized();
    private:
        static std::string CreateRandomArenaTeamName();

        // Returns a name drawn from freeNames to the pool. A name is free
        // exactly when no `characters` row holds it (the pool is rebuilt from
        // ai_playerbot_names LEFT JOIN characters), so every creation failure
        // that gives up BEFORE the character is persisted must put its name
        // back or the pool drains for the lifetime of the process.
        // Callers must already hold nameMutex.
        static void ReturnFreeName(NameRaceAndGender raceAndGender, std::string const& name);
        static std::unordered_map<NameRaceAndGender, std::vector<std::string>> freeNames;
        static std::unordered_map<NameRaceAndGender, std::vector<std::string>> allNames;
        static std::mutex nameMutex;
        static bool namesInitialized;

        uint32 accountId;
        static std::map<uint8, std::vector<uint8> > availableRaces;
};

#endif
