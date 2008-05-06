/*
 * Copyright (c) 2003-2008 by FlashCode <flashcode@flashtux.org>
 * See README for License detail, AUTHORS for developers list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* xfer-buffer.c: display xfer list on xfer buffer */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../weechat-plugin.h"
#include "xfer.h"
#include "xfer-config.h"
#include "xfer-network.h"


struct t_gui_buffer *xfer_buffer = NULL;
int xfer_buffer_selected_line = 0;


/*
 * xfer_buffer_refresh: update a xfer in buffer and update hotlist for xfer buffer
 */

void
xfer_buffer_refresh (char *hotlist)
{
    struct t_xfer *ptr_xfer, *xfer_selected;
    char str_color[256], status[64], date[128], *progress_bar, format[128];
    char format_per_sec[128], bytes_per_sec[256], eta[128];
    int i, length, line, progress_bar_size, num_bars, num_unit;
    int num_unit_per_sec;
    unsigned long pct_complete;
    char *unit_name[] = { N_("bytes"), N_("KB"), N_("MB"), N_("GB") };
    char *unit_format[] = { "%.0f", "%.1f", "%.02f", "%.02f" };
    float unit_divide[] = { 1, 1024, 1024*1024, 1024*1024*1024 };
    struct tm *date_tmp;
    
    if (xfer_buffer)
    {
        weechat_buffer_clear (xfer_buffer);
        line = 0;
        xfer_selected = xfer_search_by_number (xfer_buffer_selected_line);
        if (xfer_selected)
        {
            weechat_printf_y (xfer_buffer, 0,
                              "%sActions (letter+enter):%s%s%s%s%s%s",
                              weechat_color("green"),
                              weechat_color("lightgreen"),
                              /* accept */
                              (XFER_IS_RECV(xfer_selected->type)
                               && (xfer_selected->status == XFER_STATUS_WAITING)) ?
                              _("  [A] Accept") : "",
                              /* cancel */
                              (!XFER_HAS_ENDED(xfer_selected->status)) ?
                              _("  [C] Cancel") : "",
                              /* remove */
                              (XFER_HAS_ENDED(xfer_selected->status)) ?
                              _("  [R] Remove") : "",
                              /* purge old */
                              _("  [P] Purge finished"),
                              /* quit */
                              _("  [Q] Close xfer list"));
        }
        for (ptr_xfer = xfer_list; ptr_xfer; ptr_xfer = ptr_xfer->next_xfer)
        {
            snprintf (str_color, sizeof (str_color),
                      "%s,%s",
                      (line == xfer_buffer_selected_line) ?
                      weechat_config_string (xfer_config_color_text_selected) :
                      weechat_config_string (xfer_config_color_text),
                      weechat_config_string (xfer_config_color_text_bg));
            
            /* display first line with remote nick and filename */
            weechat_printf_y (xfer_buffer, (line * 2) + 2,
                              "%s%s%-24s %s%s%s",
                              weechat_color(str_color),
                              (line == xfer_buffer_selected_line) ?
                              "*** " : "    ",
                              ptr_xfer->remote_nick,
                              (XFER_IS_FILE(ptr_xfer->type)) ? "\"" : "",
                              (XFER_IS_FILE(ptr_xfer->type)) ?
                              ptr_xfer->filename : _("xfer chat"),
                              (XFER_IS_FILE(ptr_xfer->type)) ? "\"" : "");

            snprintf (status, sizeof (status),
                      "%s", _(xfer_status_string[ptr_xfer->status]));
            length = weechat_utf8_strlen_screen (status);
            if (length < 20)
            {
                for (i = 0; i < 20 - length; i++)
                {
                    strcat (status, " ");
                }
            }
            
            if (XFER_IS_CHAT(ptr_xfer->type))
            {
                /* display second line for chat with status and date */
                date_tmp = localtime (&(ptr_xfer->start_time));
                strftime (date, sizeof (date),
                          "%a, %d %b %Y %H:%M:%S", date_tmp);
                weechat_printf_y (xfer_buffer, (line * 2) + 3,
                                  "%s%s%s %s%s%s%s%s",
                                  weechat_color(str_color),
                                  (line == xfer_buffer_selected_line) ?
                                  "*** " : "    ",
                                  (XFER_IS_SEND(ptr_xfer->type)) ?
                                  "<<--" : "-->>",
                                  weechat_color(weechat_config_string (xfer_config_color_status[ptr_xfer->status])),
                                  status,
                                  weechat_color ("reset"),
                                  weechat_color (str_color),
                                  date);
            }
            else
            {
                /* build progress bar */
                progress_bar = NULL;
                progress_bar_size = weechat_config_integer (xfer_config_look_progress_bar_size);
                if (progress_bar_size > 0)
                {
                    progress_bar = malloc (1 + progress_bar_size + 1 + 1 + 1);
                    strcpy (progress_bar, "[");
                    if (ptr_xfer->size == 0)
                    {
                        if (ptr_xfer->status == XFER_STATUS_DONE)
                            num_bars = progress_bar_size;
                        else
                            num_bars = 0;
                    }
                    else
                        num_bars = (int)(((float)(ptr_xfer->pos)/(float)(ptr_xfer->size)) * (float)progress_bar_size);
                    for (i = 0; i < num_bars - 1; i++)
                    {
                        strcat (progress_bar, "=");
                    }
                    if (num_bars > 0)
                        strcat (progress_bar, ">");
                    for (i = 0; i < progress_bar_size - num_bars; i++)
                    {
                        strcat (progress_bar, " ");
                    }
                    strcat (progress_bar, "] ");
                }
                
                /* computes pourcentage */
                if (ptr_xfer->size < 1024*10)
                    num_unit = 0;
                else if (ptr_xfer->size < 1024*1024)
                    num_unit = 1;
                else if (ptr_xfer->size < 1024*1024*1024)
                    num_unit = 2;
                else
                    num_unit = 3;
                if (ptr_xfer->size == 0)
                {
                    if (ptr_xfer->status == XFER_STATUS_DONE)
                        pct_complete = 100;
                    else
                        pct_complete = 0;
                }
                else
                    pct_complete = (unsigned long)(((float)(ptr_xfer->pos)/(float)(ptr_xfer->size)) * 100);
                
                snprintf (format, sizeof (format),
                          "%%s%%s%%s %%s%%s%%s%%s%%3lu%%%%   %s %%s / %s %%s  (%%s%%s)",
                          unit_format[num_unit],
                          unit_format[num_unit]);
                
                /* bytes per second */
                bytes_per_sec[0] = '\0';
                if (ptr_xfer->bytes_per_sec < 1024*10)
                    num_unit_per_sec = 0;
                else if (ptr_xfer->bytes_per_sec < 1024*1024)
                    num_unit_per_sec = 1;
                else if (ptr_xfer->bytes_per_sec < 1024*1024*1024)
                    num_unit_per_sec = 2;
                else
                    num_unit_per_sec = 3;
                snprintf (format_per_sec, sizeof (format_per_sec),
                          "%s %%s/s",
                          unit_format[num_unit_per_sec]);
                snprintf (bytes_per_sec, sizeof (bytes_per_sec),
                          format_per_sec,
                          ((float)ptr_xfer->bytes_per_sec) / ((float)(unit_divide[num_unit_per_sec])),
                          _(unit_name[num_unit_per_sec]));
                
                /* ETA */
                eta[0] = '\0';
                if (ptr_xfer->status == XFER_STATUS_ACTIVE)
                {
                    snprintf (eta, sizeof (eta),
                              "%s: %.2lu:%.2lu:%.2lu - ",
                              _("ETA"),
                              ptr_xfer->eta / 3600,
                              (ptr_xfer->eta / 60) % 60,
                              ptr_xfer->eta % 60);
                }
                
                /* display second line for file with status, progress bar and estimated time */
                weechat_printf_y (xfer_buffer, (line * 2) + 3,
                                  format,
                                  weechat_color(str_color),
                                  (line == xfer_buffer_selected_line) ?
                                  "*** " : "    ",
                                  (XFER_IS_SEND(ptr_xfer->type)) ?
                                  "<<--" : "-->>",
                                  weechat_color(weechat_config_string (xfer_config_color_status[ptr_xfer->status])),
                                  status,
                                  weechat_color (str_color),
                                  (progress_bar) ? progress_bar : "",
                                  pct_complete,
                                  ((float)(ptr_xfer->pos)) / unit_divide[num_unit],
                                  _(unit_name[num_unit]),
                                  ((float)(ptr_xfer->size)) / unit_divide[num_unit],
                                  _(unit_name[num_unit]),
                                  eta,
                                  bytes_per_sec);
            }
            line++;
        }
        weechat_buffer_set (xfer_buffer, "hotlist", hotlist);
    }
}

