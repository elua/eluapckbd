/****************************************************************************/
/*  pc_keyboard is a ps/2 keyboard driver for eLua ( www.eluaproject.net )  */
/*                                                                          */
/*         v0.2, June 2010, by Thiago Naves, LED Lab, PUC-Rio               */
/*                                                                          */
/****************************************************************************/

/* Functions */

/*
   - keyboard_init( Clock, Data, Clock PullDown, Data PullDown )
   - keyboard_setflags( Start, Stop, Parity )
   - keyboard_receive()
   - keyboard_send( Data )
   - keyboard_setleds( Num Lock, Caps Lock, Scroll Lock )
   - keyboard_disablekeyevents( Key code, Break, Typematic repeat )
   - keyboard_configkeys( Key code, Break, Typematic repeat )
   - keyboard_setRepeatRateAndDelay( rate, delay )
   - keyboard_setScanCodeSet( Code set )
   - keyboard_enable()
   - keyboard_disable()
   - keyboard_reset()
   - keyboard_default()
   - keyboard_resend()
   - keyboard_echo()
*/

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "auxmods.h"
#include "lrotable.h"
#include "platform_conf.h"
#include <string.h>

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"

#define DIR_OUT 0
#define DIR_IN 1

#define IGNORE 1
#define USE 0

#define ERROR 0

#define ACK 0xFA
#define SETLEDS 0xED
#define ECHO 0xEE
#define SET_TYPEMATIC_RD 0xF3
#define SET_SCAN_CODE_SET 0xF0
#define ENABLE 0xF4
#define DISABLE 0xF5
#define DEFAULT 0xF6
#define RESET 0xFF
#define RESEND 0xFE

typedef struct sPin
{
  pio_type pin, port;
} tPin;

/* Pin Configuration */
tPin P_CLK, P_DATA, P_CLK_PD, P_DATA_PD;

/* Start, Stop and Parity bits ignore configuration */
char igStart = USE;
char igStop = USE;
char igParity = USE;

/* Returns the absolut value of a number */
static unsigned int abs( int i )
{
  if ( i < 0 )
    return -i;
  else
    return i;
}

/* Converts a pin number got from the Lua stack to the tPin format */
static tPin convertPin( int p )
{
  tPin result;
  result.port = PLATFORM_IO_GET_PORT( p );
  result.pin = ( 1 << PLATFORM_IO_GET_PIN( p ) );

  return result;
}

/* Just to make the code easier to read
 * Sets a pin value ( 1 / 0 ) 
 */
static void setPinVal( tPin p, char val )
{
  if ( val )
    platform_pio_op( p.port, p.pin, PLATFORM_IO_PIN_SET );
  else
    platform_pio_op( p.port, p.pin, PLATFORM_IO_PIN_CLEAR );
}

/* Just to make the code easier to read
 * Sets a pin direction ( DIR_IN / DIR_OU )
 */
static void setPinDir( tPin p, char dir )
{
  if ( dir == DIR_IN )
    platform_pio_op( p.port, p.pin, PLATFORM_IO_PIN_DIR_INPUT );
  else
    platform_pio_op( p.port, p.pin, PLATFORM_IO_PIN_DIR_OUTPUT );
}

/* Just to make the code easier to read
 * Returns the current pin value 
 */
static int getPinVal( tPin p )
{
  return platform_pio_op( p.port, p.pin, PLATFORM_IO_PIN_GET );
}

/* Generates a Parity bit for a given char ( to send data ) */
static char genCRC( unsigned char data )
{
  int i, count;

  count = 0;

  /* Count the 1s */
  for ( i=0; i<8; i++ )
  {
    if ( ( data & 1 ) == 1 )
      count ++;

    data = data >> 1;
  }

  /* The parity bit is set if there is an even number of 1s  */
  return ( ( count & 1 ) != 1 );
}

