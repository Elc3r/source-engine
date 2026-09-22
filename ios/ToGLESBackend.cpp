#include "togles/rendermechanism.h"
#include "ToGLESBackend.h"
#include <EGL/egl.h>
#include <string.h>

static char missingFunctions[2048];

static bool HasExtension(const char *extension)
{
    typedef const GLubyte *(*GetString)(GLenum);
    GetString getString = reinterpret_cast<GetString>(eglGetProcAddress("glGetString"));
    const char *list = getString ? reinterpret_cast<const char *>(getString(GL_EXTENSIONS)) : NULL;
    if (!list) return false;
    size_t length = strlen(extension);
    for (const char *match = strstr(list, extension); match; match = strstr(match + length, extension))
        if ((match == list || match[-1] == ' ') && (match[length] == ' ' || !match[length])) return true;
    return false;
}

// D3D9 shaders use vertex attributes, never gl_VertexID. When the GLES driver
// lacks a base-vertex extension, shift per-vertex attributes for this one draw
// and restore the VAO. Instanced attributes are intentionally not shifted.
static void DrawRangeBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count,
    GLenum type, const void *indices, GLint baseVertex)
{
    if (!baseVertex) { gGL->glDrawRangeElements(mode, start, end, count, type, indices); return; }
    typedef void (*GetAttrib)(GLuint, GLenum, GLint *);
    typedef void (*GetPointer)(GLuint, GLenum, void **);
    typedef void (*IPointer)(GLuint, GLint, GLenum, GLsizei, const void *);
    GetAttrib getAttrib = reinterpret_cast<GetAttrib>(eglGetProcAddress("glGetVertexAttribiv"));
    GetPointer getPointer = reinterpret_cast<GetPointer>(eglGetProcAddress("glGetVertexAttribPointerv"));
    IPointer iPointer = reinterpret_cast<IPointer>(eglGetProcAddress("glVertexAttribIPointer"));
    struct Attribute { GLint enabled, divisor, buffer, size, type, normalized, stride, integer, step; void *pointer; };
    GLint maxAttribs, oldBuffer;
    gGL->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    gGL->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    CUtlVector<Attribute> attributes;
    attributes.SetCount(maxAttribs);
    bool valid = true;
    for (GLint i=0; i<maxAttribs; ++i) {
        Attribute &a = attributes[i];
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &a.divisor);
        if (!a.enabled || a.divisor) continue;
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.buffer);
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.normalized);
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
        getAttrib(i, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &a.integer);
        getPointer(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.pointer);
        GLint bytes = 0;
        switch (a.type) {
            case GL_BYTE: case GL_UNSIGNED_BYTE: bytes = a.size; break;
            case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_HALF_FLOAT: bytes = 2*a.size; break;
            case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: case GL_FIXED: bytes = 4*a.size; break;
            case GL_INT_2_10_10_10_REV: case GL_UNSIGNED_INT_2_10_10_10_REV: bytes = 4; break;
        }
        a.step = a.stride ? a.stride : bytes;
        intptr_t offset = reinterpret_cast<intptr_t>(a.pointer) + intptr_t(baseVertex) * a.step;
        if (!a.buffer || !bytes || offset < 0) { valid = false; break; }
    }
    if (!valid) { Warning("ToGLES: invalid vertex buffer offset for base-vertex draw\n"); return; }
    for (GLint i=0; i<maxAttribs; ++i) {
        Attribute &a = attributes[i];
        if (!a.enabled || a.divisor) continue;
        const void *shifted = reinterpret_cast<void *>(reinterpret_cast<intptr_t>(a.pointer) + intptr_t(baseVertex)*a.step);
        gGL->glBindBuffer(GL_ARRAY_BUFFER,a.buffer);
        if (a.integer) iPointer(i,a.size,a.type,a.stride,shifted);
        else gGL->glVertexAttribPointer(i,a.size,a.type,a.normalized,a.stride,shifted);
    }
    gGL->glDrawRangeElements(mode,start,end,count,type,indices);
    for (GLint i=0; i<maxAttribs; ++i) {
        Attribute &a = attributes[i];
        if (!a.enabled || a.divisor) continue;
        gGL->glBindBuffer(GL_ARRAY_BUFFER,a.buffer);
        if (a.integer) iPointer(i,a.size,a.type,a.stride,a.pointer);
        else gGL->glVertexAttribPointer(i,a.size,a.type,a.normalized,a.stride,a.pointer);
    }
    gGL->glBindBuffer(GL_ARRAY_BUFFER,oldBuffer);
}

static void *Lookup(const char *name, bool &okay, const bool required, void *fallback)
{
    // GLES 3.0 exposes base-vertex drawing as an extension, not a core function.
    if (!strcmp(name, "glDrawRangeElementsBaseVertex")) {
        if (HasExtension("GL_EXT_draw_elements_base_vertex")) name = "glDrawRangeElementsBaseVertexEXT";
        else if (HasExtension("GL_OES_draw_elements_base_vertex")) name = "glDrawRangeElementsBaseVertexOES";
        else return reinterpret_cast<void *>(DrawRangeBaseVertex);
    }
    void *result = reinterpret_cast<void *>(eglGetProcAddress(name));
    if (!result && required) {
        size_t used = strlen(missingFunctions);
        snprintf(missingFunctions + used, sizeof(missingFunctions) - used, "%s%s", used ? ", " : "", name);
    }
    return result;
}

int InitializeToGLESBackend(char *detail, size_t capacity)
{
    if (gGL) { snprintf(detail,capacity,"ToGLES dispatch is already initialized"); return 0; }
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
        snprintf(detail, capacity, "ToGLES requires a current EGL context"); return 0;
    }
    extern GL_GetProcAddressCallbackFunc_t gGL_GetProcAddressCallback;
    gGL_GetProcAddressCallback = Lookup;
    missingFunctions[0] = 0;
    gGL = new COpenGLEntryPoints;
    if (!gGL->m_bHave_OpenGL || !gGL->glTexImage2D || !gGL->glReadPixels || !gGL->glGenFramebuffers) {
        snprintf(detail, capacity, "Missing GLES entry points: %s", missingFunctions);
        ShutdownToGLESBackend(); return 0;
    }
    if (gGL->m_bHave_GL_QCOM_alpha_test || gGL->m_bHave_GL_EXT_direct_state_access) {
        snprintf(detail, capacity, "Unexpected desktop/QCOM capability on ANGLE Metal");
        ShutdownToGLESBackend(); return 0;
    }
    return 1;
}

void ShutdownToGLESBackend(void)
{
    delete gGL;
    gGL = NULL;
}

void DrawToGLESIndexed(unsigned mode, unsigned start, unsigned end, int count,
    unsigned type, const void *indices, int baseVertex)
{
    gGL->glDrawRangeElementsBaseVertex(mode,start,end,count,type,indices,baseVertex);
}
