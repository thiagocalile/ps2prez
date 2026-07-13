#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <tamtypes.h>
//#include <debug.h>
#include <unistd.h>

// Stuff needed for showing the slides
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

//for debugging
#include <stdio.h>

#include "libpad.h"

#include "includes/presentation.h"
#include "includes/slides.h"

#define STB_IMAGE_IMPLEMENTATION
#include "includes/stb_image.h"

#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 512

static char padBuf[256] __attribute__((aligned(64)));

static char actAlign[6];
static int actuators;


/*
 * Local functions
 */

// This one was done by Gemini
void show_slide(const Slide *slide, int tex_addr);

/*
 * loadModules()
 */
static void
loadModules(void)
{
    int ret;
    
    ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    if (ret < 0) SleepThread();

    ret = SifLoadModule("rom0:PADMAN", 0, NULL);
    if (ret < 0) SleepThread();
}

/*
 * waitPadReady()
 */
static int waitPadReady(int port, int slot)
{
    int state;
    int lastState;
    char stateString[16];

    state = padGetState(port, slot);
    lastState = -1;
    while((state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1)) {
        if (state != lastState) padStateInt2String(state, stateString);
        lastState = state;
        state=padGetState(port, slot);
    }

    return 0;
}


/*
 * initializePad()
 */
static int
initializePad(int port, int slot)
{

    int ret;
    int modes;
    int i;

    waitPadReady(port, slot);

    // How many different modes can this device operate in?
    // i.e. get # entrys in the modetable
    modes = padInfoMode(port, slot, PAD_MODETABLE, -1);

    if (modes > 0) {

        for (i = 0; i < modes; i++) {
	  padInfoMode(port, slot, PAD_MODETABLE, i);
        }

    }

    padInfoMode(port, slot, PAD_MODECURID, 0);

    // If modes == 0, this is not a Dual shock controller
    // (it has no actuator engines)
    if (modes == 0) return 1;
    
    // Verify that the controller has a DUAL SHOCK mode

    i = 0;

    do {
        if (padInfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK)
            break;
        i++;
    } while (i < modes);
    
    if (i >= modes)  return 1;

    // If ExId != 0x0 => This controller has actuator engines
    // This check should always pass if the Dual Shock test above passed
    ret = padInfoMode(port, slot, PAD_MODECUREXID, 0);

    if (ret == 0) return 1;

    // When using MMODE_LOCK, user cant change mode with Select button
    padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

    waitPadReady(port, slot);
    padInfoPressMode(port, slot);

    waitPadReady(port, slot);
    padEnterPressMode(port, slot);

    waitPadReady(port, slot);
    actuators = padInfoAct(port, slot, -1, 0);
    
    if (actuators != 0) {
        actAlign[0] = 0;   // Enable small engine
        actAlign[1] = 1;   // Enable big engine
        actAlign[2] = 0xff;
        actAlign[3] = 0xff;
        actAlign[4] = 0xff;
        actAlign[5] = 0xff;

        waitPadReady(port, slot);
        padSetActAlign(port, slot, actAlign);
    };

    waitPadReady(port, slot);

    return 1;
}

enum SlideCommand current_command(int port, int slot);

int
main()
{
    int ret;
    int port, slot;

    //for debugging
    //sceSifInitRpc(0);
    //init_scr();

    // ------------------ AI preamble ----------------------------------------------
    int total_slides = sizeof(presentation) / sizeof(Slide);
    int current_slide_index = 0;

    // Initialize the GS hardware and drawing environment (Standard Setup)
    framebuffer_t frame = { .width = IMAGE_WIDTH, .height = IMAGE_HEIGHT, .mask = 0, .psm = GS_PSM_32 };
    frame.address = graph_vram_allocate(frame.width, frame.height, frame.psm, GRAPH_ALIGN_PAGE);
    
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);   
    dma_channel_fast_waits(DMA_CHANNEL_GIF); 
    graph_initialize(0, frame.width, frame.height, frame.psm, 0, 0);

    // Allocate the VRAM address ONCE. Every slide will overwrite this block.
    int tex_addr = graph_vram_allocate(0, 0, GS_PSM_32, GRAPH_ALIGN_BLOCK);

    // --- MISSING SETUP FIX ---
    zbuffer_t z = { .enable = 0, .mask = 0, .method = 0, .zsm = 0, .address = 0 };
    packet_t *env_packet = packet_init(20, PACKET_NORMAL);
    qword_t *env_q = env_packet->data;
    
    env_q = draw_setup_environment(env_q, 0, &frame, &z);
    env_q = draw_clear(env_q, 0, 0, 0, frame.width, frame.height, 0, 0, 0);
    
    dma_channel_send_normal(DMA_CHANNEL_GIF, env_packet->data, env_q - env_packet->data, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet_free(env_packet);
    // -------------------------
    
    // ------------------ end of AI preamble ---------------------------------------
    
    loadModules();

    padInit(0);

    port = 0; // 0 -> Connector 1, 1 -> Connector 2
    slot = 0; // Always zero if not using multitap
    padGetPortMax();
    padGetSlotMax(port);

    if((ret = padPortOpen(port, slot, padBuf)) == 0) {
        SleepThread();
    }

    if(!initializePad(port, slot)) {
        SleepThread();
    }

    show_slide(&presentation[current_slide_index], tex_addr);
    
    for (;;) {      // We are phorever people

      bool command_updated = false;
      enum SlideCommand command = current_command(port, slot);

      if(command != NONE) command_updated = true;

      if(command_updated){
	//scr_clear();
	if(command == PREV_SLIDE){
	  //scr_clear();
          //scr_printf("PREV_SLIDE\n");
	  if(current_slide_index <= 0){
	    current_slide_index = 0;
	    show_slide(&presentation[current_slide_index], tex_addr);
	  } else {
	    current_slide_index--;
	    show_slide(&presentation[current_slide_index], tex_addr);
	  }
        }
        else if (command == NEXT_SLIDE){
	  //scr_clear();
	  //scr_printf("NEXT_SLIDE\n");
	  if(current_slide_index >= total_slides - 1){
	    current_slide_index = total_slides - 1;
	    show_slide(&presentation[current_slide_index], tex_addr);
	  } else {
	    current_slide_index++;
	    show_slide(&presentation[current_slide_index], tex_addr);
	  }
        }
	command_updated = false;
      }
      
    } // for

    //scr_clear(); scr_printf("Goto sleep!\n");
    SleepThread();

    return 0;
}

