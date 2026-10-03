#ifndef TOGLES_TEXTURE_UPLOAD_H
#define TOGLES_TEXTURE_UPLOAD_H

// GL types are provided by the caller's rendermechanism/GL headers.
// Returns a tightly packed allocation owned by the caller, or NULL on invalid
// dimensions/format/byte count/allocation failure. Never aliases compressed data.
GLvoid *uncompressDXTc(GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
    int transparent0, int *simpleAlpha, int *complexAlpha, const GLvoid *data);
void CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
    GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
#endif
