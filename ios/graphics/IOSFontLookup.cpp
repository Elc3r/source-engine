#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdlib>
#include <cstring>
#include <limits.h>

// Resolve the system face without Fontconfig or copying system/game font files.
// The existing FreeType backend owns glyph metrics and rasterization.
char *SourceIOSFontFileName(const char *name, bool bold, bool italic)
{
    if (!name) return NULL;
    if (!strcasecmp(name,"Tahoma") || !strcasecmp(name,"Verdana")) name="Helvetica";
    if (!strcasecmp(name,"Lucida Console")) name="Menlo";
    CFStringRef family=CFStringCreateWithCString(NULL,name,kCFStringEncodingUTF8);
    if (!family) return NULL;
    CTFontRef font=CTFontCreateWithName(family,16,NULL);
    CFRelease(family);
    if (!font) return NULL;
    CTFontSymbolicTraits traits=(bold?kCTFontBoldTrait:0)|(italic?kCTFontItalicTrait:0);
    if (traits) {
        CTFontRef styled=CTFontCreateCopyWithSymbolicTraits(font,0,NULL,traits,traits);
        if (styled) { CFRelease(font); font=styled; }
    }
    CFTypeRef value=CTFontCopyAttribute(font,kCTFontURLAttribute);
    char path[PATH_MAX]={};
    bool found=value && CFGetTypeID(value)==CFURLGetTypeID() &&
        CFURLGetFileSystemRepresentation(static_cast<CFURLRef>(value),true,
                                        reinterpret_cast<UInt8 *>(path),sizeof(path));
    if (value) CFRelease(value);
    CFRelease(font);
    return found?strdup(path):NULL;
}