enum SlideCommand current_command(int port, int slot){
  
  int ret;
  struct padButtonStatus buttons;
  u32 paddata;
  static u32 old_pad = 0;
  u32 new_pad;
  
        ret = padGetState(port, slot);
	
        while((ret != PAD_STATE_STABLE) && (ret != PAD_STATE_FINDCTP1)) {
          ret = padGetState(port, slot);
        }

        ret = padRead(port, slot, &buttons); // port, slot, buttons

        if (ret != 0) {
            paddata = 0xffff ^ buttons.btns;

            new_pad = paddata & ~old_pad;
            old_pad = paddata;

            // Directions
            if(new_pad & PAD_LEFT) {
	      //scr_clear(); scr_printf("LEFT\n");
	      return PREV_SLIDE;
            }
            if(new_pad & PAD_DOWN) {
	      //scr_clear(); scr_printf("DOWN\n");
	      return NEXT_SLIDE;
            }
            if(new_pad & PAD_RIGHT) {
	      //scr_clear(); scr_printf("RIGHT\n");
                /*
                       padSetMainMode(port, slot,
                                      PAD_MMODE_DIGITAL, PAD_MMODE_LOCK));
                */
	      return NEXT_SLIDE;
            }
            if(new_pad & PAD_UP) {
	      //scr_clear(); scr_printf("UP\n");
	      return PREV_SLIDE;
            }
            if(new_pad & PAD_START) {
	      //scr_clear(); scr_printf("START\n");
	      return NONE;
            }
            if(new_pad & PAD_R3) {
	      //scr_clear(); scr_printf("R3\n");
	      return NONE;
            }
            if(new_pad & PAD_L3) {
	      //scr_clear(); scr_printf("L3\n");
	      return NONE;
            }
            if(new_pad & PAD_SELECT) {
	      //scr_clear(); scr_printf("SELECT\n");
	      return NONE;
            }
            if(new_pad & PAD_SQUARE) {
	      //scr_clear(); scr_printf("SQUARE\n");
	      return PREV_SLIDE;
            }
            if(new_pad & PAD_CROSS) {
	      //padEnterPressMode(port, slot);
	      //scr_clear(); scr_printf("CROSS - Enter press mode\n");
	      return NEXT_SLIDE;
            }
            if(new_pad & PAD_CIRCLE) {
	      //padExitPressMode(port, slot);
	      //scr_clear(); scr_printf("CIRCLE - Exit press mode\n");
	      return NEXT_SLIDE;
            }
            if(new_pad & PAD_TRIANGLE) {
                // Check for the reason below..
                //scr_clear(); scr_printf("TRIANGLE (press mode disabled, see code)\n");
	      return PREV_SLIDE;
            }
            if(new_pad & PAD_R1) {
	      //actAlign[0] = 1; // Start small engine
	      //padSetActDirect(port, slot, actAlign);
                //scr_clear(); scr_printf("R1 - Start little engine\n");
	      return NEXT_SLIDE;
            }
            if(new_pad & PAD_L1) {
	      // actAlign[0] = 0; // Stop engine 0
	      //padSetActDirect(port, slot, actAlign);
                //scr_clear(); scr_printf("L1 - Stop little engine\n");
	      return PREV_SLIDE;
            }
            if(new_pad & PAD_R2) {
	      //scr_clear(); scr_printf("R2\n");
	      return NEXT_SLIDE;
            }
            if(new_pad & PAD_L2) {
	      //scr_clear(); scr_printf("L2\n");
	      return PREV_SLIDE;
            }
            /*
            // Test the press mode
            if(buttons.triangle_p) {
	      //scr_clear(); scr_printf("TRIANGLE %d\n", buttons.triangle_p);
            }
            // Start little engine if we move right analogue stick right
            if(buttons.rjoy_h > 0xf0)
            {
                // Stupid check to see if engine is already running,
                // just to prevent overloading the IOP with requests
                if (actAlign[0] == 0) {
                    actAlign[0] = 1;
                    padSetActDirect(port, slot, actAlign);
                }
            }
	    */
        }
	return NONE;
  
};

