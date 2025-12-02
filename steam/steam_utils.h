#pragma once
#include <string>
#include <vector>
#include <steam_api.h>

class SteamUtils {
public:
    struct FriendInfo
    {
        CSteamID id;
        std::string name;
        std::string avatarDataUrl;
    };

    static std::vector<FriendInfo> getFriendsList();
};
