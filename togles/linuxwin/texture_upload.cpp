//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
//
//  Copyright 2011-2014 Valve Corporation
//  All Rights Reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
//
// cglmtex.cpp
//
//===============================================================================

#include "togles/rendermechanism.h"
#include "texture_upload.h"
extern "C" {
#include "decompress.h"
}

GLboolean isDXTc(GLenum format) {
    switch (format) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            return 1;
    }
    return 0;
}

GLboolean isDXTcSRGB(GLenum format) {
    switch (format) {
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            return 1;
    }
    return 0;
}

static GLboolean isDXTcAlpha(GLenum format) {
    switch (format) {
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            return 1;
    }
    return 0;
}

GLvoid *uncompressDXTc(GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
    int transparent0, int *simpleAlpha, int *complexAlpha, const GLvoid *data)
{
    if (width <= 0 || height <= 0 || imageSize < 0 || !data || !isDXTc(format) ||
        !simpleAlpha || !complexAlpha)
        return NULL;
    const size_t pixelSize = isDXTcAlpha(format) ? 4 : 3;
    const size_t blockSize = (format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
        format == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT ||
        format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
        format == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT) ? 8 : 16;
    const size_t blocksX = (size_t(width) + 3) / 4;
    const size_t blocksY = (size_t(height) + 3) / 4;
    if (blocksX > SIZE_MAX / blocksY / blockSize ||
        size_t(imageSize) != blocksX * blocksY * blockSize ||
        size_t(width) > SIZE_MAX / size_t(height) / pixelSize)
        return NULL;
    uint8_t *pixels = static_cast<uint8_t *>(malloc(size_t(width) * height * pixelSize));
    if (!pixels) return NULL;
    const uint8_t *source = static_cast<const uint8_t *>(data);
    for (size_t by = 0; by < blocksY; ++by) {
        for (size_t bx = 0; bx < blocksX; ++bx) {
            // Decode into a complete aligned block, then crop its edge texels.
            // This handles 1x1/2x2 mip tails and non-multiple-of-four dimensions.
            uint32_t decoded[16];
            uint32_t compressed[4];
            memcpy(compressed, source, blockSize);
            if (blockSize == 8)
                DecompressBlockDXT1(0, 0, 4, reinterpret_cast<uint8_t *>(compressed),
                    pixelSize == 4, simpleAlpha, complexAlpha, decoded);
            else if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || format == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT)
                DecompressBlockDXT3(0, 0, 4, reinterpret_cast<uint8_t *>(compressed),
                    transparent0, simpleAlpha, complexAlpha, decoded);
            else
                DecompressBlockDXT5(0, 0, 4, reinterpret_cast<uint8_t *>(compressed),
                    transparent0, simpleAlpha, complexAlpha, decoded);
            const size_t copyWidth = MIN(size_t(4), size_t(width) - bx * 4);
            const size_t copyHeight = MIN(size_t(4), size_t(height) - by * 4);
            for (size_t row = 0; row < copyHeight; ++row)
                memcpy(pixels + ((by * 4 + row) * width + bx * 4) * pixelSize,
                    reinterpret_cast<uint8_t *>(decoded) + row * 4 * pixelSize, copyWidth * pixelSize);
            source += blockSize;
        }
    }
    return pixels;
}

void CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const GLvoid *data)
{
    if (internalformat==GL_RGBA8)
        internalformat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;

	if ((width<=0) || (height<=0) || !isDXTc(internalformat)) {
        return;
    }

	bool hasAlpha = (internalformat != GL_COMPRESSED_RGB_S3TC_DXT1_EXT) && (internalformat != GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

    GLenum format = hasAlpha ? GL_RGBA : GL_RGB;
	GLenum intformat = hasAlpha ? GL_RGBA8 : GL_RGB8;
	GLenum type = GL_UNSIGNED_BYTE;
	GLvoid *pixels = NULL;

    if (isDXTc(internalformat))
    {
        int srgb = isDXTcSRGB(internalformat);
        int simpleAlpha = 0;
        int complexAlpha = 0;
        int transparent0 = (internalformat==GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || internalformat==GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT)?1:0;
        if (data) {
            pixels = uncompressDXTc(width, height, internalformat, imageSize, transparent0, &simpleAlpha, &complexAlpha, data);
            if (!pixels) {
                Warning("ToGLES: invalid DXT upload or allocation failure (%dx%d, %d bytes)\n", width, height, imageSize);
                return;
            }
        } else {
            if(isDXTcAlpha(internalformat)) {
                simpleAlpha = complexAlpha = 1;
            }
        }

		if( srgb )
			intformat = hasAlpha ? GL_SRGB8_ALPHA8 : GL_SRGB8;
	}

    GLint alignment, rowLength, skipPixels, skipRows, unpackBuffer;
    gGL->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
    gGL->glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    gGL->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    gGL->glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    gGL->glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
    gGL->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gGL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gGL->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gGL->glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    gGL->glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    gGL->glTexImage2D(target, level, intformat, width, height, border, format, type, pixels);
    gGL->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
    gGL->glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    gGL->glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    gGL->glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
    gGL->glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
	if( data != pixels )
		free(pixels);
}
