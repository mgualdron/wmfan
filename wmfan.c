/* wmfan - cooling fan speed monitor designed for Window Maker
*          based on wmload
*  Copyright (C) 1996 Beat Christen <bchriste@iiic.ethz.ch>
*  Copyright (C) 1997 Ryan Land <rland@bc1.com>
*  Copyright (C) 2015 Window Maker Developers Team
*                     <wmaker-dev@lists.windowmaker.org>
*  Copyright (C) 2020 Miguel Gualdron
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License along
*  with this program; if not, write to the Free Software Foundation, Inc.,
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <X11/Xatom.h>

#include "back.xpm"
#include "mask2.xbm"
#include "mask.xpm"

#define major_VER 0
#define minor_VER 0
#define patch_VER 1
#define MW_EVENTS   (ExposureMask | ButtonPressMask | StructureNotifyMask)
#define FALSE 0
#define Shape(num) (ONLYSHAPE ? num-5 : num)
#define NCPUSTATES 4

/* Global Data storage/structures ********************************************/
static long cp_time[NCPUSTATES];
static long last[NCPUSTATES];
int ONLYSHAPE=0; /* default value is noshape */
int updatespeed = 4;
static char *help_message[] = {
"where options include:",
"    -u <secs>               updatespeed",
"    -exe <program>          program to start on click",
"    -led <color>            color of the led",
"    -position [+|-]x[+|-]y  position of wmfan",
"    -shape                  without groundplate",
"    -iconic                 start up as icon",
"    -withdrawn              start up withdrawn",
"    -ver                    output version",
NULL
};

/* X11 Variables *************************************************************/
Display *dpy;	  /* welches DISPLAY */
Window Root;      /* Hintergrund-Drawable */
int screen;
int x_fd;
int d_depth;
XSizeHints mysizehints;
XWMHints mywmhints;
Pixel back_pix, fore_pix;
GC NormalGC;
Window iconwin, win;       /* My home is my window */
char *ProgName;
char *Geometry;
char *LedColor = "LightSeaGreen";
char Execute[] = "echo no program has been specified";
char *ERR_colorcells = "not enough free color cells\n";
char *ampers = " &";

/* XPM Structures & Variables ************************************************/
typedef struct _XpmIcon {
    Pixmap pixmap;
    Pixmap mask;
    XpmAttributes attributes;
}        XpmIcon;

XpmIcon wmfan;
XpmIcon visible;
time_t actualtime;
long actualmin;

/* Function definitions ******************************************************/
void GetXPM(void);
Pixel GetColor(char *name);
void RedrawWindow( XpmIcon *v);
void InitLoad();
void InsertLoad();

/*****************************************************************************/
/* Source Code <--> Function Implementations                                 */
/*****************************************************************************/
void usage()
{
  char **cpp;

  fprintf(stderr,"\nusage:  %s [-options ...] \n", ProgName);
  for (cpp = help_message; *cpp; cpp++) {
    fprintf(stderr, "%s\n", *cpp);
  }
  fprintf(stderr,"\n");
  exit(1);
}

/*
 * Copied from ascpu - albert@tigr.net - 09 Mar 2000
 *
 * This function executes an external command while
 * checking whether we should drop the privileges.
 *
 * Since we might need privileges later we fork and
 * then drop privileges in one of the instances which
 * will then execute the command and die.
 *
 * This fixes the security hole for FreeBSD and AIX
 * where this program needs privileges to access
 * the system information.
 */
