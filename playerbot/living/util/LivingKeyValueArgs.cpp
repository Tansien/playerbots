#include "LivingKeyValueArgs.h"

namespace living
{
    bool TryParseKeyValueArgs(std::string const& text, std::set<std::string> const& allowedKeys,
        std::map<std::string, std::string>& out, std::string& error)
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t const end = text.find(' ', pos);
            std::string const token = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? text.size() : end + 1;

            if (token.empty())
                continue; // collapsed duplicate spaces

            size_t const eqPos = token.find('=');
            if (eqPos == std::string::npos)
            {
                error = "Unexpected argument (expected key=value): " + token;
                return false;
            }

            std::string const key = token.substr(0, eqPos);
            std::string const value = token.substr(eqPos + 1);

            if (key.empty())
            {
                error = "Missing key in argument: " + token;
                return false;
            }

            if (allowedKeys.find(key) == allowedKeys.end())
            {
                error = "Unknown argument: " + key;
                return false;
            }

            if (value.empty())
            {
                error = "Empty value for argument: " + key;
                return false;
            }

            if (out.find(key) != out.end())
            {
                error = "Duplicate argument: " + key;
                return false;
            }

            out[key] = value;
        }

        return true;
    }
}
