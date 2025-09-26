/*
 MIT License

 Copyright (c) 2025 Thomas Foster

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#import <SDL3/SDL.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#define VER_MAJ 1
#define VER_MIN 0

#define MAX_LOOPS 255
#define MAX_CELS 255

const SDL_Color pal[16] = {
    { 0x00, 0x00, 0x00, 0xFF },
    { 0x00, 0x00, 0xAA, 0xFF },
    { 0x00, 0xAA, 0x00, 0xFF },
    { 0x00, 0xAA, 0xAA, 0xFF },
    { 0xAA, 0x00, 0x00, 0xFF },
    { 0xAA, 0x00, 0xAA, 0xFF },
    { 0xAA, 0x55, 0x00, 0xFF },
    { 0xAA, 0xAA, 0xAA, 0xFF },
    { 0x55, 0x55, 0x55, 0xFF },
    { 0x55, 0x55, 0xFF, 0xFF },
    { 0x55, 0xFF, 0x55, 0xFF },
    { 0x55, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0x55, 0x55, 0xFF },
    { 0xFF, 0x55, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0x55, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF }
};



typedef struct {
    Uint16 header_offset;
    Uint16 data_offset;
    Uint8 width;
    Uint8 height;
    Uint8 transparency_color;
    Uint8 is_mirrored;
    Uint8 unmirrored_loop_num;
} Cel;



typedef struct {
    Uint16 offset;
    Uint8 num_cels;
    Uint8 total_width;
    Uint8 total_height;
    Cel cels[MAX_CELS];
} Loop;



typedef struct {
    Loop loops[MAX_LOOPS];
    Uint8 num_loops;
} View;



/// Calculate the surface size needed to accommodate all loops and cells in a
/// View. Also updates the each loop's size.
SDL_Rect
GetSurfaceSize(View * view)
{
    SDL_Rect result = { 0 };

    for ( int i = 0; i < view->num_loops; i++ ) {
        Loop * loop = &view->loops[i];

        for ( int j = 0; j < loop->num_cels; j++ ) {
            Cel * cel = &loop->cels[j];

            loop->total_width += loop->cels[j].width;
            if ( cel->height > loop->total_height ) {
                loop->total_height = cel->height;
            }
        }

        if ( loop->total_width > result.w ) {
            result.w = loop->total_width;
        }

        result.h += loop->total_height;
    }

    result.w *= 2; // Accommodate double-wide pixels.

    return result;
}



SDL_Surface *
CreateSurface(View * view)
{
    SDL_Rect size = GetSurfaceSize(view);

    SDL_Surface * s = SDL_CreateSurface(size.w, size.h, SDL_PIXELFORMAT_RGBA32);
    if ( s == NULL ) {
        fprintf(stderr, "SDL_CreateSurface failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Clear the surface
    const SDL_PixelFormatDetails * details = SDL_GetPixelFormatDetails(s->format);
    Uint32 blank = SDL_MapRGBA(details, NULL, 0, 0, 0, 0);
    SDL_FillSurfaceRect(s, NULL, blank);

    return s;
}



void
ViewToBMP(const char * path)
{
    printf("Converting %s... ", path);
    FILE * file = fopen(path, "rb");
    if ( file == NULL ) {
        printf("Error: could not open view file '%s': %s\n", path, strerror(errno));
        return;
    }

    View view = { 0 };

    // Read the number of loops.
    fseek(file, 2, SEEK_SET);
    fread(&view.num_loops, sizeof(Uint8), 1, file);

    // Seek to start of loop offset list.
    fseek(file, 5, SEEK_SET);

    // Read all loop offsets.
    for ( int i = 0; i < view.num_loops; i++ ) {
        fread(&view.loops[i].offset, sizeof(Uint16), 1, file);
    }

    // Read all loops.
    for ( int i = 0; i < view.num_loops; i++ ) {
        Loop * loop = &view.loops[i];
        fseek(file, loop->offset, SEEK_SET);

        // Read the number of cels in this loop.
        fread(&loop->num_cels, sizeof(Uint8), 1, file);

        // Read the cel header offsets, storing them as absolute offsets.
        for ( int j = 0; j < loop->num_cels; j++ ) {
            Uint16 rel_offset;
            fread(&rel_offset, sizeof(Uint16), 1, file);
            loop->cels[j].header_offset = loop->offset + rel_offset;
        }

        // Read each cel header and store info.
        for ( int j = 0; j < loop->num_cels; j++ ) {
            Cel * cel = &loop->cels[j];
            fseek(file, loop->cels[j].header_offset, SEEK_SET);

            Uint8 info;
            fread(&cel->width, sizeof(Uint8), 1, file);
            fread(&cel->height, sizeof(Uint8), 1, file);
            fread(&info, sizeof(Uint8), 1, file);

            cel->is_mirrored = (info & 0x80) >> 7;
            cel->unmirrored_loop_num = (info & 0x70) >> 4;
            cel->transparency_color = (info & 0x0F);
            cel->data_offset = ftell(file);
        }
    }

    SDL_Surface * s = CreateSurface(&view);

    // Write each loop's cels horizontally from left to right, each loop in its
    // own row.
    int cel_y = 0;
    for ( int i = 0; i < view.num_loops; i++ ) {
        Loop * loop = &view.loops[i];

        int cel_x = 0;
        for ( int j = 0; j < loop->num_cels; j++ ) {
            Cel * cel = &loop->cels[j];
            fseek(file, cel->data_offset, SEEK_SET);

            for ( int y = cel_y; y < cel_y + cel->height; y++ ) {
                int x;
                int step;
                if ( cel->is_mirrored && cel->unmirrored_loop_num != i ) {
                    x = cel_x + (cel->width * 2) - 1;
                    step = -1;
                } else {
                    x = cel_x;
                    step = 1;
                }

                while ( 1 ) {
                    // Read image data.
                    Uint8 byte;
                    fread(&byte, sizeof(byte), 1, file);

                    if ( byte == 0 ) {
                        break; // End of this row.
                    }

                    Uint8 color = (byte >> 4) & 0x0F;
                    Uint8 count = byte & 0x0F;

                    // Decompress RLE data and write to surface.
                    while ( count-- ) {
                        Uint8 r = 0, g = 0, b = 0, a = 0;

                        if ( color != cel->transparency_color ) {
                            r = pal[color].r;
                            g = pal[color].g;
                            b = pal[color].b;
                            a = 255;
                        }

                        // Write doubled pixel.
                        for ( int k = 0; k < 2; k++ ) {
                            SDL_WriteSurfacePixel(s, x, y, r, g, b, a);
                            x += step;
                        }
                    }
                }
            }

            cel_x += cel->width * 2; // Accommodate double-wide pixels
        }

        cel_y += loop->total_height;
    }

    char name[256] = { 0 };
    snprintf(name, sizeof(name), "%s.bmp", path);
    SDL_SaveBMP(s, name);
    printf("saved %s\n", name);
    SDL_DestroySurface(s);

    fclose(file);
}



int
main(int argc, char ** argv)
{
    printf("agiview2bmp\n");
    printf("Convert Sierra Adventure Game Interpreter (AGI) "
           "View resources to bitmap\n");
    printf("Ver. %d.%d (C) Copyright 2025 Thomas Foster (github.com/teefoss)\n\n",
           VER_MAJ, VER_MIN);

    if ( argc < 2 ) {
        printf("usage: %s [view path(, view path, ...)]\n", argv[0]);
    }

    for ( int i = 1; i < argc; i++ ) {
        ViewToBMP(argv[i]);
    }

    return 0;
}
