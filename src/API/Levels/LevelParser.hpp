#pragma once

// Parser for the Gild list API:
//   GET https://gild56-website.onrender.com/api/lists/gild/classic
//
// The endpoint returns a *bare JSON array*:
//   [ { "id": "149183462", "name": "Shitty Flamewall", "position": 1,
//       "description": "...", "completions": { "Sao": "7dESNubydBA" } }, ... ]
//
// IMPORTANT: "id" is a JSON *string*. matjson's asInt() fails on strings (and unwrapOrDefault()
// would silently turn every level id into 0), so every numeric field goes through toInteger() /
// toNumber(), which accept both JSON numbers and numeric strings.
//
// The parser is deliberately tolerant so the backend can change shape without a mod update:
//   - root:      array, or an object holding the array under data / levels / list / items / results
//                (nested too, e.g. { "data": { "levels": [...] } })
//   - level id:  level_id | levelID | levelId | ingame_id | level.{id,...} | id   (most specific first)
//   - position:  placement | position | rank
//   - points:    points | score   (optional; 0 when the API doesn't send it - the Gild API doesn't)
//
// This header only depends on matjson + GDLLevel (no cocos2d, no networking), so it is easy to test.

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>
#include "../../Models/GDLLevel.hpp"

namespace GDL::API::Levels::Parser {
    namespace detail {
        // Copy of obj[key] if obj is an object containing that key, otherwise nullopt.
        inline std::optional<matjson::Value> field(matjson::Value const& obj, char const* key) {
            if (!obj.isObject() || !obj.contains(key)) return std::nullopt;
            return obj[key];
        }

