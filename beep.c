/*  beep - just what it sounds like, makes the console beep - but with
 * precision control.  See the man page for details.
 *
 * Try beep -h for command line args
 *
 * This code is copyright (C) Johnathan Nightingale, 2000.
 *
 * This code may distributed only under the terms of the GNU Public License 
 * which can be found at http://www.gnu.org/copyleft or in the file COPYING 
 * supplied with this code.
 *
 * This code is not distributed with warranties of any kind, including implied
 * warranties of merchantability or fitness for a particular use or ability to 
 * breed pandas in captivity, it just can't be done.
 *
 * Bug me, I like it:  http://johnath.com/  or johnath@johnath.com
 */

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/kd.h>
#include <linux/input.h>

#ifdef HAVE_WIRINGPI
#include <wiringPi.h>
#endif

/* I don't know where this number comes from, I admit that freely.  A 
   wonderful human named Raine M. Ekman used it in a program that played
   a tune at the console, and apparently, it's how the kernel likes its
   sound requests to be phrased.  If you see Raine, thank him for me.  
   
   June 28, email from Peter Tirsek (peter at tirsek dot com):
   
   This number represents the fixed frequency of the original PC XT's
   timer chip (the 8254 AFAIR), which is approximately 1.193 MHz. This
   number is divided with the desired frequency to obtain a counter value,
   that is subsequently fed into the timer chip, tied to the PC speaker.
   The chip decreases this counter at every tick (1.193 MHz) and when it
   reaches zero, it toggles the state of the speaker (on/off, or in/out),
   resets the counter to the original value, and starts over. The end
   result of this is a tone at approximately the desired frequency. :)
*/
#ifndef CLOCK_TICK_RATE
#define CLOCK_TICK_RATE 1193180
#endif

#define VERSION_STRING "beep-1.3"
char *copyright = 
"Copyright (C) Johnathan Nightingale, 2002.  "
"Use and Distribution subject to GPL.  "
"For information: http://www.gnu.org/copyleft/.";

/* Meaningful Defaults */
#define DEFAULT_FREQ       440.0 /* Middle A */
#define DEFAULT_LENGTH     200   /* milliseconds */
#define DEFAULT_REPS       1
#define DEFAULT_DELAY      100   /* milliseconds */
#define DEFAULT_END_DELAY  NO_END_DELAY
#define DEFAULT_STDIN_BEEP NO_STDIN_BEEP

/* Other Constants */
#define NO_END_DELAY    0
#define YES_END_DELAY   1

#define NO_STDIN_BEEP   0
#define LINE_STDIN_BEEP 1
#define CHAR_STDIN_BEEP 2

typedef struct beep_parms_t {
  float freq;     /* tone frequency (Hz)      */
  int length;     /* tone length    (ms)      */
  int reps;       /* # of repetitions         */
  int delay;      /* delay between reps  (ms) */
  int end_delay;  /* do we delay after last rep? */
  int stdin_beep; /* are we using stdin triggers?  We have three options:
		     - just beep and terminate (default)
		     - beep after a line of input
		     - beep after a character of input
		     In the latter two cases, pass the text back out again,
		     so that beep can be tucked appropriately into a text-
		     processing pipe.
		  */
  int verbose;    /* verbose output?          */
  struct beep_parms_t *next;  /* in case -n/--new is used. */
} beep_parms_t;

enum { BEEP_TYPE_CONSOLE, BEEP_TYPE_EVDEV, BEEP_TYPE_WIRINGPI };

/* Momma taught me never to use globals, but we need something the signal 
   handlers can get at.*/
int console_fd = -1;
int console_type = BEEP_TYPE_CONSOLE;
char *console_device = NULL;
#ifdef HAVE_WIRINGPI
int gpio_pin = -1;