/*
 * xfer_buffer_input_cb: callback called when user send data to xfer list
 *                       buffer
 */

int
xfer_buffer_input_cb (void *data, struct t_gui_buffer *buffer,
                      char *input_data)
{
    struct t_xfer *xfer, *ptr_xfer, *next_xfer;
    
    /* make C compiler happy */
    (void) data;
    
    xfer = xfer_search_by_number (xfer_buffer_selected_line);
    
    /* accept xfer */
    if (weechat_strcasecmp (input_data, "a") == 0)
    {
        if (xfer && XFER_IS_RECV(xfer->type)
            && (xfer->status == XFER_STATUS_WAITING))
        {
            xfer_network_accept (xfer);
        }
    }
    /* cancel xfer */
    else if (weechat_strcasecmp (input_data, "c") == 0)
    {
        if (xfer && !XFER_HAS_ENDED(xfer->status))
        {
            xfer_close (xfer, XFER_STATUS_ABORTED);
            xfer_buffer_refresh (WEECHAT_HOTLIST_MESSAGE);
        }
    }
    /* purge old xfer */
    else if (weechat_strcasecmp (input_data, "p") == 0)
    {
        ptr_xfer = xfer_list;
        while (ptr_xfer)
        {
            next_xfer = ptr_xfer->next_xfer;
            if (XFER_HAS_ENDED(ptr_xfer->status))
                xfer_free (ptr_xfer);
            ptr_xfer = next_xfer;
        }
        xfer_buffer_refresh (WEECHAT_HOTLIST_MESSAGE);
    }
    /* quit xfer buffer (close it) */
    else if (weechat_strcasecmp (input_data, "q") == 0)
    {
        weechat_buffer_close (buffer, 1);
    }
    /* remove xfer */
    else if (weechat_strcasecmp (input_data, "r") == 0)
    {
        if (xfer && XFER_HAS_ENDED(xfer->status))
        {
            xfer_free (xfer);
            xfer_buffer_refresh (WEECHAT_HOTLIST_MESSAGE);
        }
    }
    
    return WEECHAT_RC_OK;
}

/*
 * xfer_buffer_close_cb: callback called when xfer buffer is closed
 */

int
xfer_buffer_close_cb (void *data, struct t_gui_buffer *buffer)
{
    /* make C compiler happy */
    (void) data;
    (void) buffer;
    
    xfer_buffer = NULL;
    
    return WEECHAT_RC_OK;
}

/*
 * xfer_buffer_open: open xfer buffer (to display list of xfer)
 */

void
xfer_buffer_open ()
{
    if (!xfer_buffer)
    {
        xfer_buffer = weechat_buffer_new ("xfer", "list",
                                          &xfer_buffer_input_cb, NULL,
                                          &xfer_buffer_close_cb, NULL);
        
        /* failed to create buffer ? then exit */
        if (!xfer_buffer)
            return;
        
        weechat_buffer_set (xfer_buffer, "type", "free");
        weechat_buffer_set (xfer_buffer, "title", _("Xfer list"));
        weechat_buffer_set (xfer_buffer, "key_bind_meta2-A", "/xfer up");
        weechat_buffer_set (xfer_buffer, "key_bind_meta2-B", "/xfer down");
    }
}
