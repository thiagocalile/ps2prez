#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <tamtypes.h>
#include <debug.h>
#include <unistd.h>

//for debugging
#include <stdio.h>

#include "libpad.h"

#include "includes/presentation.h"

static char padBuf[256] __attribute__((aligned(64)));

static char actAlign[6];
static int actuators;


/*
 * Local functions
 */

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
    sceSifInitRpc(0);
    init_scr();

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

    for (;;) {      // We are phorever people

      bool command_updated = false;
      enum SlideCommand command = current_command(port, slot);

      if(command != NONE) command_updated = true;

      if(command_updated){
	scr_clear();
	if(command == PREV_SLIDE){
	  //scr_clear();
          scr_printf("PREV_SLIDE\n");
        }
        else if (command == NEXT_SLIDE){
	  //scr_clear();
	  scr_printf("NEXT_SLIDE\n");
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
  u32 old_pad = 0;
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
