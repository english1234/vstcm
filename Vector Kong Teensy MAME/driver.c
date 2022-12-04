#include "driver.h"

extern struct GameDriver ckongs_driver;

/* "Crazy Climber hardware" games */
extern struct GameDriver ckong_driver;
extern struct GameDriver ckonga_driver;
extern struct GameDriver ckongjeu_driver;
extern struct GameDriver ckongalc_driver;
extern struct GameDriver monkeyd_driver;

/* Nintendo games */
extern struct GameDriver dkong_driver;
extern struct GameDriver dkongjp_driver;
extern struct GameDriver dkongjr_driver;
extern struct GameDriver dkngjrjp_driver;
extern struct GameDriver dkjrjp_driver;
extern struct GameDriver dkjrbl_driver;
extern struct GameDriver dkong3_driver;

//#define CLASSIC 1

const struct GameDriver *drivers[] =
{
  /* Nintendo games */
  &dkong_driver,    /* (c) 1981 Nintendo of America */
  &dkongjp_driver,  /* (c) 1981 Nintendo */
  &dkongjr_driver,  /* (c) 1982 Nintendo of America */
  &dkngjrjp_driver, /* no copyright notice */
  &dkjrjp_driver,   /* (c) 1982 Nintendo */
  &dkjrbl_driver,   /* (c) 1982 Nintendo of America */
  &dkong3_driver,   /* (c) 1983 Nintendo of America */

	0	/* end of array */
};
