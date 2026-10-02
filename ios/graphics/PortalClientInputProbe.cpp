#include "cbase.h"
#include "iinput.h"
#include "usercmd.h"
#include "tier1/bitbuf.h"
#include "PortalInputProbe.h"
#include "touch.h"

extern "C" bool SourceIOSReadPortalCommand(int sequence, IOSPortalCommand *out)
{
    CUserCmd *cmd=input?input->GetUserCmd(sequence):NULL;
    if (!cmd || !out || cmd->command_number!=sequence) return false;
    out->sequence=cmd->command_number; out->buttons=cmd->buttons;
    out->forward=cmd->forwardmove; out->side=cmd->sidemove;
    out->pitch=cmd->viewangles.x; out->yaw=cmd->viewangles.y;
    out->mouseX=cmd->mousedx; out->mouseY=cmd->mousedy;
    out->checksum=cmd->GetChecksum();
    // Exercise the real demo/network codec as well as the command ring.
    unsigned char bytes[4096]={};
    bf_write writer(bytes,sizeof(bytes));
    input->EncodeUserCmdToBuffer(writer,sequence);
    bf_read reader(bytes,writer.GetNumBytesWritten(),writer.GetNumBitsWritten());
    CUserCmd decoded, baseline;
    ReadUsercmd(&reader,&decoded,&baseline);
    out->serializationMatches=!writer.IsOverflowed() && !reader.IsOverflowed() &&
        decoded.GetChecksum()==out->checksum;
    return true;
}

extern "C" bool SourceIOSFindPortalControl(const char *name,float *x,float *y) {
    CTouchButton *button=gTouch.FindButton(name);
    if (!button || !x || !y) return false;
    *x=(button->x1+button->x2)*.5f; *y=(button->y1+button->y2)*.5f;
    return true;
}