void ExecuteExternal()
{
	uid_t ruid, euid;
	int pid;
#ifdef DEBUG
	printf("wmfan: system(%s)\n",Execute);
#endif
	if( Execute[0] == '\0' ) {
		return;
	}
	ruid = getuid();
	euid = geteuid();
	if ( ruid == euid ) {
		if (system( Execute ) == -1)
			fprintf(stderr, "system(%s) returned an error",
				Execute);
		return;
	}
	pid = fork();
	if ( pid == -1 ) {
		printf("wmfan : fork() failed (%s), command not executed",
				strerror(errno));
		return;
	}
	if ( pid != 0 ) {
		/* parent process simply waits for the child and continues */
		if ( waitpid(pid, 0, 0) == -1 ) {
			printf("wmfan : waitpid() for child failed (%s)",
				strerror(errno));
		}
		return;
	}
	/*
	 * child process drops the privileges
	 * executes the command and dies
	 */
	if ( setuid(ruid) ) {
		printf("wmfan : setuid failed (%s), command not executed",
				strerror(errno));
		exit(127);
	}
	if (system( Execute ) == -1)
		fprintf(stderr, "system(%s) returned an error", Execute);
	exit(0);
}
int main(int argc,char *argv[])
{
  int i;
  unsigned int borderwidth ;
  char *display_name = NULL;
  char *wname = "wmfan";
  XGCValues gcv;
  unsigned long gcm;
  XEvent Event;
  XTextProperty name;
  XClassHint classHint;
  Pixmap pixmask;
  Atom _XA_WM_DELETE_WINDOW 	= None;
  Geometry = "";
  mywmhints.initial_state = NormalState;

  /* Parse command line options */
  ProgName = argv[0];

  for(i=1;i<argc;i++) {
    char *arg= argv[i];

    if (arg[0] == '-') {
      switch(arg[1]) {
      case 'u':
	if(++i >=argc) usage();
	sscanf(argv[i], "%d", &updatespeed);
	continue;
      case 'e':
	if(++i >=argc) usage();
	strcpy(&Execute[0], argv[i]);
	strcat(&Execute[0], " &");
	continue;
      case 's':
	ONLYSHAPE=1;
	continue;
      case 'p':
	if(++i >=argc) usage();
	Geometry = argv[i];
	continue;
      case 'i':
	mywmhints.initial_state = IconicState;
	continue;
      case 'w':
	mywmhints.initial_state = WithdrawnState;
	continue;
      case 'l':
	if(++i >=argc) usage();
	LedColor = argv[i];
	continue;
      case 'v':
	fprintf(stdout, "\nwmfan version: %i.%i.%i\n", major_VER, minor_VER, patch_VER);
	if(argc == 2) exit(0);
	continue;
      default:
	usage();
      }
    }
    else
      {
        fprintf(stderr, "\nInvalid argument: %s\n", arg);
        usage();
      }
  }

  /* Open the display */
  if (!(dpy = XOpenDisplay(display_name)))
    {
      fprintf(stderr,"wmfan: can't open display %s\n",
	      XDisplayName(display_name));
      exit (1);
    }

  screen= DefaultScreen(dpy);
  Root = RootWindow(dpy, screen);
  d_depth = DefaultDepth(dpy, screen);
  x_fd = XConnectionNumber(dpy);
  _XA_WM_DELETE_WINDOW = XInternAtom (dpy, "WM_DELETE_WINDOW", False);

  /* Convert XPM Data to XImage */
  GetXPM();

  /* Create a window to hold the banner */
  mysizehints.flags= USSize|USPosition;
  mysizehints.x = 0;
  mysizehints.y = 0;

  back_pix = GetColor("white");
  fore_pix = GetColor("black");

  XWMGeometry(dpy, screen, Geometry, NULL, (borderwidth =1), &mysizehints,
	      &mysizehints.x,&mysizehints.y,&mysizehints.width,&mysizehints.height, &i);

  mysizehints.width = wmfan.attributes.width;
  mysizehints.height= wmfan.attributes.height;

  win = XCreateSimpleWindow(dpy,Root,mysizehints.x,mysizehints.y,
			    mysizehints.width,mysizehints.height,
			    borderwidth,fore_pix,back_pix);
  iconwin = XCreateSimpleWindow(dpy,win,mysizehints.x,mysizehints.y,
				mysizehints.width,mysizehints.height,
				borderwidth,fore_pix,back_pix);

  /* activate hints */
  XSetWMNormalHints(dpy, win, &mysizehints);
  classHint.res_name =  "wmfan";
  classHint.res_class = "wmfan";
  XSetClassHint(dpy, win, &classHint);

  XSelectInput(dpy,win,MW_EVENTS);
  XSelectInput(dpy,iconwin,MW_EVENTS);
  XSetCommand(dpy,win,argv,argc);

  if (XStringListToTextProperty(&wname, 1, &name) ==0) {
    fprintf(stderr, "wmfan: can't allocate window name\n");
    exit(-1);
  }
  XSetWMName(dpy, win, &name);

  /* Create a GC for drawing */
  gcm = GCForeground|GCBackground|GCGraphicsExposures;
  gcv.foreground = fore_pix;
  gcv.background = back_pix;
  gcv.graphics_exposures = FALSE;
  NormalGC = XCreateGC(dpy, Root, gcm, &gcv);

  if (ONLYSHAPE) { /* try to make shaped window here */
    pixmask = XCreateBitmapFromData(dpy, win, (char *)mask2_bits, mask2_width,
				    mask2_height);
    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, pixmask, ShapeSet);
    XShapeCombineMask(dpy, iconwin, ShapeBounding, 0, 0, pixmask, ShapeSet);
  }

  mywmhints.icon_window = iconwin;
  mywmhints.icon_x = mysizehints.x;
  mywmhints.icon_y = mysizehints.y;
  mywmhints.window_group = win;
  mywmhints.flags = StateHint | IconWindowHint | IconPositionHint
      | WindowGroupHint;
  XSetWMHints(dpy, win, &mywmhints);
  XSetWMProtocols (dpy, win, &_XA_WM_DELETE_WINDOW, 1);

  XMapWindow(dpy,win);
  InitLoad();
  InsertLoad();
  RedrawWindow(&visible);
  while(1)
    {
      if (actualtime != time(0))
	{
	  actualtime = time(0);

	  if(actualtime % updatespeed == 0)
	    InsertLoad();

	  RedrawWindow(&visible);
	}

      /* read a packet */
      while (XPending(dpy))
	{
	  XNextEvent(dpy,&Event);
	  switch(Event.type)
	    {
	    case Expose:
	      if(Event.xexpose.count == 0 )
		RedrawWindow(&visible);
	      break;
	    case ButtonPress:
	      ExecuteExternal();
	      break;
	    case ClientMessage:
    	      if ((Event.xclient.format != 32) ||
		  ((Atom)Event.xclient.data.l[0] != _XA_WM_DELETE_WINDOW))
		break;
	    case DestroyNotify:
	      XFreeGC(dpy, NormalGC);
	      XDestroyWindow(dpy, iconwin);
              XDestroyWindow(dpy, win);
              XCloseDisplay(dpy);
	      exit(0);
	      break ;
	    default:
	      break;
	    }
	}
      XFlush(dpy);
#ifdef SYSV
      poll((struct poll *) 0, (size_t) 0, 50);
#else
      {
        struct timespec ts;

        ts.tv_sec = 0;
        ts.tv_nsec = 50000000L;        /* 5/100 sec */
        nanosleep(&ts, NULL);
      }
#endif
    }
  return 0;
}

