/*
 * Copyright © 2014-2017 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef VC5_RESOURCE_H
#define VC5_RESOURCE_H

#include "v3d_screen.h"
#include "util/u_transfer.h"

/* A UIFblock is a 256-byte region of memory that's 256-byte aligned.  These
 * will be grouped in 4x4 blocks (left-to-right, then top-to-bottom) in a 4KB
 * page.  Those pages are then arranged left-to-right, top-to-bottom, to cover
 * an image.
 *
 * The inside of a UIFblock, for packed pixels, will be split into 4 64-byte
 * utiles.  Utiles may be 8x8 (8bpp), 8x4(16bpp) or 4x4 (32bpp).
 */

/**
 * Tiling mode enum used for v3d_resource.c, which maps directly to the Memory
 * Format field of render target and Z/Stencil config.
 */
enum v3d_tiling_mode {
        /* Untiled resources.  Not valid as texture inputs. */
        VC5_TILING_RASTER,

        /* Single line of u-tiles. */
        VC5_TILING_LINEARTILE,

        /* Departure from standard 4-UIF block column format. */
        VC5_TILING_UBLINEAR_1_COLUMN,

        /* Departure from standard 4-UIF block column format. */
        VC5_TILING_UBLINEAR_2_COLUMN,

        /* Normal tiling format: grouped in 4x4 UIFblocks, each of which is
         * split 2x2 into utiles.
         */
        VC5_TILING_UIF_NO_XOR,

        /* Normal tiling format: grouped in 4x4 UIFblocks, each of which is
         * split 2x2 into utiles.
         */
        VC5_TILING_UIF_XOR,
};

struct v3d_transfer {
        struct pipe_transfer base;
        void *map;
};

struct v3d_resource_slice {
        uint32_t offset;
        uint32_t stride;
        uint32_t padded_height;
        /* Size of a single pane of the slice.  For 3D textures, there will be
         * a number of panes equal to the minified, power-of-two-aligned
         * depth.
         */
        uint32_t size;
        uint8_t ub_pad;
        enum v3d_tiling_mode tiling;
};

struct v3d_surface {
        struct pipe_surface base;
        uint32_t offset;
        enum v3d_tiling_mode tiling;
        /**
         * Output image format for TILE_RENDERING_MODE_CONFIGURATION
         */
        uint8_t format;

        /**
         * Internal format of the tile buffer for
         * TILE_RENDERING_MODE_CONFIGURATION.
         */
        uint8_t internal_type;

        /**
         * internal bpp value (0=32bpp, 2=128bpp) for color buffers in
         * TILE_RENDERING_MODE_CONFIGURATION.
         */
        uint8_t internal_bpp;

        uint32_t padded_height_of_output_image_in_uif_blocks;

        /* If the resource being referenced is separate stencil, then this is
         * the surface to use when reading/writing stencil.
         */
        struct pipe_surface *separate_stencil;
};

struct v3d_resource {
        struct pipe_resource base;
        struct v3d_bo *bo;
        struct v3d_resource_slice slices[VC5_MAX_MIP_LEVELS];
        uint32_t cube_map_stride;
        uint32_t size;
        int cpp;
        bool tiled;

        /**
         * Number of times the resource has been written to.
         *
         * This is used to track whether we need to load the surface on first
         * rendering.
         */
        uint64_t writes;

        /**
         * Bitmask of PIPE_CLEAR_COLOR0, PIPE_CLEAR_DEPTH, PIPE_CLEAR_STENCIL
         * for which parts of the resource are defined.
         *
         * Used for avoiding fallback to quad clears for clearing just depth,
         * when the stencil contents have never been initialized.  Note that
         * we're lazy and fields not present in the buffer (DEPTH in a color
         * buffer) may get marked.
         */
        uint32_t initialized_buffers;

        enum pipe_format internal_format;

        /* Resource storing the S8 part of a Z32F_S8 resource, or NULL. */
        struct v3d_resource *separate_stencil;
};

static inline struct v3d_resource *
v3d_resource(struct pipe_resource *prsc)
{
        return (struct v3d_resource *)prsc;
}

static inline struct v3d_surface *
v3d_surface(struct pipe_surface *psurf)
{
        return (struct v3d_surface *)psurf;
}

static inline struct v3d_transfer *
v3d_transfer(struct pipe_transfer *ptrans)
{
        return (struct v3d_transfer *)ptrans;
}

void v3d_resource_screen_init(struct pipe_screen *pscreen);
void v3d_resource_context_init(struct pipe_context *pctx);
struct pipe_resource *v3d_resource_create(struct pipe_screen *pscreen,
                                          const struct pipe_resource *tmpl);
uint32_t v3d_layer_offset(struct pipe_resource *prsc, uint32_t level,
                          uint32_t layer);


#endif /* VC5_RESOURCE_H */
