/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is ``thrashview''
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corp.  Portions created by the Initial Developer are
 * Copyright (C) 2001 the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Waterson <waterson@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * ``thrashview'' is a program that reads a binary stream of addresses
 * from stdin and displays the pattern graphically in a window.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <X11/Xlib.h>

#define GET_DISPLAY_FD(display_) ConnectionNumber(display_)

static Display *display;
static Window window;
static GC gc;
static XColor colors[256];
const unsigned int cellsize = 4;
static unsigned int width = 64 * cellsize;
static unsigned int height = 64 * cellsize;

static unsigned int minpage = static_cast<unsigned int>(-1);
static unsigned int maxpage = 0;
static unsigned char pages[64*64]; // should hold 16MB worth of code


/**
 * Create a simple window and the X objects we'll need to talk with it.
 */
static int
init()
{
    display = XOpenDisplay(0);
    if (! display)
        return 0;

    window =
        XCreateSimpleWindow(display,
                            RootWindow(display, 0),
                            1, 1, width, height,
                            0,
                            BlackPixel(display, 0),
                            BlackPixel(display, 0));

    if (! window)
        return 0;

    gc = XCreateGC(display, window, 0, 0);
    if (! gc)
        return 0;

    // Set up a grayscale
    const unsigned int ncolors = sizeof colors / sizeof colors[0];
    const unsigned short step = 65536 / ncolors;
    unsigned short brightness = 0;

    XColor *color = colors;
    XColor *last_color = colors + ncolors;
    for (; color < last_color; ++color, brightness += step) {
        color->red   = brightness;
        color->green = brightness;
        color->blue  = brightness;
        XAllocColor(display, DefaultColormap(display, 0), color);
    }

    // We want exposes and resizes.
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);

    XMapWindow(display, window);
    XFlush(display);

    return 1;
}

/**
 * Age pages that haven't been recently touched.
 */
static void
decay()
{
    unsigned char *page = pages;
    unsigned char *last_page = pages + maxpage - minpage;
    for (; page < last_page; ++page) {
        if (*page) {
            *page /= 8;
            *page *= 7;
        }
    }
}

/**
 * Blast the state of our pages to the screen.
 */
static int
handle_expose(const XExposeEvent& event)
{
    //printf("handle_expose(%d, %d, %d, %d)\n", event.x, event.y, event.width, event.height);

    int i = event.x / cellsize;
    int imost = i + event.width / cellsize + 1;

    int j = event.y / cellsize;
    int jmost = j + event.height / cellsize + 1;

    unsigned char *last_cell = pages + maxpage - minpage;
    unsigned char *row = pages + j;
    for (int y = j * cellsize, ymost = jmost * cellsize;
         y < ymost;
         y += cellsize, row += width / cellsize) {
        unsigned char *cell = row + i;
        for (int x = i * cellsize, xmost = imost * cellsize;
             x < xmost;
             x += cellsize, ++cell) {
            unsigned int pixel = (cell <= last_cell) ? colors[*cell].pixel : colors[0].pixel;
            XSetForeground(display, gc, pixel);
            XFillRectangle(display, window, gc, x, y, cellsize - 1, cellsize - 1);
        }
    }

    XFlush(display);

    return 1;
}

/**
 * Invalidate the entire window.
 */
static void
invalidate_window()
{
    XExposeEvent event;
    event.x = event.y = 0;
    event.width = width;
    event.height = height;

    handle_expose(event);
}

/**
 * Handle a configure event.
 */
static int
handle_configure(const XConfigureEvent& event)
{
    //printf("handle_resize(%d, %d)\n", event.width, event.height);
    width = event.width - event.width % cellsize;
    height = event.height;
    return 1;
}

/**
 * Filter to select any message.
 */
static Bool
any_event(Display *display, XEvent *event, XPointer arg)
{
    return 1;
}

/**
 * An X event occurred. Process it and flush the queue.
 */
static int
handle_xevents()
{
    int ok;
    
    XEvent event;
    XNextEvent(display, &event);
    do {
        switch (event.type) {
        case Expose:
            ok = handle_expose(reinterpret_cast<const XExposeEvent&>(event));
            break;

        case ConfigureNotify:
            ok = handle_configure(reinterpret_cast<const XConfigureEvent&>(event));
            break;

        default:
            ok = 1;
        }
    } while (ok && XCheckIfEvent(display, &event, any_event, 0));

    return ok;
}

/**
 * Read address data from stdin.
 */
static int
read_addrs()
{
    unsigned int buf[1024];
    ssize_t count;
    while ((count = read(0, buf, sizeof buf)) > 0) {
        if (count % sizeof(unsigned int))
            fprintf(stderr, "truncating unaligned read\n");

        count /= sizeof buf[0];

        unsigned int *addr = reinterpret_cast<unsigned int *>(buf);
        unsigned int *last_addr = addr + count;

        for (; addr < last_addr; ++addr) {
            // map the address to a page
            unsigned int page = *addr / 4096;

            if (page < minpage) {
                if (maxpage) {
                    // Everything needs to shift.
                    unsigned int shift = minpage - page;
                    memmove(pages + shift, pages, maxpage - minpage);
                    memset(pages, 0, shift);
                }
                minpage = page;
            }

            if (page > maxpage)
                maxpage = page;

            page -= minpage;
            pages[page] = 255;
        }
    }

    if (count < 0 && errno != EWOULDBLOCK) {
        perror("read");
        return 0;
    }

    return 1;
}

/**
 * Tear down our window and stuff.
 */
static void
finish()
{
    if (window) {
        XUnmapWindow(display, window);
        XDestroyWindow(display, window);
    }

    if (display)
        XCloseDisplay(display);
}

/**
 * Program starts here.
 */
int
main(int argc, char *argv[])
{
    if (init()) {
        fcntl(0, F_SETFL, O_NONBLOCK);

        struct timeval last;
        gettimeofday(&last, 0);

        int ok;

        do {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            FD_SET(GET_DISPLAY_FD(display), &fds);

            struct timeval tv;
            tv.tv_sec  = 1;
            tv.tv_usec = 0;

            ok = select(GET_DISPLAY_FD(display) + 1, &fds, 0, 0, &tv);
            if (ok < 0)
                break;

            struct timeval now;
            gettimeofday(&now, 0);

            if (maxpage && (now.tv_sec != last.tv_sec)) {
                last = now;
                decay();
                invalidate_window();
            }
            else if (now.tv_usec - last.tv_usec > 100000) {
                last.tv_usec = now.tv_usec;
                invalidate_window();
            }

            ok = 1;

            if (FD_ISSET(GET_DISPLAY_FD(display), &fds))
                ok = handle_xevents();

            if (FD_ISSET(STDIN_FILENO, &fds))
                ok = read_addrs();
        } while (ok);
    }

    finish();

    return 0;
}
