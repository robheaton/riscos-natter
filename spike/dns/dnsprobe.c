/* dnsprobe.c - asks the Resolver module of RISC OS for the address of a host in small steps (Resolver_GetHost, SWI 0x46001), as Natter will, and prints what each call says and when.
   dnsprobe NAME [NAME ...] */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kernel.h"

static char name[256];					/* below 4 GB: a static buffer */

static long now_ms (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000L; }

int main (int argc, char **argv)
{
  int a;
  for (a = 1; a < argc; a++)
    {
      long t0 = now_ms();
      int calls = 0;
      snprintf (name, sizeof name, "%s", argv[a]);
      printf ("%s:\n", name);
      for (;;)
	{
	  _kernel_swi_regs in, out;
	  _kernel_oserror *e;
	  memset (&in, 0, sizeof in);
	  in.r[0] = (int) (unsigned long) name;
	  e = _kernel_swi (0x46001 | 0x20000, &in, &out);
	  calls++;
	  if (e) { printf ("  call %d at %ld ms: SWI error %d %s\n", calls, now_ms () - t0, e->errnum, e->errmess); break; }
	  if (out.r[0] == 0 && out.r[1])
	    {
	      const unsigned int *h = (const unsigned int *) (unsigned long) (unsigned) out.r[1];
	      const unsigned int *list = (const unsigned int *) (unsigned long) h[4];
	      int n = 0;
	      printf ("  call %d at %ld ms: found, type %u length %u addresses:", calls, now_ms () - t0, h[2], h[3]);
	      while (list && list[n] && n < 8) { const unsigned char *p = (const unsigned char *) (unsigned long) list[n]; printf (" %d.%d.%d.%d", p[0], p[1], p[2], p[3]); n++; }
	      printf ("\n");
	      break;
	    }
	  printf ("  call %d at %ld ms: r0=%d r1=%x\n", calls, now_ms () - t0, out.r[0], (unsigned) out.r[1]);
	  if (out.r[0] != 36) break;				/* not "in progress": an answer (an error) */
	  if (now_ms () - t0 > 20000) { printf ("  gave up after 20 s\n"); break; }
	  usleep (50000);
	}
    }
  return 0;
}
