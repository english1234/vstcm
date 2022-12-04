#include "driver.h"
#include "myport.h"

char mameversion[] = "0.34 ("__DATE__
                     ")";

static struct RunningMachine machine;
struct RunningMachine *Machine = &machine;
static const struct GameDriver *gamedrv;
static const struct MachineDriver *drv;

int mame_debug; /* !0 when -debug option is specified */

int frameskip;
int VolumePTR = 0;
static int settingsloaded;

int bitmap_dirty; /* set by osd_clearbitmap() */

unsigned char *ROM;

FILE *errorlog;

void *record;   /* for -record */
void *playback; /* for -playback */

int init_machine(void);
void shutdown_machine(void);
int run_machine(void);

int run_game(int game, struct GameOptions *options) {
  int err;
emu_printf("run_game");
  errorlog = options->errorlog;
  record = options->record;
  playback = options->playback;
  mame_debug = options->mame_debug;

  Machine->gamedrv = gamedrv = drivers[game];
  Machine->drv = drv = gamedrv->drv;

  /* copy configuration */
  Machine->sample_rate = options->samplerate;
  Machine->sample_bits = options->samplebits;
  frameskip = options->frameskip;

  /* get orientation right */
  Machine->orientation = gamedrv->orientation;
  if (options->norotate)
    Machine->orientation = ORIENTATION_DEFAULT;
  if (options->ror) {
    /* if only one of the components is inverted, switch them */
    if ((Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_X || (Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_Y)
      Machine->orientation ^= ORIENTATION_ROTATE_180;

    Machine->orientation ^= ORIENTATION_ROTATE_90;
  }
  if (options->rol) {
    /* if only one of the components is inverted, switch them */
    if ((Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_X || (Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_Y)
      Machine->orientation ^= ORIENTATION_ROTATE_180;

    Machine->orientation ^= ORIENTATION_ROTATE_270;
  }
  if (options->flipx)
    Machine->orientation ^= ORIENTATION_FLIP_X;
  if (options->flipy)
    Machine->orientation ^= ORIENTATION_FLIP_Y;


  /* Do the work*/
  err = 1;

  if (init_machine() == 0) {
    if (osd_init() == 0) {
      if (run_machine() == 0)
        err = 0;
      else emu_printf("Unable to start machine emulation\n");

      osd_exit();
    } else emu_printf("Unable to initialize system\n");

    shutdown_machine();
  } else emu_printf("Unable to initialize machine emulation\n");

  return err;
}

/***************************************************************************

  Initialize the emulated machine (load the roms, initialize the various
  subsystems...). Returns 0 if successful.

***************************************************************************/
int init_machine(void) {
  if (gamedrv->input_ports) {
    int total;
    const struct InputPort *from;
    struct InputPort *to;

    from = gamedrv->input_ports;
    total = 0;
    do {
      total++;
    } while ((from++)->type != IPT_END);

    if ((Machine->input_ports = malloc(total * sizeof(struct InputPort))) == 0)
      return 1;

    from = gamedrv->input_ports;
    to = Machine->input_ports;

    do {
      memcpy(to, from, sizeof(struct InputPort));

      to++;
    } while ((from++)->type != IPT_END);
  }

  if (readroms() != 0) {
    free(Machine->input_ports);
    return 1;
  }

  {
    extern unsigned char *RAM;
    RAM = Machine->memory_region[drv->cpu[0].memory_region];
    ROM = RAM;
  }

  /* decrypt the ROMs if necessary */
  if (gamedrv->rom_decode) (*gamedrv->rom_decode)();

  if (gamedrv->opcode_decode) {
    int j;


    /* find the first available memory region pointer */
    j = 0;
    while (Machine->memory_region[j]) j++;

    /* allocate a ROM array of the same length of memory region #0 */
    if ((ROM = malloc(Machine->memory_region_length[0])) == 0) {
      free(Machine->input_ports);
      /* TODO: should also free the allocated memory regions */
      return 1;
    }

    Machine->memory_region[j] = ROM;
    Machine->memory_region_length[j] = Machine->memory_region_length[0];

    (*gamedrv->opcode_decode)();
  }


  /* read audio samples if available */
  Machine->samples = readsamples(gamedrv->samplenames, gamedrv->name);


  /* first of all initialize the memory handlers, which could be used by the */
  /* other initialization routines */
  cpu_init();

  /* load input ports settings (keys, dip switches, and so on) */
  settingsloaded = load_input_port_settings();

  /* ASG 971007 move from mame.c */
  if (!initmemoryhandlers()) {
    free(Machine->input_ports);
    return 1;
  }

  if (gamedrv->driver_init) (*gamedrv->driver_init)();

  return 0;
}

void shutdown_machine(void) {
  int i;

  /* free audio samples */
  freesamples(Machine->samples);
  Machine->samples = 0;

  /* ASG 971007 free memory element map */
  shutdownmemoryhandler();

  /* free the memory allocated for ROM and RAM */
  for (i = 0; i < MAX_MEMORY_REGIONS; i++) {
    free(Machine->memory_region[i]);
    Machine->memory_region[i] = 0;
    Machine->memory_region_length[i] = 0;
  }

  /* free the memory allocated for input ports definition */
  free(Machine->input_ports);
  Machine->input_ports = 0;
}

static void vh_close(void) {
  int i;


  for (i = 0; i < MAX_GFX_ELEMENTS; i++) {
    freegfx(Machine->gfx[i]);
    Machine->gfx[i] = 0;
  }
  freegfx(Machine->uifont);
  Machine->uifont = 0;
  osd_close_display();
  palette_stop();
}

static int vh_open(void) {
  int i;

  for (i = 0; i < MAX_GFX_ELEMENTS; i++) Machine->gfx[i] = 0;
  Machine->uifont = 0;

  if (palette_start() != 0) {
    vh_close();
    return 1;
  }

  /* convert the gfx ROMs into character sets. This is done BEFORE calling the driver's */
  /* convert_color_prom() routine (in palette_init()) because it might need to check the */
  /* Machine->gfx[] data */
  if (drv->gfxdecodeinfo) {
    for (i = 0; i < MAX_GFX_ELEMENTS && drv->gfxdecodeinfo[i].memory_region != -1; i++) {
      if ((Machine->gfx[i] = decodegfx(Machine->memory_region[drv->gfxdecodeinfo[i].memory_region]
                                         + drv->gfxdecodeinfo[i].start,
                                       drv->gfxdecodeinfo[i].gfxlayout))
          == 0) {
        vh_close();
        return 1;
      }
      Machine->gfx[i]->colortable = &Machine->colortable[drv->gfxdecodeinfo[i].color_codes_start];
      Machine->gfx[i]->total_colors = drv->gfxdecodeinfo[i].total_color_codes;
    }
  }

  /* create the display bitmap, and allocate the palette */
  if ((Machine->scrbitmap = osd_create_display(
         drv->screen_width, drv->screen_height,
         drv->video_attributes))
      == 0) {
    vh_close();
    return 1;
  }

  /* build our private user interface font */
  /* This must be done AFTER osd_create_display() so the function knows the */
  /* resolution we are running at and can pick a different font depending on it. */
  /* It must be done BEFORE palette_init() because that will also initialize */
  /* (through osd_allocate_colors()) the uifont colortable. */
  //if ((Machine->uifont = builduifont()) == 0)
  //{
  //	vh_close();
  //	return 1;
  //}

  /* initialize the palette - must be done after osd_create_display() */
  palette_init();

  return 0;
}


/***************************************************************************

  This function takes care of refreshing the screen, processing user input,
  and throttling the emulation speed to obtain the required frames per second.

***************************************************************************/

int need_to_clear_bitmap; /* set by the user interface */

int updatescreen(void) {
  // emu_printf("updatescreen");

  static int framecount = 0;
  int skipme = 1;


  /* see if we recomend skipping this frame */
  if (++framecount > frameskip) {
    framecount = 0;
    skipme = 0;
  }
  skipme = 0;  //osd_skip_this_frame(skipme);

  /* if not, go for it */
  if (!skipme) {
    //osd_profiler(OSD_PROFILE_VIDEO);
    if (need_to_clear_bitmap) {
      osd_clearbitmap(Machine->scrbitmap);
      need_to_clear_bitmap = 0;
    }

    // emu_printf("updatescreen: about to call vh_update");

    (*drv->vh_update)(Machine->scrbitmap, bitmap_dirty); /* update screen */
    bitmap_dirty = 0;
    //osd_profiler(OSD_PROFILE_END);
  }

  if (!skipme)
    osd_update_display();

  return 0;
}


/***************************************************************************

  Run the emulation. Start the various subsystems and the CPU emulation.
  Returns non zero in case of error.

***************************************************************************/
int run_machine(void) {
  int res = 1;


  if (vh_open() == 0) {
    if (drv->vh_start == 0 || (*drv->vh_start)() == 0) /* start the video hardware */
    {
      if (sound_start() == 0) /* start the audio hardware */
      {
        const struct RomModule *romp = gamedrv->rom;
        int region;

        /* free memory regions allocated with ROM_REGION_DISPOSE (typically gfx roms) */
        for (region = 0; romp->name || romp->offset || romp->length; region++) {
          if (romp->offset & ROMFLAG_DISPOSE) {
            free(Machine->memory_region[region]);
            Machine->memory_region[region] = 0;
          }
          do { romp++; } while (romp->length);
        }

        //if (settingsloaded == 0)
        //{
        //	/* if there is no saved config, it must be first time we run this game, */
        //	/* so show the disclaimer and driver credits. */
        //	showcopyright();
        //	showcredits();
        //}

        //if (showgamewarnings() == 0)  /* show info about incorrect behaviour (wrong colors etc.) */
        //{
        //	init_user_interface();

        cpu_run(); /* run the emulation! */

        /* save input ports settings */
        save_input_port_settings();
        //}

        /* the following MUST be done after hiscore_save() otherwise */
        /* some 68000 games will not work */
        sound_stop();
        if (drv->vh_stop) (*drv->vh_stop)();

        res = 0;
      } else printf("Unable to start audio emulation\n");
    } else printf("Unable to start video emulation\n");

    vh_close();
  } else printf("Unable to initialize display\n");

  return res;
}

int mame_highscore_enabled(void) {
  /* disable high score when record/playback is on */
  if (record != 0 || playback != 0) return 0;

  return 1;
}

/***********************************/
/* Add by JMH                      */
/***********************************/
int start_game(int game, struct GameOptions *options) {
  int err;

  errorlog = options->errorlog;
  record = options->record;
  playback = options->playback;
  mame_debug = options->mame_debug;

  Machine->gamedrv = gamedrv = drivers[game];
  Machine->drv = drv = gamedrv->drv;

  /* copy configuration */
  Machine->sample_rate = options->samplerate;
  Machine->sample_bits = options->samplebits;
  frameskip = options->frameskip;

  /* get orientation right */
  Machine->orientation = gamedrv->orientation;
  if (options->norotate)
    Machine->orientation = ORIENTATION_DEFAULT;
  if (options->ror) {
    /* if only one of the components is inverted, switch them */
    if ((Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_X || (Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_Y)
      Machine->orientation ^= ORIENTATION_ROTATE_180;

    Machine->orientation ^= ORIENTATION_ROTATE_90;
  }
  if (options->rol) {
    /* if only one of the components is inverted, switch them */
    if ((Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_X || (Machine->orientation & ORIENTATION_ROTATE_180) == ORIENTATION_FLIP_Y)
      Machine->orientation ^= ORIENTATION_ROTATE_180;

    Machine->orientation ^= ORIENTATION_ROTATE_270;
  }
  if (options->flipx)
    Machine->orientation ^= ORIENTATION_FLIP_X;
  if (options->flipy)
    Machine->orientation ^= ORIENTATION_FLIP_Y;

  /* Do the work*/
  err = 1;
  if (init_machine() == 0) {
    if (osd_init() == 0) {
      if (vh_open() == 0) {
        if (drv->vh_start == 0 || (*drv->vh_start)() == 0) /* start the video hardware */
        {
          if (sound_start() == 0) /* start the audio hardware */
          {
            /* free the graphics ROMs, they are no longer needed */
            /* TODO: instead of hardcoding region 1, use a flag to mark regions */
            /*       which can be freed after initialization. */
            free(Machine->memory_region[1]);
            Machine->memory_region[1] = 0;

            if (settingsloaded == 0) {
            }

            cpu_start(); /* run the emulation! */
                         //            cpu_run();      /* run the emulation! */

            err = 0;
          } else printf("Unable to start audio emulation\n");
        } else printf("Unable to start video emulation\n");

      } else printf("Unable to initialize display\n");
    } else printf("Unable to initialize system\n");

  } else printf("Unable to initialize machine emulation\n");

  return err;
}

int step_game() {
  cpu_step();
}

int stop_game() {
  cpu_stop();

  /* save input ports settings */
  save_input_port_settings();

  /* the following MUST be done after hiscore_save() otherwise */
  /* some 68000 games will not work */
  sound_stop();
  if (drv->vh_stop) (*drv->vh_stop)();
  vh_close();

  //  stop_machine();
  osd_exit();
  shutdown_machine();
  return 0;
}