/* Checks the Parity bit for a given package */
static char checkCRC( unsigned int data )
{
  /* Checks the parity bit ( odd parity ) */
  unsigned int tmp;
  int i, count;

  count = 0;
  tmp = data;

  /* Extract data bits */
  data = data >> 1;
  data = data & 255;

  /* Count the 1s */
  for ( i=0; i<8; i++ )
  {
    if ( ( data & 1 ) == 1 )
      count ++;

    data = data >> 1;
  }

  /* The parity bit is set if there is an even number of 1s  */
  return ( ( count & 1 ) != ( tmp & 512 ) );
}

/* Set IGNORE flags for Start, Stop and/or Parity bits
 * This is due to buggy keyboards
 * Lua: keyboard.setflags( Start, Stop, Parity )
 */
static int keyboard_setflags( lua_State *L )
{ 
  /* Start, Stop, Parity */
  /* Set ignore bits flags */
  igStart = luaL_checkinteger( L, 1 );
  igStop = luaL_checkinteger( L, 2 );
  igParity = luaL_checkinteger( L, 3 );

  return 0;
}

/* Initializes pin directions and default values
 * Lua: keyboard.init( Clock, Data, Clock PullDown, Data PullDown ) 
 */
static int keyboard_init( lua_State *L )
{
  P_CLK = convertPin( luaL_checkinteger( L, 1 ) );
  P_DATA = convertPin( luaL_checkinteger( L, 2 ) );
  P_CLK_PD = convertPin( luaL_checkinteger( L, 3 ) );
  P_DATA_PD = convertPin( luaL_checkinteger( L, 4 ) );

  setPinDir( P_CLK_PD, DIR_OUT );
  setPinDir( P_DATA_PD, DIR_OUT );
  setPinDir( P_DATA, DIR_IN );
  setPinDir( P_CLK, DIR_IN );

  setPinVal( P_DATA_PD, 1 );
  setPinVal( P_CLK_PD, 1 );

  return 0;
}

/* Receives a char from the keyboard */
static char keyboard_getchar( )
{
  unsigned int data = 0;

  int i;
  for ( i=1; i<12; i++ )
  {
    while ( getPinVal( P_CLK ) != 1 ) /* Wait for next clock */
    {}

    if ( i < 11 )
      while ( getPinVal( P_CLK ) ) /* Wait for clock to go low */
      {}

    data = data >> 1;

    if ( getPinVal( P_DATA ) == 1 )
      data = data | ( 1 << 10 );
  }

  /* Check start bit */
  if ( ( ( data & 1 ) == 1 ) && ( igStart == USE ) )
    return ERROR;

  /* Check stop bit */
  if ( ( ( data & 1024 ) == 0 ) && ( igStop == USE ) )
    return ERROR;

  /* Check CRC bit */
  if ( ( checkCRC( data ) == 0 ) && ( igParity == USE ) )
    return ERROR;

  /* Remove Start, Stop and CRC bits */
  data = data >> 1;
  data = data & 255;

  return data;
}

/* Wrapper ( bind ) for keyboard_getchar function
 * Lua: keyboard.receive() 
 */
static int keyboard_receive( lua_State *L )
{
  lua_pushinteger( L, keyboard_getchar () );
  return 1;
}

/*
static int keyboard_read( lua_State *L )
{
  char c;
  int qtd = 0;

  while ( 1 )
  {
    c = keyboard_getchar();

    if ( c == 28 )
      lua_pushchar( "a" )
      printf( "a" );
    else
      if ( c == 50 )
         printf( "b" );
  }

  printf( "\n" );
  return 0;
}
*/

