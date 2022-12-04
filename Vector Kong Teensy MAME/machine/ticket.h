/***************************************************************************

	Function prototypes and constants for the ticket dispenser emulator

***************************************************************************/


#define TICKET_MOTOR_ACTIVE_LOW    0    /* Ticket motor is triggered by D7=0 */
#define TICKET_MOTOR_ACTIVE_HIGH   1    /* Ticket motor is triggered by D7=1 */

#define TICKET_STATUS_ACTIVE_LOW   0    /* Ticket is done dispensing when D7=0 */
#define TICKET_STATUS_ACTIVE_HIGH  1    /* Ticket is done dispensing when D7=1 */

/***************************************************************************
  ticket_dispenser_init

  msec       = how many milliseconds it takes to dispense a ticket
  activehigh = see constants above

***************************************************************************/
void ticket_dispenser_init(int msec, int motoractivehigh, int statusactivehigh);


/***************************************************************************
  ticket_dispenser_r
***************************************************************************/
int ticket_dispenser_r(int offset);


/***************************************************************************
  ticket_dispenser_w
***************************************************************************/
void ticket_dispenser_w(int offset, int data);

