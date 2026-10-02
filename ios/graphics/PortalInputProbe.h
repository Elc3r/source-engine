#pragma once
// Read-only inspection of a command produced by the original client input.
// No command fields are synthesized or written back to the client.
struct IOSPortalCommand {
    int sequence, buttons;
    float forward, side, pitch, yaw;
    int mouseX, mouseY;
    unsigned int checksum;
    bool serializationMatches;
};
typedef bool (*IOSReadPortalCommand)(int sequence, IOSPortalCommand *command);

typedef bool (*IOSFindPortalControl)(const char *name,float *x,float *y);
