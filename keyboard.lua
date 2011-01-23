-----------------------------------------------------------------------------
--    This file exemplifies the use of keyboard lib running on a MBED      --
--    This example implements keyboard_read() function that prints         --
--    received keys on screen.                                             --
--    Note: Only letters a ~ z, space, shift and enter keys are            --
--    implemented. Any other key will break the ( endless ) loop.          --

local chars = {}

chars[ 0x1C ] = "a"
chars[ 0x32 ] = "b"
chars[ 0x21 ] = "c"
chars[ 0x23 ] = "d"
chars[ 0x24 ] = "e"
chars[ 0x2B ] = "f"
chars[ 0x34 ] = "g"
chars[ 0x33 ] = "h"
chars[ 0x43 ] = "i"
chars[ 0x3B ] = "j"
chars[ 0x42 ] = "k"
chars[ 0x4B ] = "l"
chars[ 0x3A ] = "m"
chars[ 0x31 ] = "n"
chars[ 0x44 ] = "o"
chars[ 0x4D ] = "p"
chars[ 0x15 ] = "q"
chars[ 0x2D ] = "r"
chars[ 0x1B ] = "s"
chars[ 0x2C ] = "t"
chars[ 0x3C ] = "u"
chars[ 0x2A ] = "v"
chars[ 0x1D ] = "w"
chars[ 0x22 ] = "x"
chars[ 0x35 ] = "y"
chars[ 0x1A ] = "z"
chars[ 0x29 ] = " "
-- chars[ 0x5A ] = "\n"

function keyboard_read()
  local c
  local shift = false

  while true do
    c = keyboard.receive()

    -- check if it's the Shift key
    if ( c == 0x12 ) or ( c == 0x59 ) then -- Shift press
      shift = true
    else -- Shift release ( or other key press / release )
      if c == 0xF0 then
        c = keyboard.receive()
        if ( c == 0x12 ) or ( c == 0x59 ) then
          shift = false
        end
      else -- Other keys
        -- If it's a keyup message, ignore ( 2 bytes )
        if c == 0xF0 then
          keyboard.receive()
        else -- Else, print the Char
          if c == 0x5A then
            print( "" )
          else
            if chars[ c] == nil then
              print( "" )
              return
            else
              if shift then
                term.print( string.upper( chars[ c ] ) )
              else
                term.print( chars[ c ] )
              end;
            end
          end
        end
      end
    end
  end
end

-- Initialize the IOs
keyboard.init( mbed.pio.P18, mbed.pio.P10, mbed.pio.P19, mbed.pio.P11 )

-- Pins:
-- Clock: Pin 18
-- Data:  Pin 10
-- Clock Pull Down: Pin 19
-- Data Pull Down:  Pin 11

-- Ignore stop bit ( buggy keyboard... )
-- keyboard.setflags( 0, 1, 0 )
keyboard.setflags( keyboard.USE, keyboard.IGNORE, keyboard.USE );