/* Sends data to the keyboard */
static void keyboard_write( char data )
{
  char bit; /* Counter */
  char par = genCRC( data ); /* Parity Bit */

  
  /* Disable communication & Request Send */
  setPinVal( P_CLK_PD, 0 );
  platform_timer_delay( 1, 120 ); /* 120 microseconds */
  setPinVal( P_DATA_PD, 0 );
  setPinVal( P_CLK_PD, 1 );

  /* Wait for clock and send data */
  for ( bit=1; bit<= 11; bit++ )
  {
    while ( getPinVal( P_CLK ) == 1 )
    {}

    if ( bit == 9 ) /* Parity Bit */
      setPinVal( P_DATA_PD, par );
    else
      if ( bit == 10 ) /* Stop Bit */
        setPinVal( P_DATA_PD, 1 );
      else
        if ( bit != 11 ) /* Data Bit */
          setPinVal( P_DATA_PD, data & ( 1 << ( bit -1 ) ) );

    /* bit == 11 -> ACK bit, just ignore it */

    while ( getPinVal( P_CLK ) == 0 )
    {}
  }
}

/* Bind to keyboard_write function
 * Sends a byte to the Keyboard
 * Lua: keyboard.send( Data byte ) 
 */
static int keyboard_send( lua_State *L )
{
  int i;
  i = luaL_checkinteger( L, 1 );
  keyboard_write( i );
  return 0;
}

/* Sets the Num Lock, Caps Lock and Scroll Lock Leds state
 * Lua: keyboard.setleds( Num Lock, Caps Lock, Scroll Lock )
 */
static int keyboard_setleds( lua_State *L )
{
  int i = 0;
  i = i | ( luaL_checkinteger( L, 1 ) << 1 ); /* Num Lock */
  i = i | ( luaL_checkinteger( L, 2 ) << 2 ); /* Caps Lock */
  i = i | ( luaL_checkinteger( L, 3 ) ); /* Scroll Lock */

  keyboard_write( SETLEDS );
  keyboard_write( i );

  return 0;
}

/* Configure wich key events the keyboard will send for a given key
 * Params: Key code, Break, Typematic repeat
 * If param == USE then enable that message
 * If param == IGNORE then ignore that message
 * Lua: keyboard.disablekeyevents( Key, Break, Typematic )
 */
static int keyboard_disablekeyevents( lua_State *L )
{
  #define makeOnly 0xFD
  #define makeBreak 0xFC
  #define makeType 0xFB
  int bk, tp, i;
  char ret;
  const char * buf;
  size_t len;

  /* Get params */
  luaL_checktype( L, 1, LUA_TSTRING );
  buf = lua_tolstring( L, 1, &len );
  bk  = luaL_checkinteger( L, 2 );
  tp  = luaL_checkinteger( L, 3 );

  /* Send operation code */
  if ( ( bk == IGNORE ) && ( tp == IGNORE ) ) 
  {
    printf( "make only\n" );
    keyboard_write( makeOnly );
  }

  if ( ( bk == IGNORE ) && ( tp == USE ) )
  {
    printf( "make type" );
    keyboard_write( makeType );
  }

  if ( ( tp == IGNORE ) && ( bk == USE ) )
  {
    printf( "make break" );
    keyboard_write( makeBreak );
  }

  /* Wait for ACK */
  ret = keyboard_getchar();

  if ( ret != ACK )
  {
    printf( "Error: No ACK ! " );
    return 0;
  }

  /* send key ( make ) code */
  for ( i=0; i<len; i++ )
  {
    /* Send key code */
    keyboard_write( buf[i] );

    /* Wait for ACK */
    ret = keyboard_getchar();

    if ( ret != ACK )
    {
      printf( "Error: No ACK ! " );
      return 0;
    }
  }

  /* Send echo ( ends key list ) */
  keyboard_write( ECHO );

  return 0;
}

/* Configure wich key events the keyboard will send for all keys
 * If param == USE then enable that message
 * If param == IGNORE then ignore that message
 * Params: Break, Typematic repeat
 * Lua: keyboard.configkeys( Break, Typematic )
 */
