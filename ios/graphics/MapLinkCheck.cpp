#include "render_pch.h"
#include "modelloader.h"

// Link anchor only. Calling the loader requires host/cache/physics startup;
// this module is intentionally excluded from the app until that exists.
extern "C" IModelLoader *SourceIOSMapLoaderLinkAnchor()
{
    return modelloader;
}