        // "  149183462 " -> 149183462. Rejects anything that is not a whole base-10 integer.
        inline std::optional<long long> parseIntegerString(std::string const& raw) {
            size_t begin = 0;
            size_t end = raw.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(raw[begin]))) ++begin;
            while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) --end;
            if (begin == end) return std::nullopt;

            long long out = 0;
            auto const* first = raw.data() + begin;
            auto const* last = raw.data() + end;
            auto [ptr, ec] = std::from_chars(first, last, out);
            if (ec != std::errc{} || ptr != last) return std::nullopt;
            return out;
        }

        // JSON number OR numeric string -> integer.
        inline std::optional<long long> toInteger(matjson::Value const& v) {
            if (v.isString()) {
                auto str = v.asString();
                if (str.isOk()) return parseIntegerString(str.unwrap());
                return std::nullopt;
            }
            if (v.isNumber()) {
                auto asInt = v.asInt();
                if (asInt.isOk()) return static_cast<long long>(asInt.unwrap());

                // e.g. 149183462.0, or a value that doesn't fit into int
                auto asDouble = v.asDouble();
                if (asDouble.isOk()) {
                    double d = asDouble.unwrap();
                    if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 9.0e15) {
                        return static_cast<long long>(d);
                    }
                }
            }
            return std::nullopt;
        }

        // JSON number OR numeric string -> double.
        inline std::optional<double> toNumber(matjson::Value const& v) {
            if (v.isNumber()) {
                auto d = v.asDouble();
                if (d.isOk() && std::isfinite(d.unwrap())) return d.unwrap();
                return std::nullopt;
            }
            if (v.isString()) {
                auto s = v.asString();
                if (!s.isOk()) return std::nullopt;

                std::string const str = s.unwrap();
                if (str.empty()) return std::nullopt;

                char* end = nullptr;
                double d = std::strtod(str.c_str(), &end);
                if (end == str.c_str() || !std::isfinite(d)) return std::nullopt;
                while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
                if (*end != '\0') return std::nullopt;
                return d;
            }
            return std::nullopt;
        }

        inline std::optional<int> toPositiveInt(std::optional<long long> v) {
            if (v && *v > 0 && *v <= INT_MAX) return static_cast<int>(*v);
            return std::nullopt;
        }

        inline std::optional<int> readLevelID(matjson::Value const& entry) {
            auto tryKeys = [](matjson::Value const& obj, std::initializer_list<char const*> keys) -> std::optional<int> {
                for (auto key : keys) {
                    if (auto v = field(obj, key)) {
                        if (auto id = toPositiveInt(toInteger(*v))) return id;
                    }
                }
                return std::nullopt;
            };

            // 1. Unambiguous "level id" style keys.
            if (auto id = tryKeys(entry, {"level_id", "levelID", "levelId", "ingame_id"})) return id;

            // 2. A nested "level" object (or a bare id stored directly in "level").
            if (auto nested = field(entry, "level")) {
                if (nested->isObject()) {
                    if (auto id = tryKeys(*nested, {"id", "level_id", "levelID", "levelId", "ingame_id"})) return id;
                }
                else if (auto id = toPositiveInt(toInteger(*nested))) {
                    return id;
                }
            }

            // 3. Generic "id" (this is what the Gild API uses).
            return tryKeys(entry, {"id"});
        }

        inline std::optional<int> readPlacement(matjson::Value const& entry) {
            for (auto key : {"placement", "position", "rank"}) {
                if (auto v = field(entry, key)) {
                    if (auto n = toPositiveInt(toInteger(*v))) return n;
                }
            }
            return std::nullopt;
        }

        inline double readPoints(matjson::Value const& entry) {
            for (auto key : {"points", "score"}) {
                if (auto v = field(entry, key)) {
                    if (auto n = toNumber(*v)) return *n;
                }
            }
            return 0.0;
        }

        inline std::string readString(matjson::Value const& obj, char const* key) {
            if (auto v = field(obj, key)) {
                if (v->isString()) return v->asString().unwrapOrDefault();
            }
            return {};
        }

        inline std::string readName(matjson::Value const& entry) {
            auto name = readString(entry, "name");
            if (name.empty()) name = readString(entry, "level_name");
            if (name.empty()) {
                if (auto nested = field(entry, "level")) name = readString(*nested, "name");
            }
            return name;
        }
    }

    struct ParseStats {
        size_t total = 0;      // entries seen in the array
        size_t skipped = 0;    // no valid id / position, or not an object
        size_t duplicates = 0; // same level id listed twice (the better placement wins)
    };

    // Finds the array of levels inside the response.
    inline bool extractLevelArray(matjson::Value const& root, matjson::Value& out, int depth = 0) {
        if (root.isArray()) {
            out = root;
            return true;
        }
        if (!root.isObject() || depth > 4) return false;

        for (auto key : {"data", "levels", "list", "items", "results"}) {
            if (auto inner = detail::field(root, key)) {
                if (extractLevelArray(*inner, out, depth + 1)) return true;
            }
        }
        return false;
    }

    // Parses the API response into levels sorted by placement (ascending), one entry per level id.
    // Returns an empty vector if the response has no usable levels.
    inline std::vector<GDLLevel> parseLevels(matjson::Value const& root, ParseStats* stats = nullptr) {
        ParseStats local;
        ParseStats& st = stats ? *stats : local;
        st = {};

        std::vector<GDLLevel> levels;

        matjson::Value array;
        if (!extractLevelArray(root, array)) return levels;

        for (auto const& entry : array) {
            ++st.total;
            if (!entry.isObject()) {
                ++st.skipped;
                continue;
            }

            auto id = detail::readLevelID(entry);
            auto placement = detail::readPlacement(entry);
            if (!id || !placement) {
                ++st.skipped;
                continue;
            }

            // The Gild API has no separate internal id, no length, no verifier data, etc.
            // Those fields stay at their defaults; `id` mirrors the in-game id.
            GDLLevel level{
                .id = *id,
                .ingameID = *id,
                .placement = *placement,
                .name = detail::readName(entry),
                .points = detail::readPoints(entry),
                .listPercent = 100
            };

            if (auto description = detail::field(entry, "description"); description && description->isString()) {
                level.description = description->asString().unwrapOrDefault();
            }

            levels.push_back(std::move(level));
        }

        std::stable_sort(levels.begin(), levels.end(), [](GDLLevel const& a, GDLLevel const& b) {
            return a.placement < b.placement;
        });

        std::vector<GDLLevel> unique;
        unique.reserve(levels.size());
        std::unordered_set<int> seen;
        for (auto& level : levels) {
            if (seen.insert(level.ingameID).second) unique.push_back(std::move(level));
            else ++st.duplicates;
        }

        return unique;
    }
}