static int keyboard_configkeys( lua_State *L )
{
  #define amakeOnly 0xF9
  #define amakeBreak 0xF8
  #define amakeType 0xF7
  #define amakeBreakType 0xFA
  int bk, tp;
  char ret;

  /* Get params */
  bk  = luaL_checkinteger( L, 1 );
  tp  = luaL_checkinteger( L, 2 );

  /* Enable all */
  printf( "make break" );
  keyboard_write( amakeBreakType );

  /* Wait for ACK */
  ret = keyboard_getchar();

  if ( ret != ACK )
  {
    printf( "Error: No ACK ! " );
    return 0;
  }

  /* Disable something ( or not ) */

  if ( ( bk == IGNORE ) && ( tp == IGNORE ) ) 
  {
    printf( "make only\n" );
    keyboard_write( amakeOnly );
  }

  if ( ( bk == IGNORE ) && ( tp == USE ) )
  {
    printf( "make type" );
    keyboard_write( amakeType );
  }

  if ( ( tp == IGNORE ) && ( bk == USE ) )
  {
    printf( "make break" );
    keyboard_write( amakeBreak );
  }

  /* Wait for ACK */
  ret = keyboard_getchar();

  if ( ret != ACK )
  {
    printf( "Error: No ACK ! " );
    return 0;
  }

  /* Send echo ( ends key list ) */
  keyboard_write( ECHO );

  /* Wait for Echo reply */
  keyboard_getchar();

  return 0;
}

/* Defines the typematic delay and character repeat rate
 *
 * Typematic delay is the delay before repeating a character
 * when a key is hold down.
 * 
 * Repeat rate tells how many characters per second will be
 * sent after that delay 
 *
 * Lua: keyboard.setrepeatrateanddelay( rate, delay )
 */
static int keyboard_setRepeatRateAndDelay( lua_State *L )
{
  int rates[32] = { 300, 267, 240, 218, 207, 185, 171, 160, 150, 133, 120,
                    109, 100, 92, 86, 80, 75, 67, 60, 55, 50, 46, 43, 40, 
                    37, 33, 30, 27, 25, 23, 21, 20 };

  int delays[4] = { 250, 500, 750, 1000 };

  unsigned int rate, delay; /* Parameters sent by user */
  unsigned int i, rDiff, rateId; /* Used to find the rate closest to the param */
  unsigned int dDiff, delayId;   /* Used to find the delay closest to the param */
  unsigned int cmd; /* Control byte to send to the keyboard */

  rate = luaL_checkinteger( L, 1 );
  delay = luaL_checkinteger( L, 2 );
  
  /* Find the rate closest to the one passed */
  rDiff = abs( rate - rates[0] );
  dDiff = abs( delay - delays[0] );
  rateId = 0;
  delayId = 0;

  for ( i=1; i<32; i++ )
  {
    if ( abs( rate - rates[i] ) < rDiff )
    {
      rDiff = abs( rate - rates[i] );
      rateId = i;
    }

    if ( abs( rate - rates[i] ) > rDiff )
      break;
  }

  /* Find the delay closest to the one passed */ 
  for ( i=1; i<4; i++ )
  {
    if ( abs( delay - delays[i] ) < dDiff )
    {
      dDiff = abs( delay - delays[i] );
      delayId = i;
    }

    if ( abs( delay - delays[i] ) > dDiff )
      break;
  }

  cmd = rateId;
  cmd = cmd | delayId << 5; 

  keyboard_write( 0xF3 );
  keyboard_write( cmd );

  lua_pushinteger( L, rates[ rateId ] );
  lua_pushinteger( L, delays[ delayId ] );

  return 2;
}

/* Sets the used Key Scan Code Set ( 1, 2 or 3 )
 *
 * Lua: keyboard.setscancodeset( set )
 */
static int keyboard_setScanCodeSet( lua_State *L )
{
  int i = luaL_checkinteger( L, 1 );

  if ( ( i > 3 ) || ( i < 1 ) )
    return 0;

  /* Send Command Code */
  keyboard_write( SET_SCAN_CODE_SET );

  /* Wait for ACK */
  if ( keyboard_getchar() != ACK )
    return 0;

  /* Send Scan Set Code */
  keyboard_write( i );

  return 0;
}

/* Enables keyboard's key scanning after a disable command
 *
 * Lua: keyboard.enable()
 */