void show_slide(const Slide *slide, int tex_addr) {
    int width, height, channels;
    
    // 1. Decode PNG from memory
    unsigned char *stb_data = stbi_load_from_memory(slide->payload, slide->size, &width, &height, &channels, 4);
    if (!stb_data) {
        printf("Failed to decode slide.\n");
        return; 
    }

    // 2. Align memory and correct the Alpha channel for the GS
    int total_bytes = width * height * 4;
    unsigned char *image_data = (unsigned char *)memalign(64, total_bytes);
    
    for (int i = 0; i < total_bytes; i += 4) {
        image_data[i]     = stb_data[i];
        image_data[i + 1] = stb_data[i + 1];
        image_data[i + 2] = stb_data[i + 2];
        image_data[i + 3] = 0x80; // Force PS2 opaque
    }
    
    stbi_image_free(stb_data); 
    SyncDCache(image_data, image_data + total_bytes);

    // 3. Build the VRAM Transfer Packet (Chunked to bypass 15-bit limit)
    packet_t *xfer_pck = packet_init(200, PACKET_NORMAL);
    qword_t *q = xfer_pck->data;
    int lTBW = (width + 63) >> 6;

    DMATAG_CNT(q, 3, 0, 0, 0); q++;
    PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TRXREG(width, height), GS_REG_TRXREG); q++;
    PACK_GIFTAG(q, GS_SET_BITBLTBUF(0, 0, 0, tex_addr >> 6, lTBW, GS_PSM_32), GS_REG_BITBLTBUF); q++;

    int lines_per_chunk = 32;
    for (int y = 0; y < height; y += lines_per_chunk) {
        int current_lines = (y + lines_per_chunk > height) ? (height - y) : lines_per_chunk;
        int current_qwc = (width * current_lines * 4) >> 4;

        DMATAG_CNT(q, 4, 0, 0, 0); q++;
        PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
        PACK_GIFTAG(q, GS_SET_TRXPOS(0, 0, 0, y, 0), GS_REG_TRXPOS); q++;
        PACK_GIFTAG(q, GS_SET_TRXDIR(0), GS_REG_TRXDIR); q++;
        PACK_GIFTAG(q, GIF_SET_TAG(current_qwc, 1, 0, 0, 2, 0), 0); q++;
        
        unsigned int memory_offset = (unsigned int)(image_data + (y * width * 4));
        DMATAG_REF(q, current_qwc, memory_offset, 0, 0, 0); q++;
    }

    DMATAG_END(q, 0, 0, 0, 0); q++;
    xfer_pck->qwc = q - xfer_pck->data;

    // 4. Build the Drawing Packet
    packet_t *draw_pck = packet_init(10, PACKET_NORMAL);
    q = draw_pck->data;
    
    int lTW = draw_log2(width);
    int lTH = draw_log2(height);

    PACK_GIFTAG(q, GIF_SET_TAG(6, 1, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TEX0(tex_addr >> 6, lTBW, GS_PSM_32, lTW, lTH, 1, 1, 0, 0, 0, 0, 0), GS_REG_TEX0_1); q++;
    PACK_GIFTAG(q, GS_SET_PRIM(6, 0, 1, 0, 0, 0, 1, 0, 0), GS_REG_PRIM); q++;
    PACK_GIFTAG(q, GS_SET_UV(0, 0), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 << 4), (2048 << 4), 0), GS_REG_XYZ2); q++;
    PACK_GIFTAG(q, GS_SET_UV(width << 4, height << 4), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((width << 4) + (2048 << 4), (height << 4) + (2048 << 4), 0), GS_REG_XYZ2); q++;
    draw_pck->qwc = q - draw_pck->data;

    // 5. Execute DMA Transfers
    dma_channel_send_chain(DMA_CHANNEL_GIF, xfer_pck->data, xfer_pck->qwc, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0); // Wait for VRAM upload to finish

    graph_wait_vsync(); // Sync drawing to the vertical blanking interval to avoid tearing

    dma_channel_send_normal(DMA_CHANNEL_GIF, draw_pck->data, draw_pck->qwc, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0); // Wait for drawing to finish

    // 6. Free all allocated memory to prevent heap fragmentation across 50 slides
    free(image_data);
    packet_free(xfer_pck);
    packet_free(draw_pck);
}