/*****************************************************************************/
void nocolor(char *a, char *b)
{
 fprintf(stderr,"wmfan: can't %s %s\n", a,b);
}

/*****************************************************************************/
/* convert the XPMIcons to XImage */
void GetXPM(void)
{
  static char **alt_xpm;
  XColor col;
  XWindowAttributes attributes;
  int ret;
  char tempc1[12],tempc2[12],tempc3[12];
  float colr,colg,colb;

  alt_xpm =ONLYSHAPE ? mask_xpm : back_xpm;

  /* for the colormap */
  XGetWindowAttributes(dpy,Root,&attributes);

  /* get user-defined color or validate the default */
  if (!XParseColor (dpy, attributes.colormap, LedColor, &col))
    {
      nocolor("parse",LedColor);
    }
  else
    {
      /* scale down the Xcolor values */
      colr = col.red   / 257;
      colg = col.green / 257;
      colb = col.blue  / 257;
      /* the brightest color */
      sprintf(tempc1, "S c #%.2x%.2x%.2x", (int)colr, (int)colg, (int)colb);
      back_xpm[47] = tempc1;

      /* make medium color */
      colr = (colr /100) *89;
      colg = (colg /100) *89;
      colb = (colb /100) *89;
      sprintf(tempc2, "R c #%.2x%.2x%.2x", (int)colr, (int)colg, (int)colb);
      back_xpm[46] = tempc2;

      /* make darkest color */
      colr = (colr /100) *89;
      colg = (colg /100) *89;
      colb = (colb /100) *89;
      sprintf(tempc3, "Q c #%.2x%.2x%.2x", (int)colr, (int)colg, (int)colb);
      back_xpm[45] = tempc3;
    }

  wmfan.attributes.valuemask |= (XpmReturnPixels | XpmReturnExtensions);
  ret = XpmCreatePixmapFromData(dpy, Root, alt_xpm, &wmfan.pixmap,
				&wmfan.mask, &wmfan.attributes);
  if(ret != XpmSuccess)
    {fprintf(stderr, "%s\n", ERR_colorcells);exit(1);}

  visible.attributes.valuemask |= (XpmReturnPixels | XpmReturnExtensions);
  ret = XpmCreatePixmapFromData(dpy, Root, back_xpm, &visible.pixmap,
				&visible.mask, &visible.attributes);
  if(ret != XpmSuccess)
    {fprintf(stderr, "%s\n", ERR_colorcells);exit(1);}

}