int setup_gpio()
{
  if(gpio_pin == -1)
  {
    int err = -1;

    if(!geteuid())
      err = wiringPiSetupGpio();
    else if(!(err = system("gpio -v")))
      err = wiringPiSetupSys();

    if(err) {
      fprintf(stderr, "GPIO setup failed!\n");
      return 1;
    }

    gpio_pin = wpiPinToGpio(1);
  }

  return 0;
}
#endif

void do_beep(int freq) {
  int period = (freq != 0 ? (int)(CLOCK_TICK_RATE/freq) : freq);

  if(console_type == BEEP_TYPE_CONSOLE) {
    if(ioctl(console_fd, KIOCSOUND, period) < 0) {
      putchar('\a');  /* Output the only beep we can, in an effort to fall back on usefulness */
      perror("ioctl");
    }
  } else if(console_type == BEEP_TYPE_EVDEV) {
     /* BEEP_TYPE_EVDEV */
     struct input_event e;

     e.type = EV_SND;
     e.code = SND_TONE;
     e.value = freq;

     if(write(console_fd, &e, sizeof(struct input_event)) < 0) {
       putchar('\a'); /* See above */
       perror("write");
     }
  }
#ifdef HAVE_WIRINGPI
  else if(console_type == BEEP_TYPE_WIRINGPI) {
    if(freq) {
      int period = 600000/freq;

      if(!geteuid()) {
        pinMode(gpio_pin, PWM_OUTPUT);
        pwmSetRange(period);
        pwmWrite(gpio_pin, period / 2);
        pwmSetMode(PWM_MODE_MS);
      } else {
        char buffer[64];

        snprintf(buffer, 64, "gpio -g mode %d pwm", gpio_pin);
        system(buffer);

        snprintf(buffer, 64, "gpio pwmr %d", period);
        system(buffer);

        snprintf(buffer, 64, "gpio -g pwm %d %d", gpio_pin, period / 2);
        system(buffer);

        system("gpio pwm-ms");
      }
    } else {
	  if(!geteuid()) {
	    pinMode(gpio_pin, INPUT);
	  } else {
	    char buffer[64];

	    snprintf(buffer, 64, "gpio -g mode %d in", gpio_pin);
	    system(buffer);
	  }
    }
  }
#endif
}


/* If we get interrupted, it would be nice to not leave the speaker beeping in
   perpetuity. */
void handle_signal(int signum) {

  if(console_device)
    free(console_device);

  switch(signum) {
  case SIGINT:
  case SIGTERM:
    if(console_fd >= 0
#ifdef HAVE_WIRINGPI		   
      || gpio_pin >= 0
#endif
	 ) {
      /* Kill the sound, quit gracefully */
      do_beep(0);
      if(console_fd >= 0)
        close(console_fd);
      exit(signum);
    } else {
      /* Just quit gracefully */
      exit(signum);
    }
  }
}

/* print usage and exit */
void usage_bail(const char *executable_name) {
  printf("Usage:\n%s [-f freq] [-l length] [-r reps] [-d delay] "
	 "[-D delay] [-s] [-c] [--verbose | --debug] [-e device]\n",
	 executable_name);
  printf("%s [Options...] [-n] [--new] [Options...] ... \n", executable_name);
  printf("%s [-h] [--help]\n", executable_name);
  printf("%s [-v] [-V] [--version]\n", executable_name);
  exit(1);
}

/* calculates (12th root of two)^n */
float pow12root2(int n) {
  if(n < 0)
    return 1.0f / pow12root2(-n);
  else if(n > 0)
    return 1.0594630943592952645f * pow12root2(n-1);
  else
    return 1.0f;
}

/* converts a piano key number to its frequency */
int key_to_freq(int key) {
  return roundf(440.0f * pow12root2(key-49));
}

int note_to_index(int raw_note)
{
  switch(raw_note)
  {
    case 'C':
      return 0;
    case 'D':
      return 2;
    case 'E':
      return 4;
    case 'F':
      return 5;
    case 'G':
      return 7;
    case 'A':
      return 9;
    case 'B':
      return 11;
    default:
      return -1;
  }
}

