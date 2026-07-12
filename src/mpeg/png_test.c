#include <stdalign.h>
#include <stdbool.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>
#include <gs_gp.h>
#include <kernel.h>
#include <dma.h>
#include <draw.h>
#include <stdlib.h>
#include <string.h>
#include <graph.h>
#include <packet.h>
#include <malloc.h>
#include <tamtypes.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 512

alignas(64) const unsigned char slide_png[] = {
    #embed "slide.png"
};

int main() {
    packet_t *packet;
    qword_t *packet_cursor;

    framebuffer_t frame = {
        .width = IMAGE_WIDTH,
        .height = IMAGE_HEIGHT,
        .mask = 0,
        .psm = GS_PSM_32,    
    };

    frame.address = graph_vram_allocate(frame.width, frame.height, frame.psm, GRAPH_ALIGN_PAGE);

    zbuffer_t z = {
        .enable = 0,
        .mask = 0,
        .method = 0,
        .zsm = 0,
        .address = 0,
    };

    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);   
    dma_channel_fast_waits(DMA_CHANNEL_GIF); 

    graph_initialize(0, frame.width, frame.height, frame.psm, 0, 0);
    int tex_addr = graph_vram_allocate(0, 0, GS_PSM_32, GRAPH_ALIGN_BLOCK);

    packet = packet_init(100, PACKET_NORMAL);
    packet_cursor = packet->data;
    packet_cursor = draw_setup_environment(packet_cursor, 0, &frame, &z);
    
    // VISUAL DEBUG 1: Clear to RED (255, 0, 0)
    packet_cursor = draw_clear(packet_cursor, 0, 0, 0, 640.0f, 480.0f, 0, 0, 0);
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data, packet_cursor - packet->data, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    
    int width, height, channels;
    unsigned char *stb_data = stbi_load_from_memory(slide_png, sizeof(slide_png), &width, &height, &channels, 4);
    
    if (!stb_data) {
        // If STB failed, we freeze here. The screen will stay RED forever.
        for(;;); 
    }

    int total_bytes = width * height * 4;
    unsigned char *image_data = (unsigned char *)memalign(64, total_bytes);
    
    // MANUALLY FIX ALPHA: Translate PC Alpha to PS2 Alpha
    for (int i = 0; i < total_bytes; i += 4) {
        image_data[i]     = stb_data[i];     // R
        image_data[i + 1] = stb_data[i + 1]; // G
        image_data[i + 2] = stb_data[i + 2]; // B
        image_data[i + 3] = 0x80;            // A (0x80 is PS2 Opaque)
    }
    
    stbi_image_free(stb_data); 
    SyncDCache(image_data, image_data + total_bytes);

    // --- BUILD TRANSFER PACKET ---
    // Allocate enough space for a packet with multiple tags
    packet_t *xfer_pck = packet_init(200, PACKET_NORMAL);
    qword_t *q = xfer_pck->data;

    int lTBW = (width + 63) >> 6;

    // 1. Set VRAM destination bounds
    DMATAG_CNT(q, 3, 0, 0, 0); q++;
    PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TRXREG(width, height), GS_REG_TRXREG); q++;
    PACK_GIFTAG(q, GS_SET_BITBLTBUF(0, 0, 0, tex_addr >> 6, lTBW, GS_PSM_32), GS_REG_BITBLTBUF); q++;

    // 2. Safe Loop Chunking (32 lines = 5,120 QWC, well under the 32,767 limit)
    int lines_per_chunk = 32;
    
    for (int y = 0; y < height; y += lines_per_chunk) {
        int current_lines = (y + lines_per_chunk > height) ? (height - y) : lines_per_chunk;
        int current_qwc = (width * current_lines * 4) >> 4;

        DMATAG_CNT(q, 4, 0, 0, 0); q++;
        PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
        // Tell the GS exactly which vertical offset we are writing to
        PACK_GIFTAG(q, GS_SET_TRXPOS(0, 0, 0, y, 0), GS_REG_TRXPOS); q++;
        PACK_GIFTAG(q, GS_SET_TRXDIR(0), GS_REG_TRXDIR); q++;
        PACK_GIFTAG(q, GIF_SET_TAG(current_qwc, 1, 0, 0, 2, 0), 0); q++;
        
        // Point the DMA directly to the correct memory offset in our decoded RAM buffer
        unsigned int memory_offset = (unsigned int)(image_data + (y * width * 4));
        DMATAG_REF(q, current_qwc, memory_offset, 0, 0, 0); q++;
    }

    DMATAG_END(q, 0, 0, 0, 0); q++;
    xfer_pck->qwc = q - xfer_pck->data;
    
    int lTW = draw_log2(width);
    int lTH = draw_log2(height);

    packet_t *draw_pck = packet_init(20, PACKET_NORMAL);
    q = draw_pck->data;
    
    // VISUAL DEBUG 2: Clear background to GREEN (0, 255, 0) right before drawing
    q = draw_clear(q, 0, 0, 0, 640.0f, 480.0f, 0, 0, 0);

    PACK_GIFTAG(q, GIF_SET_TAG(6, 1, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TEX0(tex_addr >> 6, lTBW, GS_PSM_32, lTW, lTH, 1, 1, 0, 0, 0, 0, 0), GS_REG_TEX0_1); q++;
    PACK_GIFTAG(q, GS_SET_PRIM(6, 0, 1, 0, 0, 0, 1, 0, 0), GS_REG_PRIM); q++;
    PACK_GIFTAG(q, GS_SET_UV(0, 0), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 << 4), (2048 << 4), 0), GS_REG_XYZ2); q++;
    PACK_GIFTAG(q, GS_SET_UV(width << 4, height << 4), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((width << 4) + (2048 << 4), (height << 4) + (2048 << 4), 0), GS_REG_XYZ2); q++;
    draw_pck->qwc = q - draw_pck->data;

    dma_channel_send_chain(DMA_CHANNEL_GIF, xfer_pck->data, xfer_pck->qwc, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);

    for(;;) {
        graph_wait_vsync();
        graph_wait_vsync();

        dma_channel_send_normal(DMA_CHANNEL_GIF, draw_pck->data, draw_pck->qwc, 0, 0);
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
    }

    return 0;
}
