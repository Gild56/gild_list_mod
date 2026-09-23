#include "Levels.hpp"
#include "../API.hpp"
#include "../../Utils/WedReq.hpp"
#include "../../Cache/Levels/Levels.hpp"
#include "../../Events/DemonlistLoadedEvent.hpp"
#include "../../Events/LevelLoadedEvent.hpp"
#include <string>

namespace GDL::API::Levels {
    void getDemonlist() {
        auto& cachedDemonlist = GDL::Cache::Levels::getDemonlist();
        if (!cachedDemonlist.empty()) {
            DemonlistLoadedEvent().send(Ok(cachedDemonlist));
            return;
        }

        Utils::WebReq(
            LEVEL_LIST_EP,
            matjson::Value::object(),
            matjson::Value::object(),
            [](matjson::Value data, APIError error) {
                if (error) {
                    DemonlistLoadedEvent().send(Err(error));
                    return;
                }
                
                matjson::Value arrayData;
                
                if (data.isArray()) {
                    arrayData = data;
                } else if (data.isObject()) {
                    if (data.contains("data") && data["data"].isArray()) {
                        arrayData = data["data"];
                    } else if (data.contains("levels") && data["levels"].isArray()) {
                        arrayData = data["levels"];
                    } else {
                        log::error("API ERROR: Expected array, got object. Server response: {}", data.dump(matjson::NO_INDENTATION));
                        DemonlistLoadedEvent().send(Err(APIError{APIErrorType::InvalidEndpointResponse, APIMessage::None}));
                        return;
                    }
                } else {
                    log::error("API ERROR: Unrecognized JSON format. Server response: {}", data.dump(matjson::NO_INDENTATION));
                    DemonlistLoadedEvent().send(Err(APIError{APIErrorType::InvalidEndpointResponse, APIMessage::None}));
                    return;
                }

                if (arrayData.asArray().unwrap().empty()) {
                    log::error("API ERROR: The array is empty []!");
                    DemonlistLoadedEvent().send(Err(APIError{APIErrorType::InvalidEndpointResponse, APIMessage::None}));
                    return;
                }

                std::vector<GDLLevel> levels;

                for (const auto& level : arrayData.asArray().unwrap()) {
                    int id = 0;
                    if (level.contains("id")) {
                        if (level["id"].isString()) {
                            std::string idStr = level["id"].asString().unwrapOrDefault();
                            try { id = std::stoi(idStr.empty() ? "0" : idStr); } catch(...) {}
                        } else {
                            id = level["id"].asInt().unwrapOrDefault();
                        }
                    }
                    
                    int ingameID = id; 
                    int placement = level.contains("position") ? level["position"].asInt().unwrapOrDefault() : 0;
                    std::string name = level.contains("name") ? level["name"].asString().unwrapOrDefault() : "Unknown";
                    
                    double points = 0.0;
                    int listPercent = 100;
                    int length = 0;
                    std::string holder = "";
                    std::string verifier = "Unknown";
                    int verifierID = 0;
                    std::string verificationURL = "";
                    std::string dateCreated = "";

                    auto gdlLevel = GDLLevel{
                        id, ingameID, placement, name, points,
                        listPercent, length, holder, verifier,
                        verifierID, verificationURL, dateCreated
                    };
                    levels.push_back(gdlLevel);
                }
                
                GDL::Cache::Levels::setDemonlist(std::move(levels));
                DemonlistLoadedEvent().send(Ok(GDL::Cache::Levels::getDemonlist()));
            }
        );
    }

    void getLevel(int levelID, bool isFullInfoRequire) {
        auto cachedLevel = GDL::Cache::Levels::getLevel(levelID);
        if (cachedLevel && (isFullInfoRequire ? cachedLevel->isFull() : true)) {
            LevelLoadedEvent(levelID).send(Ok(cachedLevel));
            return;
        }

        std::string url = BASE_URL + "/lists/id/" + std::to_string(levelID);

        Utils::WebReq(
            url,
            matjson::Value::object(), 
            matjson::Value::object(),
            [levelID](matjson::Value data, APIError error) {
                if (error) {
                    LevelLoadedEvent(levelID).send(Err(error));
                    return;
                }

                matjson::Value levelData;
                if (data.isArray() && data.asArray().unwrap().size() > 0) {
                    levelData = data.asArray().unwrap()[0];
                } else if (data.isObject()) {
                    if (data.contains("data") && data["data"].isArray() && data["data"].asArray().unwrap().size() > 0) {
                        levelData = data["data"].asArray().unwrap()[0];
                    } else if (data.contains("data") && data["data"].isObject()) {
                        levelData = data["data"];
                    } else {
                        levelData = data;
                    }
                }

                if (!levelData.isObject() || levelData.size() == 0 || !levelData.contains("position")) {
                    LevelLoadedEvent(levelID).send(Err(APIError{APIErrorType::InvalidEndpointResponse, APIMessage::LevelNotFound}));
                    return;
                }

                int id = 0;
                if (levelData.contains("id") && levelData["id"].isString()) {
                    std::string idStr = levelData["id"].asString().unwrapOrDefault();
                    try { id = std::stoi(idStr.empty() ? "0" : idStr); } catch(...) {}
                } else if (levelData.contains("id")) {
                    id = levelData["id"].asInt().unwrapOrDefault();
                }

                int ingameID = id;
                int placement = levelData.contains("position") ? levelData["position"].asInt().unwrapOrDefault() : 0;
                std::string name = levelData.contains("name") ? levelData["name"].asString().unwrapOrDefault() : "Unknown";
                double points = 0.0;
                int listPercent = 100;
                int length = 0;
                int objects = 0;
                std::string description = levelData.contains("description") ? levelData["description"].asString().unwrapOrDefault() : "";
                std::string creator = "Unknown";
                std::string holder = "";
                std::string songURL = "";
                int gameVersion = 22;
                
                std::string verifier = "Unknown";
                int verifierID = 0;
                std::string verificationURL = "";
                
                bool isCopyable = false;
                std::string password = "";
                std::string dateCreated = "";

                auto gdlLevel = GDLLevel{
                    id, ingameID, placement, name, points, listPercent,
                    length, holder, verifier, verifierID, verificationURL,
                    dateCreated, objects, description, creator,
                    songURL, gameVersion, isCopyable, password
                };
                
                GDL::Cache::Levels::setLevel(std::move(gdlLevel));
                LevelLoadedEvent(levelID).send(Ok(GDL::Cache::Levels::getLevel(levelID)));
            }
        );
    }
}