/* converts a note like A4 or C#4 to its frequency */
int sci_note_to_freq(const char* note) {
  const char* notep;
  int octave;
  int note_index;
  int adj;

  notep = note;

  if((note_index = note_to_index(*notep++)) == -1) {
    return -1;
  }

  octave = 4;
  adj = 0;

  switch(*notep)
  {
    case '#':
      adj = 1;
      ++notep;
      break;
    case 'b':
      adj = -1;
      ++notep;
      break;
  }

  if(!sscanf(notep, "%d", &octave) || octave < 0) {
    return -1;
  }
#if 1
  else {
    int key = -8 + 12 * octave + note_index + adj;
    int freq = key_to_freq(key);
    fprintf(stderr, "[DEBUG] %s -> #%d -> %d Hz\n", note, key, freq);
    return freq;
  }
#else
  return key_to_freq(-8 + 12 * octave + note_index + adj);
#endif
}

int abc_note_to_freq(const char* note)
{
  const char* notep;
  int octave;
  int note_index;
  int adj;

  notep = note;
  octave = 2;
  adj = 0;

  switch(*notep)
  {
    case '^':
      adj = 1;
      ++notep;
      break;
    case '_':
      adj = -1;
      ++notep;
      break;
  }

  if(islower(*notep)) {
    note_index = note_to_index(toupper(*notep));
    ++octave;
  } else {
    note_index = note_to_index(*notep);
  }

  if(note_index < 0)
    return -1;

  while(*++notep)
  {
    switch(*notep)
    {
      case '\'':
        ++octave;
        break;

      case ',':
        --octave;
        break;

      default:
        fprintf(stderr, "WARNING: ignoring %s portion of %s\n", notep, note);
    }
  }

#if 1
    int key = -8 + 12 * octave + note_index + adj;
    int freq = key_to_freq(key);
    fprintf(stderr, "[DEBUG] %s -> #%d -> %d Hz\n", note, key, freq);
    return freq;
#else
  return note_to_freq(octave, note_index, adj);
#endif
}

int note_to_freq(int octave, int note_index, int adj) {
  return key_to_freq(-8 + 12 * octave + note_index + adj);
}


/* Parse the command line.  argv should be untampered, as passed to main.
 * Beep parameters returned in result, subsequent parameters in argv will over-
 * ride previous ones.
 * 
 * Currently valid parameters:
 *  "-f <frequency in Hz>"
 *  "-k <piano key#>
 *  "-N <note>
 *  "-l <tone length in ms>"
 *  "-r <repetitions>"
 *  "-d <delay in ms>"
 *  "-D <delay in ms>" (similar to -d, but delay after last repetition as well)
 *  "-s" (beep after each line of input from stdin, echo line to stdout)
 *  "-c" (beep after each char of input from stdin, echo char to stdout)
 *  "--verbose/--debug"
 *  "-h/--help"
 *  "-v/-V/--version"
 *  "-n/--new"
 *
 * March 29, 2002 - Daniel Eisenbud points out that c should be int, not char,
 * for correctness on platforms with unsigned chars.
 */
