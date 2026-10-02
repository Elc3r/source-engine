#pragma once
// Read-only state from the original server player after command simulation.
struct IOSPortalPlayer {
    int command, tickBase;
    unsigned int checksum;
    float x,y,z;
};
typedef bool (*IOSReadPortalPlayer)(int index,IOSPortalPlayer *out);
