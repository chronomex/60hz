/* Simple example for Teensy USB Development Board
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2008 PJRC.COM, LLC
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <util/delay.h>
#include "usb_serial.h"

#define LED_CONFIG	(DDRD |= (1<<6))
#define LED_ON		(PORTD |= (1<<6))
#define LED_OFF		(PORTD &= ~(1<<6))
#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))

void send_str(const char *s);
uint8_t recv_str(char *buf, uint8_t size);
void parse_and_execute_command(const char *buf, uint8_t num);

int main(void)
{
	int changed = 0, val;
	int port = 'B' - 'A';
	int pin = 1 << ('0' - '0');

	CPU_PRESCALE(0x00); // 16 MHz
	usb_init();
	LED_CONFIG;
	while (!usb_configured()) /* wait */ ;
	_delay_ms(1000);
	CPU_PRESCALE(0x04); // 1 MHz

	uint8_t *portload = 0x20 + port * 3;
	*portload &= ~pin;

	TCCR1B |= (1 << CS10); // Set timer1 to 1 microsecond steps

	while (1) {
		val = *portload & pin;
		if ((val == 0)  && (changed == 1)) {
			/* trailing edge of pulse.
			 *
			 * I track the 1->0 transition rather than
			 * 0->1 because it's sharper.
			 */
			int count;
			count = TCNT1; // read timer
			if (count < 15000)
				/* ignore spurious pulses.  If I don't
				 * do this, for some reason I'll see a
				 * bunch of half-width pulses.
				 */
				continue;
			TCNT1 = 0; // reset timer
			changed = 0;

//			usb_serial_putchar(count > 16667 ? '>' : '<');
			send_int(count);
			usb_serial_putchar('\n');
		} else if ((val == 1)  && (changed == 0)) {
			/* leading edge of pulse.
			 */
			changed = 1;
//			usb_serial_putchar('0');
		}
	}
}

// Send a string to the USB serial port.  The string must be in
// flash memory, using PSTR
//
void send_str(const char *s)
{
	char c;
	while (1) {
		c = pgm_read_byte(s++);
		if (!c) break;
		usb_serial_putchar(c);
	}
}

/* Write a decimal integer to the serial port
 */
void send_int(int val)
{
	char c_out[10];
	int ptr;
	c_out[ptr++] = '\0';
	while (val > 0) {
		c_out[ptr++] = (val % 10) + '0';
		val /= 10;
	}
	ptr--;
	while (ptr) {
		usb_serial_putchar(c_out[ptr--]);
	}
}

// Receive a string from the USB serial port.  The string is stored
// in the buffer and this function will not exceed the buffer size.
// A carriage return or newline completes the string, and is not
// stored into the buffer.
// The return value is the number of characters received, or 255 if
// the virtual serial connection was closed while waiting.
//
uint8_t recv_str(char *buf, uint8_t size)
{
	int16_t r;
	uint8_t count=0;

	while (count < size) {
		r = usb_serial_getchar();
		if (r != -1) {
			if (r == '\r' || r == '\n') return count;
			if (r >= ' ' && r <= '~') {
				*buf++ = r;
				usb_serial_putchar(r);
				count++;
			}
		} else {
			if (!usb_configured() ||
			  !(usb_serial_get_control() & USB_SERIAL_DTR)) {
				// user no longer connected
				return 255;
			}
			// just a normal timeout, keep waiting
		}
	}
	return count;
}

// parse a user command and execute it, or print an error message
//
void parse_and_execute_command(const char *buf, uint8_t num)
{
	uint8_t port, pin, val;

	if (num < 3) {
		send_str(PSTR("unrecognized format, 3 chars min req'd\r\n"));
		return;
	}
	// first character is the port letter
	if (buf[0] >= 'A' && buf[0] <= 'F') {
		port = buf[0] - 'A';
	} else if (buf[0] >= 'a' && buf[0] <= 'f') {
		port = buf[0] - 'a';
	} else {
		send_str(PSTR("Unknown port \""));
		usb_serial_putchar(buf[0]);
		send_str(PSTR("\", must be A - F\r\n"));
		return;
	}
	// second character is the pin number
	if (buf[1] >= '0' && buf[1] <= '7') {
		pin = buf[1] - '0';
	} else {
		send_str(PSTR("Unknown pin \""));
		usb_serial_putchar(buf[0]);
		send_str(PSTR("\", must be 0 to 7\r\n"));
		return;
	}
	// if the third character is a question mark, read the pin
	if (buf[2] == '?') {
		// make the pin an input
		*(uint8_t *)(0x21 + port * 3) &= ~(1 << pin);
		// read the pin
		val = *(uint8_t *)(0x20 + port * 3) & (1 << pin);
		usb_serial_putchar(val ? '1' : '0');
		send_str(PSTR("\r\n"));
		return;
	}
	// if the third character is an equals sign, write the pin
	if (num >= 4 && buf[2] == '=') {
		if (buf[3] == '0') {
			// make the pin an output
			*(uint8_t *)(0x21 + port * 3) |= (1 << pin);
			// drive it low
			*(uint8_t *)(0x22 + port * 3) &= ~(1 << pin);
			return;
		} else if (buf[3] == '1') {
			// make the pin an output
			*(uint8_t *)(0x21 + port * 3) |= (1 << pin);
			// drive it high
			*(uint8_t *)(0x22 + port * 3) |= (1 << pin);
			return;
		} else {
			send_str(PSTR("Unknown value \""));
			usb_serial_putchar(buf[3]);
			send_str(PSTR("\", must be 0 or 1\r\n"));
			return;
		}
	}
	// otherwise, error message
	send_str(PSTR("Unknown command \""));
	usb_serial_putchar(buf[0]);
	send_str(PSTR("\", must be ? or =\r\n"));
}