void parse_command_line(int argc, char **argv, beep_parms_t *result) {
  int c;
  int note_format_sci = 1;

  struct option opt_list[7] = {{"help", 0, NULL, 'h'},
			       {"version", 0, NULL, 'V'},
			       {"new", 0, NULL, 'n'},
			       {"verbose", 0, NULL, 'X'},
			       {"debug", 0, NULL, 'X'},
			       {"device", 1, NULL, 'e'},
			       {0,0,0,0}};
  while((c = getopt_long(argc, argv, "f:k:N:F:l:r:d:D:schvVne:", opt_list, NULL))
	!= EOF) {
    int argval = -1;    /* handle parsed numbers for various arguments */
    float argfreq = -1; 
    switch(c) {      
    case 'f':  /* freq */
      if(!sscanf(optarg, "%f", &argfreq) || (argfreq >= 20000 /* ack! */) || 
	 (argfreq <= 0))
	usage_bail(argv[0]);
      else
	if (result->freq != 0)
	  fprintf(stderr, "WARNING: multiple -f/-k values given, only last "
	    "one is used.\n");
	result->freq = argfreq;    
      break;
    case 'k': /* piano key# */
      if(!sscanf(optarg, "%d", &argval) || (argval < 0))
        usage_bail(argv[0]);
      else if(result->freq != 0)
        fprintf(stderr, "WARNING: multiple -f/-k values given, only last "
          "one is used.\n");
      result->freq = key_to_freq(argval);
	  break;
    case 'N': /* note */
      if(note_format_sci)
        argval = sci_note_to_freq(optarg);
      else
        argval = abc_note_to_freq(optarg);

      if(argval < 0)
        fprintf(stderr, "ERROR: failed to parse note %s\n", optarg);
      if(result->freq != 0) {
        fprintf(stderr, "WARNING: multiple -f/-k values given, only last "
          "one is used.\n");
      }

      result->freq = argval;
      break;
    case 'F': /* note format */
      if(!strcmp("sci", optarg))
        note_format_sci = 1;
      else if(!strcmp("abc", optarg))
        note_format_sci = 0;
      else
        fprintf(stderr, "ERROR: options valid for -F are 'sci' or 'abc'\n");
      break;
    case 'l' : /* length */
      if(!sscanf(optarg, "%d", &argval) || (argval < 0))
	usage_bail(argv[0]);
      else
	result->length = argval;
      break;
    case 'r' : /* repetitions */
      if(!sscanf(optarg, "%d", &argval) || (argval < 0))
	usage_bail(argv[0]);
      else
	result->reps = argval;
      break;
    case 'd' : /* delay between reps - WITHOUT delay after last beep*/
      if(!sscanf(optarg, "%d", &argval) || (argval < 0))
	usage_bail(argv[0]);
      else {
	result->delay = argval;
	result->end_delay = NO_END_DELAY;
      }
      break;
    case 'D' : /* delay between reps - WITH delay after last beep */
      if(!sscanf(optarg, "%d", &argval) || (argval < 0))
	usage_bail(argv[0]);
      else {
	result->delay = argval;
	result->end_delay = YES_END_DELAY;
      }
      break;
    case 's' :
      result->stdin_beep = LINE_STDIN_BEEP;
      break;
    case 'c' :
      result->stdin_beep = CHAR_STDIN_BEEP;
      break;
    case 'v' :
    case 'V' : /* also --version */
      printf("%s\n",VERSION_STRING);
      exit(0);
      break;
    case 'n' : /* also --new - create another beep */
      if (result->freq == 0)
	result->freq = DEFAULT_FREQ;
      result->next = (beep_parms_t *)malloc(sizeof(beep_parms_t));
      result->next->freq       = 0;
      result->next->length     = DEFAULT_LENGTH;
      result->next->reps       = DEFAULT_REPS;
      result->next->delay      = DEFAULT_DELAY;
      result->next->end_delay  = DEFAULT_END_DELAY;
      result->next->stdin_beep = DEFAULT_STDIN_BEEP;
      result->next->verbose    = result->verbose;
      result->next->next       = NULL;
      result = result->next; /* yes, I meant to do that. */
      break;
    case 'X' : /* --debug / --verbose */
      result->verbose = 1;
      break;
    case 'e' : /* also --device */
      console_device = strdup(optarg);
      break;
    case 'h' : /* notice that this is also --help */
    default :
      usage_bail(argv[0]);
    }
  }
  if (result->freq == 0)
    result->freq = DEFAULT_FREQ;
}  