/*****************************************************************************/
/* Removes expose events for a specific window from the queue */
int flush_expose (Window w)
{
  XEvent dummy;
  int i=0;

  while (XCheckTypedWindowEvent (dpy, w, Expose, &dummy))i++;
  return i;
}

/*****************************************************************************/
/* Draws the icon window */
void RedrawWindow( XpmIcon *v)
{
  flush_expose (iconwin);
  XCopyArea(dpy,v->pixmap,iconwin,NormalGC,
	    0,0,v->attributes.width, v->attributes.height,0,0);
  flush_expose (win);
  XCopyArea(dpy,v->pixmap,win,NormalGC,
	    0,0,v->attributes.width, v->attributes.height,0,0);

}

/*****************************************************************************/
Pixel GetColor(char *name)
{
  XColor color;
  XWindowAttributes attributes;

  XGetWindowAttributes(dpy,Root,&attributes);
  color.pixel = 0;
   if (!XParseColor (dpy, attributes.colormap, name, &color))
     {
       nocolor("parse",name);
     }
   else if(!XAllocColor (dpy, attributes.colormap, &color))
     {
       nocolor("alloc",name);
     }
  return color.pixel;
}

/*****************************************************************************/
void InitLoad()
{
  /* Save the 4 base colors in wmfan */
  XCopyArea(dpy, visible.pixmap, wmfan.pixmap, NormalGC,
            6,6,3,52, Shape(6), Shape(6));

  /* Copy the base panel to visible */
  XCopyArea(dpy, wmfan.pixmap, visible.pixmap, NormalGC,
	    0,0,mysizehints.width, mysizehints.height, 0 ,0);

  /* Remove the 4 base colors from visible */
  XCopyArea(dpy, visible.pixmap, visible.pixmap, NormalGC,
	    Shape(9),Shape(6),3,52, Shape(6), Shape(6));
}

static char *
skip_token(const char *p)
{
    while (isspace(*p)) p++;
    while (*p && !isspace(*p)) p++;
    return (char *)p;
}

void GetLoad(int Maximum, int *usr)
{
  char buffer[100];/*[4096+1];*/
  int fd, len;
  int total;
  char *p;

  fd = open("/sys/class/hwmon/hwmon3/fan1_input", O_RDONLY);
  len = read(fd, buffer, sizeof(buffer)-1);
  close(fd);
  buffer[len] = '\0';

  /*
  p = skip_token(buffer);		 "cpu" */
  p = buffer;

  cp_time[0] = strtoul(p, &p, 0);	/* user   */
  // if( (*usr  = cp_time[0] - last[0]) < 0 ) *usr = 0 ;
  *usr = cp_time[0];
  total = 5000;
  last[0] = cp_time[0];

  *usr = rint(Maximum * (float)(*usr)/total);

  //printf("usr = %d\n", *usr);
}

void InsertLoad()
{
  int UserTime, act, free;
  GetLoad( 52, &UserTime);

  /* Move the area */
    XCopyArea(dpy, visible.pixmap, visible.pixmap, NormalGC,
        	Shape(7), Shape(6), 51, 52, Shape(6), Shape(6));


    /* User Time */
    act = 58 - UserTime;
    free = 52 - UserTime;

    //printf("UserTime = %d\n", UserTime);
    //printf("act = %d\n", act);
    //printf("free = %d\n", free);

    if(UserTime > 0)
      XCopyArea(dpy, wmfan.pixmap, visible.pixmap, NormalGC,
        Shape(6), Shape(6), 1, UserTime, Shape(57), Shape(act));

    if(free > 0)
      XCopyArea(dpy, wmfan.pixmap, visible.pixmap, NormalGC,
		Shape(9), Shape(6), 1, free, Shape(57), Shape(6));
}
