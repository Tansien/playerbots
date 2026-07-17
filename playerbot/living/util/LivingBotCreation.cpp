#include "LivingBotCreation.h"

namespace living
{
    bool PickWeightedIndex(std::vector<uint32_t> const& weights, uint64_t draw, size_t& outIndex)
    {
        uint64_t total = 0;
        for (uint32_t const weight : weights)
            total += weight;

        if (total == 0 || draw >= total)
            return false;

        for (size_t i = 0; i < weights.size(); ++i)
        {
            if (draw < weights[i])
            {
                outIndex = i;
                return true;
            }

            draw -= weights[i];
        }

        // Unreachable: draw < total and the loop consumes exactly total.
        return false;
    }
}