void play_beep(beep_parms_t parms) {
  int i; /* loop counter */

  if(parms.verbose == 1)
      fprintf(stderr, "[DEBUG] %d times %d ms beeps (%d delay between, "
	"%d delay after) @ %.2f Hz\n",
	parms.reps, parms.length, parms.delay, parms.end_delay, parms.freq);

  /* try to snag the console */
  if(console_device)
    console_fd = open(console_device, O_WRONLY);
#ifdef HAVE_WIRINGPI
  else if(setup_gpio() == 0)
    console_type = BEEP_TYPE_WIRINGPI;
#endif
  else
    if((console_fd = open("/dev/tty0", O_WRONLY)) == -1)
      console_fd = open("/dev/vc/0", O_WRONLY);

  if(console_fd == -1) {
	if(console_type != BEEP_TYPE_WIRINGPI) {
      fprintf(stderr, "Could not open %s for writing\n",
      console_device != NULL ? console_device : "/dev/tty0 or /dev/vc/0");
      printf("\a");  /* Output the only beep we can, in an effort to fall back on usefulness */
      perror("open");
      exit(1);
	}
  } else {
	  if (ioctl(console_fd, EVIOCGSND(0)) != -1)
		console_type = BEEP_TYPE_EVDEV;
	  else
		console_type = BEEP_TYPE_CONSOLE;
  }

  if(parms.verbose)
	  fprintf(stderr, "[DEBUG] console_type=%d\n", console_type);
  
  /* Beep */
  for (i = 0; i < parms.reps; i++) {                    /* start beep */
    do_beep(parms.freq);
    /* Look ma, I'm not ansi C compatible! */
    usleep(1000*parms.length);                          /* wait...    */
    do_beep(0);                                         /* stop beep  */
    if(parms.end_delay || (i+1 < parms.reps))
       usleep(1000*parms.delay);                        /* wait...    */
  }                                                     /* repeat.    */

  close(console_fd);
}



int main(int argc, char **argv) {
  char sin[4096], *ptr;
  
  beep_parms_t *parms = (beep_parms_t *)malloc(sizeof(beep_parms_t));
  parms->freq       = 0;
  parms->length     = DEFAULT_LENGTH;
  parms->reps       = DEFAULT_REPS;
  parms->delay      = DEFAULT_DELAY;
  parms->end_delay  = DEFAULT_END_DELAY;
  parms->stdin_beep = DEFAULT_STDIN_BEEP;
  parms->verbose    = 0;
  parms->next       = NULL;

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  parse_command_line(argc, argv, parms);

  /* this outermost while loop handles the possibility that -n/--new has been
     used, i.e. that we have multiple beeps specified. Each iteration will
     play, then free() one parms instance. */
  while(parms) {
    beep_parms_t *next = parms->next;

    if(parms->stdin_beep) {
      /* in this case, beep is probably part of a pipe, in which case POSIX 
	 says stdin and out should be fuly buffered.  This however means very 
	 laggy performance with beep just twiddling it's thumbs until a buffer
	 fills. Thus, kill the buffering.  In some situations, this too won't 
	 be enough, namely if we're in the middle of a long pipe, and the 
	 processes feeding us stdin are buffered, we'll have to wait for them,
	 not much to  be done about that. */
      setvbuf(stdin, NULL, _IONBF, 0);
      setvbuf(stdout, NULL, _IONBF, 0);
      while(fgets(sin, 4096, stdin)) {
	if(parms->stdin_beep==CHAR_STDIN_BEEP) {
	  for(ptr=sin;*ptr;ptr++) {
	    putchar(*ptr);
	    fflush(stdout);
	    play_beep(*parms);
	  }
	} else {
	  fputs(sin, stdout);
	  play_beep(*parms);
	}
      }
    } else {
      play_beep(*parms);
    }

    /* Junk each parms struct after playing it */
    free(parms);
    parms = next;
  }

  if(console_device)
    free(console_device);

  return EXIT_SUCCESS;
}
