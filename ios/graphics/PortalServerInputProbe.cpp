#include "cbase.h"
#include "player.h"
#include "usercmd.h"
#include "PortalPlayerProbe.h"

extern "C" bool SourceIOSReadPortalPlayer(int index,IOSPortalPlayer *out) {
    CBasePlayer *player=UTIL_PlayerByIndex(index);
    if (!player || !out || player->IsFakeClient()) return false;
    const CUserCmd *cmd=player->GetLastUserCommand();
    if (!cmd) return false;
    out->command=cmd->command_number;
    out->checksum=cmd->GetChecksum();
    out->forward=cmd->forwardmove; out->side=cmd->sidemove; out->yaw=cmd->viewangles.y;
    out->flags=player->GetFlags();
    out->tickBase=TIME_TO_TICKS(player->GetTimeBase());
    const Vector &origin=player->GetAbsOrigin();
    out->x=origin.x; out->y=origin.y; out->z=origin.z;
    return true;
}