static int keyboard_enable( lua_State *L )
{
  keyboard_write( ENABLE );
  return 0;
}

/* Disable keyboard's key scanning ( keyboard stops looking for
 * pressed keys.
 *
 * Note: keyboard will return to the default configuration
 * ( see keyboard.default() command ).
 *
 * Lua: keyboard.disable()
 */
static int keyboard_disable( lua_State *L )
{
  keyboard_write( DISABLE );
  return 0;
}

/* Retuns keyboard to it's default state:
 * Typematic delay = 500ms
 * Typematic rate = 10.9 c.p.s
 * Keyboard sends key Make, Break and Typematic messages
 * Key Scan Code Set = 2
 *
 * Lua: keyboard.default()
 */
static int keyboard_default( lua_State *L )
{
  keyboard_write( DEFAULT );
  return 0;
}

/* Resets the keyboard
 *
 * Lua: keyboard.reset()
 */
static int keyboard_reset( lua_State *L )
{
  keyboard_write( RESET );

  /* Wait for ACK */
  keyboard_getchar();

  return 0;
}

/* Keyboards resends last byte, except if it was "resend".
 * In this case it sends the last non-resend byte.
 *
 * Lua: keyboard.resend()
 */
static int keyboard_resend( lua_State *L )
{
  /* Send command code */
  keyboard_write( RESEND );

  /* Returns the response */
  lua_pushinteger( L, keyboard_getchar() );
  return 1;
}

/* Keyboard responds with echo ( keyboard.ECHO - 0xEE )
 *
 * Lua: keyboard.echo()
 */
static int keyboard_echo( lua_State *L )
{
  /* Sends echo */
  keyboard_write( ECHO );

  /* Returns the response */
  lua_pushinteger( L, keyboard_getchar() );

  return 1;
}

const LUA_REG_TYPE keyboard_map[] = {
  { LSTRKEY( "init" ), LFUNCVAL( keyboard_init ) },
  { LSTRKEY( "receive" ), LFUNCVAL( keyboard_receive ) },
  { LSTRKEY( "setflags" ), LFUNCVAL( keyboard_setflags ) },
//  { LSTRKEY( "read" ), LFUNCVAL( keyboard_read ) },
  { LSTRKEY( "send" ), LFUNCVAL( keyboard_send ) },
  { LSTRKEY( "setleds" ), LFUNCVAL( keyboard_setleds ) },
  { LSTRKEY( "configkeys" ), LFUNCVAL( keyboard_configkeys ) },
  { LSTRKEY( "disablekeyevents" ), LFUNCVAL( keyboard_disablekeyevents ) },
  { LSTRKEY( "setrepeatrateanddelay" ), LFUNCVAL( keyboard_setRepeatRateAndDelay) },
  { LSTRKEY( "setscancodeset" ), LFUNCVAL( keyboard_setScanCodeSet ) },
  { LSTRKEY( "reset" ), LFUNCVAL( keyboard_reset ) },
  { LSTRKEY( "enable" ), LFUNCVAL( keyboard_enable ) },
  { LSTRKEY( "disable" ), LFUNCVAL( keyboard_disable ) },
  { LSTRKEY( "default" ), LFUNCVAL( keyboard_default ) },
  { LSTRKEY( "resend" ), LFUNCVAL( keyboard_resend ) },
  { LSTRKEY( "echo" ), LFUNCVAL( keyboard_echo ) },
  { LSTRKEY( "ECHO" ), LNUMVAL( ECHO ) },
  { LSTRKEY( "IGNORE" ), LNUMVAL( IGNORE ) },
  { LSTRKEY( "USE" ), LNUMVAL( USE ) },
  { LSTRKEY( "ERROR" ), LNUMVAL( ERROR ) },
  { LSTRKEY( "ACK" ), LNUMVAL( ACK) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_keyboard ( lua_State *L )
{
  LREGISTER( L, "keyboard", keyboard_map );
};

