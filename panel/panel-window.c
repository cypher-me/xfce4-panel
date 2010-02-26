/* $Id$ */
/*
 * Copyright (C) 2009 Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include <exo/exo.h>
#include <common/panel-private.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4panel/xfce-panel-plugin-provider.h>
#include <panel/panel-base-window.h>
#include <panel/panel-window.h>
#include <panel/panel-item-dialog.h>
#include <panel/panel-preferences-dialog.h>
#include <panel/panel-dialogs.h>
#include <panel/panel-dbus-service.h>
#include <panel/panel-plugin-external.h>



#define SNAP_DISTANCE      (10)
#define SET_OLD_WM_STRUTS  (FALSE)
#define STRUTS_DEBUGGING   (FALSE)
#define POPUP_DELAY        (225)
#define POPDOWN_DELAY      (350)
#define HANDLE_SPACING     (4)
#define HANDLE_DOTS        (2)
#define HANDLE_PIXELS      (2)
#define HANDLE_PIXEL_SPACE (1)
#define HANDLE_SIZE        (HANDLE_DOTS * (HANDLE_PIXELS + \
                            HANDLE_PIXEL_SPACE) - HANDLE_PIXEL_SPACE)
#define HANDLE_SIZE_TOTAL  (2 * HANDLE_SPACING + HANDLE_SIZE)



typedef enum _StrutsEgde    StrutsEgde;
typedef enum _AutohideState AutohideState;
typedef enum _SnapPosition  SnapPosition;



static void         panel_window_get_property               (GObject          *object,
                                                             guint             prop_id,
                                                             GValue           *value,
                                                             GParamSpec       *pspec);
static void         panel_window_set_property               (GObject          *object,
                                                             guint             prop_id,
                                                             const GValue     *value,
                                                             GParamSpec       *pspec);
static void         panel_window_finalize                   (GObject          *object);
static gboolean     panel_window_expose_event               (GtkWidget        *widget,
                                                             GdkEventExpose   *event);
static gboolean     panel_window_enter_notify_event         (GtkWidget        *widget,
                                                             GdkEventCrossing *event);
static gboolean     panel_window_leave_notify_event         (GtkWidget        *widget,
                                                             GdkEventCrossing *event);
static gboolean     panel_window_drag_motion                (GtkWidget        *widget,
                                                             GdkDragContext   *context,
                                                             gint              x,
                                                             gint              y,
                                                             guint             drag_time);
static void         panel_window_drag_leave                 (GtkWidget        *widget,
                                                             GdkDragContext   *context,
                                                             guint             drag_time);
static gboolean     panel_window_motion_notify_event        (GtkWidget        *widget,
                                                             GdkEventMotion   *event);
static gboolean     panel_window_button_press_event         (GtkWidget        *widget,
                                                             GdkEventButton   *event);
static gboolean     panel_window_button_release_event       (GtkWidget        *widget,
                                                             GdkEventButton   *event);
static void         panel_window_grab_notify                (GtkWidget        *widget,
                                                             gboolean         was_grabbed);
static void         panel_window_size_request               (GtkWidget        *widget,
                                                             GtkRequisition   *requisition);
static void         panel_window_size_allocate              (GtkWidget        *widget,
                                                             GtkAllocation    *alloc);
static void         panel_window_size_allocate_set_xy       (PanelWindow      *window,
                                                             gint              window_width,
                                                             gint              window_height,
                                                             gint             *return_x,
                                                             gint             *return_y);
static void         panel_window_screen_changed             (GtkWidget        *widget,
                                                             GdkScreen        *previous_screen);
static StrutsEgde   panel_window_screen_struts_edge         (PanelWindow      *window);
static void         panel_window_screen_struts_set          (PanelWindow      *window);
static void         panel_window_screen_force_update        (PanelWindow      *window);
static void         panel_window_screen_update_borders      (PanelWindow      *window);
static SnapPosition panel_window_snap_position              (PanelWindow      *window);
static void         panel_window_screen_layout_changed      (GdkScreen        *screen,
                                                             PanelWindow      *window);
static void         panel_window_autohide_queue             (PanelWindow      *window,
                                                             AutohideState     new_state);
static void         panel_window_set_autohide               (PanelWindow      *window,
                                                             gboolean          autohide);
static void         panel_window_menu_popup                 (PanelWindow      *window,
                                                             guint32           event_time);
static void         panel_window_set_plugin_orientation     (GtkWidget        *widget,
                                                             gpointer          user_data);
static void         panel_window_set_plugin_size            (GtkWidget        *widget,
                                                             gpointer          user_data);
static void         panel_window_set_plugin_screen_position (GtkWidget        *widget,
                                                             gpointer          user_data);



enum
{
  PROP_0,
  PROP_HORIZONTAL,
  PROP_SIZE,
  PROP_LENGTH,
  PROP_LOCKED,
  PROP_AUTOHIDE,
  PROP_SPAN_MONITORS,
  PROP_OUTPUT_NAME,
  PROP_POSITION
};

enum _AutohideState
{
  AUTOHIDE_DISABLED = 0, /* autohide is disabled */
  AUTOHIDE_VISIBLE,      /* visible */
  AUTOHIDE_POPDOWN,      /* visible, but hide timeout is running */
  AUTOHIDE_POPDOWN_SLOW, /* same as popdown, but timeout is 4x longer */
  AUTOHIDE_HIDDEN,       /* invisible */
  AUTOHIDE_POPUP,        /* invisible, but show timeout is running */
  AUTOHIDE_BLOCKED       /* autohide is enabled, but blocked */
};

enum _SnapPosition
{
  /* no snapping */
  SNAP_POSITION_NONE, /* snapping */

  /* right edge */
  SNAP_POSITION_E,    /* right */
  SNAP_POSITION_NE,   /* top right */
  SNAP_POSITION_EC,   /* right center */
  SNAP_POSITION_SE,   /* bottom right */

  /* left edge */
  SNAP_POSITION_W,    /* left */
  SNAP_POSITION_NW,   /* top left */
  SNAP_POSITION_WC,   /* left center */
  SNAP_POSITION_SW,   /* bottom left */

  /* top and bottom */
  SNAP_POSITION_NC,   /* top center */
  SNAP_POSITION_SC,   /* bottom center */
  SNAP_POSITION_N,    /* top */
  SNAP_POSITION_S,    /* bottom */
};

enum
{
  EDGE_GRAVITY_NONE   = 0,
  EDGE_GRAVITY_START  = (SNAP_POSITION_NE - SNAP_POSITION_E),
  EDGE_GRAVITY_CENTER = (SNAP_POSITION_EC - SNAP_POSITION_E),
  EDGE_GRAVITY_END    = (SNAP_POSITION_SE - SNAP_POSITION_E)
};

enum _StrutsEgde
{
  STRUTS_EDGE_NONE = 0,
  STRUTS_EDGE_LEFT,
  STRUTS_EDGE_RIGHT,
  STRUTS_EDGE_TOP,
  STRUTS_EDGE_BOTTOM
};

enum
{
  STRUT_LEFT = 0,
  STRUT_RIGHT,
  STRUT_TOP,
  STRUT_BOTTOM,
  STRUT_LEFT_START_Y,
  STRUT_LEFT_END_Y,
  STRUT_RIGHT_START_Y,
  STRUT_RIGHT_END_Y,
  STRUT_TOP_START_X,
  STRUT_TOP_END_X,
  STRUT_BOTTOM_START_X,
  STRUT_BOTTOM_END_X,
  N_STRUTS
};

struct _PanelWindowClass
{
  PanelBaseWindowClass __parent__;
};

struct _PanelWindow
{
  PanelBaseWindow      __parent__;

  /* screen and working area of this panel */
  GdkScreen           *screen;
  GdkRectangle         area;

  /* struts information */
  StrutsEgde           struts_edge;
  gulong               struts[N_STRUTS];

  /* window positioning */
  guint                size;
  gdouble              length;
  guint                horizontal : 1;
  SnapPosition         snap_position;
  guint                span_monitors : 1;
  gchar               *output_name;

  /* allocated position of the panel */
  GdkRectangle         alloc;

  /* autohiding */
  GtkWidget           *autohide_window;
  AutohideState        autohide_state;
  guint                autohide_timeout_id;
  gint                 autohide_block;

  /* whether the window is locked */
  guint                locked : 1;

  /* window base point */
  gint                 base_x;
  gint                 base_y;

  /* window drag information */
  guint32              grab_time;
  gint                 grab_x;
  gint                 grab_y;
};



static GdkAtom cardinal_atom = 0;
#if SET_OLD_WM_STRUTS
static GdkAtom net_wm_strut_atom = 0;
#endif
static GdkAtom net_wm_strut_partial_atom = 0;



G_DEFINE_TYPE (PanelWindow, panel_window, PANEL_TYPE_BASE_WINDOW)



static void
panel_window_class_init (PanelWindowClass *klass)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *gtkwidget_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = panel_window_get_property;
  gobject_class->set_property = panel_window_set_property;
  gobject_class->finalize = panel_window_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->expose_event = panel_window_expose_event;
  gtkwidget_class->enter_notify_event = panel_window_enter_notify_event;
  gtkwidget_class->leave_notify_event = panel_window_leave_notify_event;
  gtkwidget_class->drag_motion = panel_window_drag_motion;
  gtkwidget_class->drag_leave = panel_window_drag_leave;
  gtkwidget_class->motion_notify_event = panel_window_motion_notify_event;
  gtkwidget_class->button_press_event = panel_window_button_press_event;
  gtkwidget_class->button_release_event = panel_window_button_release_event;
  gtkwidget_class->grab_notify = panel_window_grab_notify;
  gtkwidget_class->size_request = panel_window_size_request;
  gtkwidget_class->size_allocate = panel_window_size_allocate;
  gtkwidget_class->screen_changed = panel_window_screen_changed;

  g_object_class_install_property (gobject_class,
                                   PROP_HORIZONTAL,
                                   g_param_spec_boolean ("horizontal", NULL, NULL,
                                                         TRUE,
                                                         EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_SIZE,
                                   g_param_spec_uint ("size", NULL, NULL,
                                                      16, 128, 48,
                                                      EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_LENGTH,
                                   g_param_spec_uint ("length", NULL, NULL,
                                                      1, 100, 25,
                                                      EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_LOCKED,
                                   g_param_spec_boolean ("locked", NULL, NULL,
                                                         FALSE,
                                                         EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_AUTOHIDE,
                                   g_param_spec_boolean ("autohide", NULL, NULL,
                                                         FALSE,
                                                         EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_SPAN_MONITORS,
                                   g_param_spec_boolean ("span-monitors", NULL, NULL,
                                                         FALSE,
                                                         EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_OUTPUT_NAME,
                                   g_param_spec_string ("output-name", NULL, NULL,
                                                        NULL,
                                                        EXO_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_POSITION,
                                   g_param_spec_string ("position", NULL, NULL,
                                                        NULL,
                                                        EXO_PARAM_READWRITE));

  /* initialize the atoms */
  cardinal_atom = gdk_atom_intern_static_string ("CARDINAL");
  net_wm_strut_partial_atom = gdk_atom_intern_static_string ("_NET_WM_STRUT_PARTIAL");
#if SET_OLD_WM_STRUTS
  net_wm_strut_atom = gdk_atom_intern_static_string ("_NET_WM_STRUT");
#endif
}



static void
panel_window_init (PanelWindow *window)
{
  /* no resizable, so allocation will follow size request */
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  /* set additional events */
  gtk_widget_add_events (GTK_WIDGET (window), GDK_BUTTON_PRESS_MASK);

  /* create a 'fake' drop zone for autohide drag motion */
  gtk_drag_dest_set (GTK_WIDGET (window), 0, NULL, 0, 0);

  /* init vars */
  window->screen = NULL;
  window->struts_edge = STRUTS_EDGE_NONE;

  window->horizontal = TRUE;
  window->size = 30;
  window->length = 0.10;
  window->snap_position = SNAP_POSITION_NONE;
  window->span_monitors = FALSE;

  window->locked = FALSE;

  window->autohide_state = AUTOHIDE_DISABLED;
  window->autohide_timeout_id = 0;
  window->autohide_block = 0;

  window->base_x = -1;
  window->base_y = -1;

  window->grab_time = 0;
  window->grab_x = 0;
  window->grab_y = 0;

  /* set the screen */
  panel_window_screen_changed (GTK_WIDGET (window), NULL);

  /* watch changes in the compositing */
  g_signal_connect (G_OBJECT (window), "notify::composited",
      G_CALLBACK (panel_window_screen_force_update), NULL);
}



static void
panel_window_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PanelWindow *window = PANEL_WINDOW (object);
  gchar       *position;

  switch (prop_id)
    {
      case PROP_HORIZONTAL:
        g_value_set_boolean (value, window->horizontal);
        break;

      case PROP_SIZE:
        g_value_set_uint (value, window->size);
        break;

      case PROP_LENGTH:
        g_value_set_uint (value,  rint (window->length * 100.00));
        break;

      case PROP_LOCKED:
        g_value_set_boolean (value, window->locked);
        break;

      case PROP_AUTOHIDE:
        g_value_set_boolean (value, !!(window->autohide_state != AUTOHIDE_DISABLED));
        break;

      case PROP_SPAN_MONITORS:
        g_value_set_boolean (value, window->span_monitors);
        break;

      case PROP_OUTPUT_NAME:
        g_value_set_static_string (value, window->output_name);
        break;

      case PROP_POSITION:
        position = g_strdup_printf ("p=%d;x=%d;y=%d",
                                    window->snap_position,
                                    window->base_x,
                                    window->base_y);
        g_value_take_string (value, position);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}



static void
panel_window_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PanelWindow *window = PANEL_WINDOW (object);
  gboolean     val_bool;
  guint        val_uint;
  gdouble      val_double;
  const gchar *val_string;
  gboolean     update = FALSE;
  gint         x, y, snap_position;
  GtkWidget   *itembar;

  switch (prop_id)
    {
      case PROP_HORIZONTAL:
        val_bool = g_value_get_boolean (value);
        if (window->horizontal != val_bool)
          {
            window->horizontal = !!val_bool;
            panel_window_screen_layout_changed (window->screen, window);
          }

        /* send the new orientation to the panel plugins */
        itembar = gtk_bin_get_child (GTK_BIN (window));
        if (G_LIKELY (itembar != NULL))
          gtk_container_foreach (GTK_CONTAINER (itembar),
              panel_window_set_plugin_orientation, window);

        /* send the new screen position */
        if (G_LIKELY (itembar != NULL))
          gtk_container_foreach (GTK_CONTAINER (itembar),
              panel_window_set_plugin_screen_position, window);
        break;

      case PROP_SIZE:
        val_uint = g_value_get_uint (value);
        if (window->size != val_uint)
          {
            window->size = val_uint;
            gtk_widget_queue_resize (GTK_WIDGET (window));
          }

        /* send the new size to the panel plugins */
        itembar = gtk_bin_get_child (GTK_BIN (window));
        if (G_LIKELY (itembar != NULL))
          gtk_container_foreach (GTK_CONTAINER (itembar),
              panel_window_set_plugin_size, window);
        break;

      case PROP_LENGTH:
        val_double = g_value_get_uint (value) / 100.00;
        if (window->length != val_double)
          {
            if (window->length == 1.00 || val_double == 1.00)
              update = TRUE;

            window->length = val_double;

            if (update)
              panel_window_screen_update_borders (window);

            gtk_widget_queue_resize (GTK_WIDGET (window));
          }
        break;

      case PROP_LOCKED:
        val_bool = g_value_get_boolean (value);
        if (window->locked != val_bool)
          {
            window->locked = !!val_bool;
            gtk_widget_queue_resize (GTK_WIDGET (window));
          }
        break;

      case PROP_AUTOHIDE:
        panel_window_set_autohide (window, g_value_get_boolean (value));
        break;

      case PROP_SPAN_MONITORS:
        val_bool = g_value_get_boolean (value);
        if (window->span_monitors == val_bool)
          {
            window->span_monitors = !!val_bool;
            panel_window_screen_layout_changed (window->screen, window);
          }
        break;

      case PROP_OUTPUT_NAME:
        g_free (window->output_name);
        window->output_name = g_value_dup_string (value);
        panel_window_screen_layout_changed (window->screen, window);
        break;

      case PROP_POSITION:
        val_string = g_value_get_string (value);
        if (sscanf (val_string, "p=%d;x=%d;y=%d", &snap_position, &x, &y) == 3)
          {
            window->snap_position = CLAMP (snap_position, SNAP_POSITION_NONE, SNAP_POSITION_S);
            window->base_x = x;
            window->base_y = y;

            panel_window_screen_layout_changed (window->screen, window);

            /* send the new screen position to the panel plugins */
            itembar = gtk_bin_get_child (GTK_BIN (window));
            if (G_LIKELY (itembar != NULL))
              gtk_container_foreach (GTK_CONTAINER (itembar),
                  panel_window_set_plugin_screen_position, window);
          }
        else
          {
            g_message ("no valid position defined");
          }
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}



static void
panel_window_finalize (GObject *object)
{
  PanelWindow *window = PANEL_WINDOW (object);

  /* stop running autohide timeout */
  if (G_UNLIKELY (window->autohide_timeout_id != 0))
    g_source_remove (window->autohide_timeout_id);

  /* destroy the autohide window */
  if (window->autohide_window != NULL)
    gtk_widget_destroy (window->autohide_window);

  /* cleanup */
  g_free (window->output_name);

  (*G_OBJECT_CLASS (panel_window_parent_class)->finalize) (object);
}



static gboolean
panel_window_expose_event (GtkWidget      *widget,
                           GdkEventExpose *event)
{
  PanelWindow  *window = PANEL_WINDOW (widget);
  cairo_t      *cr;
  GdkColor     *color;
  guint         xx, yy, i;
  gint          xs, xe, ys, ye;
  gint          handle_w, handle_h;
  gdouble       alpha = 1.00;
  GtkWidget    *child;

  /* expose the background and borders */
  (*GTK_WIDGET_CLASS (panel_window_parent_class)->expose_event) (widget, event);

  if (window->locked || !GTK_WIDGET_DRAWABLE (widget))
    goto end;

  if (window->horizontal)
    {
      handle_h = window->alloc.height / 2;
      handle_w = HANDLE_SIZE;

      xs = HANDLE_SPACING + 1;
      xe = window->alloc.width - HANDLE_SIZE - HANDLE_SIZE;
      ys = ye = (window->alloc.height - handle_h) / 2;

      /* dirty check if we have to redraw the handles */
      if (event->area.x > xs + HANDLE_SIZE
          && event->area.x + event->area.width < xe)
        goto end;
    }
  else
    {
      handle_h = HANDLE_SIZE;
      handle_w = window->alloc.width / 2;

      xs = xe = (window->alloc.width - handle_w) / 2;
      ys = HANDLE_SPACING + 1;
      ye = window->alloc.height - HANDLE_SIZE - HANDLE_SIZE;

      /* dirty check if we have to redraw the handles */
      if (event->area.y > ys + HANDLE_SIZE
          && event->area.y + event->area.height < ye)
        goto end;
    }

  /* create cairo context and set some default properties */
  cr = gdk_cairo_create (widget->window);
  panel_return_val_if_fail (cr != NULL, FALSE);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

  /* clip the drawing area */
  gdk_cairo_rectangle (cr, &event->area);
  cairo_clip (cr);

  /* alpha color */
  if (PANEL_BASE_WINDOW (window)->is_composited)
    alpha = MAX (0.50, PANEL_BASE_WINDOW (window)->background_alpha);

  for (i = HANDLE_PIXELS; i >= HANDLE_PIXELS - 1; i--)
    {
      /* set the source color */
      if (i == HANDLE_PIXELS)
        color = &(widget->style->light[GTK_STATE_NORMAL]);
      else
        color = &(widget->style->dark[GTK_STATE_NORMAL]);
      panel_util_set_source_rgba (cr, color, alpha);

      /* draw the dots */
      for (xx = 0; xx < (guint) handle_w; xx += HANDLE_PIXELS + HANDLE_PIXEL_SPACE)
        for (yy = 0; yy < (guint) handle_h; yy += HANDLE_PIXELS + HANDLE_PIXEL_SPACE)
          {
            cairo_rectangle (cr, xs + xx, ys + yy, i, i);
            cairo_rectangle (cr, xe + xx, ye + yy, i, i);
          }

      /* fill the rectangles */
      cairo_fill (cr);
    }

  /* destroy cairo context */
  cairo_destroy (cr);

end:
  /* send the expose event to the child */
  child = gtk_bin_get_child (GTK_BIN (widget));
  if (G_LIKELY (child != NULL))
    gtk_container_propagate_expose (GTK_CONTAINER (widget), child, event);

  return FALSE;
}



static gboolean
panel_window_enter_notify_event (GtkWidget        *widget,
                                 GdkEventCrossing *event)
{
  PanelWindow *window = PANEL_WINDOW (widget);

  /* update autohide status */
  if (event->detail != GDK_NOTIFY_INFERIOR
      && window->autohide_state != AUTOHIDE_DISABLED)
    {
      /* stop a running autohide timeout */
      if (window->autohide_timeout_id != 0)
        g_source_remove (window->autohide_timeout_id);

      /* update autohide status */
      if (window->autohide_state == AUTOHIDE_POPDOWN)
        window->autohide_state = AUTOHIDE_VISIBLE;
    }

  return (*GTK_WIDGET_CLASS (panel_window_parent_class)->enter_notify_event) (widget, event);
}



static gboolean
panel_window_leave_notify_event (GtkWidget        *widget,
                                 GdkEventCrossing *event)
{
  PanelWindow *window = PANEL_WINDOW (widget);

   /* queue an autohide timeout if needed */
  if (event->detail != GDK_NOTIFY_INFERIOR
      && window->autohide_state != AUTOHIDE_DISABLED
      && window->autohide_state != AUTOHIDE_BLOCKED)
    panel_window_autohide_queue (window, AUTOHIDE_POPDOWN);

  return (*GTK_WIDGET_CLASS (panel_window_parent_class)->leave_notify_event) (widget, event);
}



static gboolean
panel_window_drag_motion (GtkWidget      *widget,
                          GdkDragContext *context,
                          gint            x,
                          gint            y,
                          guint           drag_time)
{
  return TRUE;
}



static void
panel_window_drag_leave (GtkWidget      *widget,
                         GdkDragContext *context,
                         guint           drag_time)
{
  PanelWindow *window = PANEL_WINDOW (widget);

  /* queue an autohide timeout if needed */
  if (window->autohide_state == AUTOHIDE_VISIBLE)
    panel_window_autohide_queue (window, AUTOHIDE_POPDOWN);
}



static gboolean
panel_window_motion_notify_event (GtkWidget      *widget,
                                  GdkEventMotion *event)
{
  PanelWindow  *window = PANEL_WINDOW (widget);
  gint          pointer_x, pointer_y;
  gint          window_x, window_y;
  gint          high;

  /* leave when the pointer is not grabbed */
  if (G_UNLIKELY (window->grab_time == 0))
    return FALSE;

  /* get the pointer position from the event */
  pointer_x = event->x_root;
  pointer_y = event->y_root;

  /* check if the pointer moved to another monitor */
  if (window->span_monitors == FALSE
      && (pointer_x < window->area.x
          || pointer_y < window->area.y
          || pointer_x > window->area.x + window->area.width
          || pointer_y > window->area.y + window->area.height))
    {
      /* set base point to cursor position and update working area */
      window->base_x = pointer_x;
      window->base_y = pointer_y;
      panel_window_screen_layout_changed (window->screen, window);
    }

  /* calculate the new window position, but keep it inside the working geometry */
  window_x = pointer_x - window->grab_x;
  high = window->area.x + window->area.width - window->alloc.width;
  window_x = CLAMP (window_x, window->area.x, high);

  window_y = pointer_y - window->grab_y;
  high = window->area.y + window->area.height - window->alloc.height;
  window_y = CLAMP (window_y, window->area.y, high);

  /* update the grab coordinates */
  window->grab_x = pointer_x - window_x;
  window->grab_y = pointer_y - window_y;

  /* update the base coordinates */
  window->base_x = window_x + window->alloc.width / 2;
  window->base_y = window_y + window->alloc.height / 2;

  /* update the allocation */
  window->alloc.x = window_x;
  window->alloc.y = window_y;

  /* update the snapping position */
  window->snap_position = panel_window_snap_position (window);

  /* update the working area */
  panel_window_screen_layout_changed (window->screen, window);

  return TRUE;
}



static gboolean
panel_window_button_press_event (GtkWidget      *widget,
                                 GdkEventButton *event)
{
  PanelWindow   *window = PANEL_WINDOW (widget);
  GdkCursor     *cursor;
  GdkGrabStatus  status;
  GdkDisplay    *display;
  guint          modifiers;

  /* leave if the event is not for this window */
  if (event->window != widget->window)
    goto end;

  /* get the modifiers */
  modifiers = event->state & gtk_accelerator_get_default_mod_mask ();

  if (event->button == 1
      && event->type == GDK_BUTTON_PRESS
      && window->locked == FALSE
      && modifiers == 0)
    {
      /* debug check */
      panel_return_val_if_fail (window->grab_time == 0, FALSE);

      /* create a cursor */
      display = gdk_screen_get_display (window->screen);
      cursor = gdk_cursor_new_for_display (display, GDK_FLEUR);

      /* grab the pointer for dragging the window */
      status = gdk_pointer_grab (event->window, FALSE,
                                 GDK_BUTTON_MOTION_MASK
                                 | GDK_BUTTON_RELEASE_MASK,
                                 NULL, cursor, event->time);

      /* release the cursor */
      gdk_cursor_unref (cursor);

      /* set the grab info if the grab was successfully made */
      if (G_LIKELY (status == GDK_GRAB_SUCCESS))
        {
          window->grab_time = event->time;
          window->grab_x = event->x;
          window->grab_y = event->y;
        }

      return !!(status == GDK_GRAB_SUCCESS);
    }
  else if (event->button == 3
           || (event->button == 1 && modifiers == GDK_CONTROL_MASK))
    {
      /* popup the panel menu */
      panel_window_menu_popup (window, event->time);

      return TRUE;
    }

end:
  if (GTK_WIDGET_CLASS (panel_window_parent_class)->button_release_event != NULL)
    return (*GTK_WIDGET_CLASS (panel_window_parent_class)->button_release_event) (widget, event);
  else
    return FALSE;
}



static gboolean
panel_window_button_release_event (GtkWidget      *widget,
                                   GdkEventButton *event)
{
  PanelWindow *window = PANEL_WINDOW (widget);
  GdkDisplay  *display;

  if (window->grab_time != 0)
    {
      /* ungrab the pointer */
      display = gdk_screen_get_display (window->screen);
      gdk_display_pointer_ungrab (display, window->grab_time);
      window->grab_time = 0;

      /* update the position property */
      g_object_notify (G_OBJECT (widget), "position");

      return TRUE;
    }

  if (GTK_WIDGET_CLASS (panel_window_parent_class)->button_release_event != NULL)
    return (*GTK_WIDGET_CLASS (panel_window_parent_class)->button_release_event) (widget, event);
  else
    return FALSE;
}



static void
panel_window_grab_notify (GtkWidget *widget,
                          gboolean   was_grabbed)
{
  PanelWindow *window = PANEL_WINDOW (widget);

  /* avoid hiding the panel when the window is grabbed. this
   * (for example) happens when the user drags in the pager plugin
   * see bug #4597 */
  if (was_grabbed)
    panel_window_thaw_autohide (window);
  else
    panel_window_freeze_autohide (window);
}



static void
panel_window_size_request (GtkWidget      *widget,
                           GtkRequisition *requisition)
{
  PanelWindow    *window = PANEL_WINDOW (widget);
  GtkRequisition  child_requisition = { 0, 0 };
  gint            value, length;
  gint            extra_width = 0, extra_height = 0;
  PanelBorders    borders;

  /* get the child requisition */
  if (GTK_BIN (widget)->child != NULL)
    gtk_widget_size_request (GTK_BIN (widget)->child, &child_requisition);

  /* handle size */
  if (!window->locked)
    {
      if (window->horizontal)
        extra_width += 2 * HANDLE_SIZE_TOTAL;
      else
        extra_height += 2 * HANDLE_SIZE_TOTAL;
    }

  /* get the active borders */
  borders = panel_base_window_get_borders (PANEL_BASE_WINDOW (window));
  if (PANEL_HAS_FLAG (borders, PANEL_BORDER_LEFT))
    extra_width++;
  if (PANEL_HAS_FLAG (borders, PANEL_BORDER_RIGHT))
    extra_width++;
  if (PANEL_HAS_FLAG (borders, PANEL_BORDER_TOP))
    extra_height++;
  if (PANEL_HAS_FLAG (borders, PANEL_BORDER_BOTTOM))
    extra_height++;

  /* set the window requisition */
  if (window->horizontal)
    {
      requisition->height = window->size + extra_height;
      length = window->area.width * window->length;
      value = child_requisition.width + extra_width;
      requisition->width = CLAMP (value, length, window->area.width);
    }
  else
    {
      requisition->width = window->size + extra_width;
      length = window->area.height * window->length;
      value = child_requisition.height + extra_height;
      requisition->height = CLAMP (value, length, window->area.height);
    }
}



static void
panel_window_size_allocate (GtkWidget     *widget,
                            GtkAllocation *alloc)
{
  PanelWindow   *window = PANEL_WINDOW (widget);
  GtkAllocation  child_alloc;
  gint           w, h, x, y;
  PanelBorders   borders;
  GtkWidget     *child;

  widget->allocation = *alloc;
  window->alloc = *alloc;

  if (G_UNLIKELY (window->autohide_state == AUTOHIDE_HIDDEN
                  || window->autohide_state == AUTOHIDE_POPUP))
    {
      /* window is invisible */
      window->alloc.x = window->alloc.y = -9999;

      /* set hidden window size */
      w = h = 3;
      if (window->horizontal)
        w = alloc->width;
      else
        h = alloc->height;

      /* position the autohide window */
      panel_window_size_allocate_set_xy (window, w, h, &x, &y);
      panel_base_window_move_resize (PANEL_BASE_WINDOW (window->autohide_window),
                                     x, y, w, h);
    }
  else
    {
      /* update the allocation */
      panel_window_size_allocate_set_xy (window, alloc->width,
          alloc->height, &window->alloc.x, &window->alloc.y);

      /* update the struts if needed, leave when nothing changed */
      if (window->struts_edge != STRUTS_EDGE_NONE
          && window->autohide_state == AUTOHIDE_DISABLED)
        panel_window_screen_struts_set (window);

      /* move the autohide window offscreen */
      if (window->autohide_window != NULL)
        panel_base_window_move_resize (PANEL_BASE_WINDOW (window->autohide_window),
                                       -9999, -9999, -1, -1);
    }

  /* move the window */
  gtk_window_move (GTK_WINDOW (window), window->alloc.x, window->alloc.y);

  /* position the child */
  child = gtk_bin_get_child (GTK_BIN (widget));
  if (G_LIKELY (child != NULL))
    {
      /* init child allocation */
      child_alloc.x = 0;
      child_alloc.y = 0;
      child_alloc.width = alloc->width;
      child_alloc.height = alloc->height;

      /* set position against the borders */
      borders = panel_base_window_get_borders (PANEL_BASE_WINDOW (window));
      if (PANEL_HAS_FLAG (borders, PANEL_BORDER_LEFT))
        {
          child_alloc.x++;
          child_alloc.width--;
        }
      if (PANEL_HAS_FLAG (borders, PANEL_BORDER_TOP))
        {
          child_alloc.y++;
          child_alloc.height--;
        }
      if (PANEL_HAS_FLAG (borders, PANEL_BORDER_RIGHT))
        child_alloc.width--;
      if (PANEL_HAS_FLAG (borders, PANEL_BORDER_BOTTOM))
        child_alloc.height--;

      /* keep space for the panel handles if not locked */
      if (!window->locked)
        {
          if (window->horizontal)
            {
              child_alloc.width -= 2 * HANDLE_SIZE_TOTAL;
              child_alloc.x += HANDLE_SIZE_TOTAL;
            }
          else
            {
              child_alloc.height -= 2 * HANDLE_SIZE_TOTAL;
              child_alloc.y += HANDLE_SIZE_TOTAL;
            }
        }

      /* allocate the itembar */
      gtk_widget_size_allocate (child, &child_alloc);
    }
}



static void
panel_window_size_allocate_set_xy (PanelWindow *window,
                                   gint         window_width,
                                   gint         window_height,
                                   gint        *return_x,
                                   gint        *return_y)
{
  gint value, hight;

  /* x-position */
  switch (window->snap_position)
    {
      case SNAP_POSITION_NONE:
      case SNAP_POSITION_N:
      case SNAP_POSITION_S:
        /* clamp base point on screen */
        value = window->base_x - (window_width / 2);
        hight = window->area.x + window->area.width - window_width;
        *return_x = CLAMP (value, window->area.x, hight);
        break;

      case SNAP_POSITION_W:
      case SNAP_POSITION_NW:
      case SNAP_POSITION_WC:
      case SNAP_POSITION_SW:
        /* left */
        *return_x = window->area.x;
        break;

      case SNAP_POSITION_E:
      case SNAP_POSITION_NE:
      case SNAP_POSITION_EC:
      case SNAP_POSITION_SE:
        /* right */
        *return_x = window->area.x + window->area.width - window_width;
        break;

      case SNAP_POSITION_NC:
      case SNAP_POSITION_SC:
        /* center */
        *return_x = window->area.x + (window->area.width - window_width) / 2;
        break;
    }

  /* y-position */
  switch (window->snap_position)
    {
      case SNAP_POSITION_NONE:
      case SNAP_POSITION_E:
      case SNAP_POSITION_W:
        /* clamp base point on screen */
        value = window->base_y - (window_height / 2);
        hight = window->area.y + window->area.height - window_height;
        *return_y = CLAMP (value, window->area.y, hight);
        break;

      case SNAP_POSITION_NE:
      case SNAP_POSITION_NW:
      case SNAP_POSITION_NC:
      case SNAP_POSITION_N:
        /* top */
        *return_y = window->area.y;
        break;

      case SNAP_POSITION_SE:
      case SNAP_POSITION_SW:
      case SNAP_POSITION_SC:
      case SNAP_POSITION_S:
        /* bottom */
        *return_y = window->area.y + window->area.height - window_height;
        break;

      case SNAP_POSITION_EC:
      case SNAP_POSITION_WC:
        /* center */
        *return_y = window->area.y + (window->area.height - window_height) / 2;
        break;
    }
}



static void
panel_window_screen_changed (GtkWidget *widget,
                             GdkScreen *previous_screen)
{
  PanelWindow *window = PANEL_WINDOW (widget);
  GdkScreen   *screen;

  /* get the new screen */
  screen = gtk_window_get_screen (GTK_WINDOW (widget));
  panel_return_if_fail (GDK_IS_SCREEN (screen));
  if (G_UNLIKELY (window->screen == screen))
    return;

  /* disconnect from previous screen */
  if (G_UNLIKELY (window->screen != NULL))
    g_signal_handlers_disconnect_by_func (G_OBJECT (window->screen),
        panel_window_screen_layout_changed, window);

  /* set the new screen */
  window->screen = screen;
  g_signal_connect (G_OBJECT (window->screen), "monitors-changed",
      G_CALLBACK (panel_window_screen_layout_changed), window);
  g_signal_connect (G_OBJECT (window->screen), "size-changed",
      G_CALLBACK (panel_window_screen_layout_changed), window);

  /* update the screen layout */
  panel_window_screen_layout_changed (screen, window);
}



static StrutsEgde
panel_window_screen_struts_edge (PanelWindow *window)
{
  panel_return_val_if_fail (PANEL_IS_WINDOW (window), STRUTS_EDGE_NONE);

  /* no struts when autohide is active */
  if (window->autohide_state != AUTOHIDE_DISABLED)
    return STRUTS_EDGE_NONE;

  /* return the screen edge on which the window is
   * visually snapped and where the struts are set */
  switch (window->snap_position)
    {
      case SNAP_POSITION_NONE:
        return STRUTS_EDGE_NONE;

      case SNAP_POSITION_E:
      case SNAP_POSITION_EC:
        return window->horizontal ? STRUTS_EDGE_NONE : STRUTS_EDGE_RIGHT;

      case SNAP_POSITION_NE:
        return window->horizontal ? STRUTS_EDGE_TOP : STRUTS_EDGE_RIGHT;

      case SNAP_POSITION_SE:
        return window->horizontal ? STRUTS_EDGE_BOTTOM : STRUTS_EDGE_RIGHT;

      case SNAP_POSITION_W:
      case SNAP_POSITION_WC:
        return window->horizontal ? STRUTS_EDGE_NONE : STRUTS_EDGE_LEFT;

      case SNAP_POSITION_NW:
        return window->horizontal ? STRUTS_EDGE_TOP : STRUTS_EDGE_LEFT;

      case SNAP_POSITION_SW:
        return window->horizontal ? STRUTS_EDGE_BOTTOM : STRUTS_EDGE_LEFT;

      case SNAP_POSITION_NC:
      case SNAP_POSITION_N:
        return window->horizontal ? STRUTS_EDGE_TOP : STRUTS_EDGE_NONE;

      case SNAP_POSITION_SC:
      case SNAP_POSITION_S:
        return window->horizontal ? STRUTS_EDGE_BOTTOM : STRUTS_EDGE_NONE;
    }

  return STRUTS_EDGE_NONE;
}



static void
panel_window_screen_struts_set (PanelWindow *window)
{
  gulong         struts[N_STRUTS] = { 0, };
  GdkRectangle  *alloc = &window->alloc;
  guint          i;
  gboolean       update_struts = FALSE;
#if STRUTS_DEBUGGING
  gint           n;
  const gchar   *names1[] = { "left", "right", "top", "bottom" };
  const gchar   *names2[] = { "y",    "y",     "x",    "x" };
#endif

  panel_return_if_fail (PANEL_IS_WINDOW (window));
  panel_return_if_fail (GDK_IS_WINDOW (GTK_WIDGET (window)->window));
  panel_return_if_fail (cardinal_atom != 0 && net_wm_strut_partial_atom != 0);
  panel_return_if_fail (GDK_IS_SCREEN (window->screen));

  /* set the struts */
  /* note that struts are relative to the screen edge! */
  if (window->struts_edge == STRUTS_EDGE_TOP)
    {
      /* the window is snapped on the top screen edge */
      struts[STRUT_TOP] = alloc->y + alloc->height;
      struts[STRUT_TOP_START_X] = alloc->x;
      struts[STRUT_TOP_END_X] = alloc->x + alloc->width;
    }
  else if (window->struts_edge == STRUTS_EDGE_BOTTOM)
    {
      /* the window is snapped on the bottom screen edge */
      struts[STRUT_BOTTOM] = gdk_screen_get_height (window->screen) - alloc->y;
      struts[STRUT_BOTTOM_START_X] = alloc->x;
      struts[STRUT_BOTTOM_END_X] = alloc->x + alloc->width;
    }
  else if (window->struts_edge == STRUTS_EDGE_LEFT)
    {
      /* the window is snapped on the left screen edge */
      struts[STRUT_LEFT] = alloc->x + alloc->width;
      struts[STRUT_LEFT_START_Y] = alloc->y;
      struts[STRUT_LEFT_END_Y] = alloc->y + alloc->height;
    }
  else if (window->struts_edge == STRUTS_EDGE_RIGHT)
    {
      /* the window is snapped on the right screen edge */
      struts[STRUT_RIGHT] = gdk_screen_get_width (window->screen) - alloc->x;
      struts[STRUT_RIGHT_START_Y] = alloc->y;
      struts[STRUT_RIGHT_END_Y] = alloc->y + alloc->height;
    }

  /* store the new struts */
  for (i = 0; i < N_STRUTS; i++)
    {
      /* check if we need to update */
      if (G_LIKELY (struts[i] == window->struts[i]))
        continue;

      /* set new value */
      update_struts = TRUE;
      window->struts[i] = struts[i];
    }

  /* leave when there is nothing to update */
  if (update_struts == FALSE)
    return;

  /* don't crash on x errors */
  gdk_error_trap_push ();

  /* set the wm strut partial */
  gdk_property_change (GTK_WIDGET (window)->window,
                       net_wm_strut_partial_atom,
                       cardinal_atom, 32, GDK_PROP_MODE_REPLACE,
                       (guchar *) &struts, N_STRUTS);

#if SET_OLD_WM_STRUTS
  /* set the wm strut (old window managers) */
  gdk_property_change (GTK_WIDGET (window)->window,
                       net_wm_strut_atom,
                       cardinal_atom, 32, GDK_PROP_MODE_REPLACE,
                       (guchar *) &struts, 4);
#endif

  /* release the trap */
  if (gdk_error_trap_pop () != 0)
    g_critical ("Failed to set the struts");

#if STRUTS_DEBUGGING
  /* debugging output */
  if (struts[STRUT_LEFT] != 0)
    n = STRUT_LEFT;
  else if (struts[STRUT_RIGHT] != 0)
    n = STRUT_RIGHT;
  else if (struts[STRUT_TOP] != 0)
    n = STRUT_TOP;
  else if (struts[STRUT_BOTTOM] != 0)
    n = STRUT_BOTTOM;
  else
    n = -1;

  if (n == -1)
    g_message ("Struts updated: Reset to zero.");
  else
    g_message ("Struts updated: %s = %ld, start_%s = %ld, end_%s = %ld.",
               names1[n], struts[n], names2[n], struts[4 + n * 2],
               names2[n], struts[5 + n * 2]);
#endif
}



static void
panel_window_screen_force_update (PanelWindow *window)
{
  panel_return_if_fail (PANEL_IS_WINDOW (window));

  if (GTK_WIDGET_VISIBLE (window))
    {
      /* make sure the struts are set again, when enabled */
      if (window->struts_edge != STRUTS_EDGE_NONE
          && window->autohide_state == AUTOHIDE_DISABLED)
        window->struts[0] = -1;

      /* update the panel position */
      panel_window_screen_layout_changed (window->screen, window);
    }
}



static void
panel_window_screen_update_borders (PanelWindow *window)
{
  PanelBorders borders = PANEL_BORDER_MASK;
  PanelBorders unset_borders = 0;

  panel_return_if_fail (PANEL_IS_WINDOW (window));

  /* the border we want to hide */
  switch (window->snap_position)
    {
      case SNAP_POSITION_NONE:
        break;

      case SNAP_POSITION_E:
      case SNAP_POSITION_EC:
        unset_borders = PANEL_BORDER_RIGHT;
        break;

      case SNAP_POSITION_W:
      case SNAP_POSITION_WC:
        unset_borders = PANEL_BORDER_LEFT;
        break;

      case SNAP_POSITION_N:
      case SNAP_POSITION_NC:
        unset_borders = PANEL_BORDER_TOP;
        break;

      case SNAP_POSITION_S:
      case SNAP_POSITION_SC:
        unset_borders = PANEL_BORDER_BOTTOM;
        break;

      case SNAP_POSITION_NE:
        unset_borders = PANEL_BORDER_RIGHT | PANEL_BORDER_TOP;
        break;

      case SNAP_POSITION_SE:
        unset_borders = PANEL_BORDER_RIGHT | PANEL_BORDER_BOTTOM;
        break;

      case SNAP_POSITION_NW:
        unset_borders = PANEL_BORDER_LEFT | PANEL_BORDER_TOP;
        break;

      case SNAP_POSITION_SW:
        unset_borders = PANEL_BORDER_LEFT | PANEL_BORDER_BOTTOM;
        break;
    }

  /* the visible borders */
  PANEL_UNSET_FLAG (borders, unset_borders);

  /* don't show the side borders if the length is 100% */
  if (window->length == 1.00)
    {
      if (window->horizontal)
        PANEL_UNSET_FLAG (borders, PANEL_BORDER_LEFT | PANEL_BORDER_RIGHT);
      else
        PANEL_UNSET_FLAG (borders, PANEL_BORDER_TOP | PANEL_BORDER_BOTTOM);
    }

  /* set the borders */
  panel_base_window_set_borders (PANEL_BASE_WINDOW (window), borders);
}



static inline guint
panel_window_snap_edge_gravity (gint value,
                                gint start,
                                gint end)
{
  gint center;

  /* snap at the start */
  if (value >= start && value <= start + SNAP_DISTANCE)
    return EDGE_GRAVITY_START;

  /* snap at the end */
  if (value <= end && value >= end - SNAP_DISTANCE)
    return EDGE_GRAVITY_END;

  /* calculate the center of start and end */
  center = start + (end - start) / 2;

  /* snap at the center */
  if (value >= center - 10 && value <= center + SNAP_DISTANCE)
    return EDGE_GRAVITY_CENTER;

  return EDGE_GRAVITY_NONE;
}



static SnapPosition
panel_window_snap_position (PanelWindow *window)
{
  guint         snap_horz, snap_vert;
  GdkRectangle *alloc = &window->alloc;

  /* get the snap offsets */
  snap_horz = panel_window_snap_edge_gravity (alloc->x, window->area.x,
      window->area.x + window->area.width - alloc->width);
  snap_vert = panel_window_snap_edge_gravity (alloc->y, window->area.y,
      window->area.y + window->area.height - alloc->height);

  /* detect the snap mode */
  if (snap_horz == EDGE_GRAVITY_START)
    return SNAP_POSITION_W + snap_vert;
  else if (snap_horz == EDGE_GRAVITY_END)
    return SNAP_POSITION_E + snap_vert;
  else if (snap_horz == EDGE_GRAVITY_CENTER && snap_vert == EDGE_GRAVITY_START)
    return SNAP_POSITION_NC;
  else if (snap_horz == EDGE_GRAVITY_CENTER && snap_vert == EDGE_GRAVITY_END)
    return SNAP_POSITION_SC;
  else if (snap_horz == EDGE_GRAVITY_NONE && snap_vert == EDGE_GRAVITY_START)
    return SNAP_POSITION_N;
  else if (snap_horz == EDGE_GRAVITY_NONE && snap_vert == EDGE_GRAVITY_END)
    return SNAP_POSITION_S;

  return SNAP_POSITION_NONE;
}



static void
panel_window_screen_layout_changed (GdkScreen   *screen,
                                    PanelWindow *window)
{
  GdkRectangle  a, b;
  gint          monitor_num, n_monitors, n;
  gint          dest_x, dest_y;
  gint          dest_w, dest_h;
  gchar        *name;
  StrutsEgde    struts_edge;
  gboolean      force_struts_update = FALSE;

  panel_return_if_fail (PANEL_IS_WINDOW (window));
  panel_return_if_fail (GDK_IS_SCREEN (screen));
  panel_return_if_fail (window->screen == screen);

  /* leave when the screen position if not set */
  if (window->base_x == -1 && window->base_y == -1)
    return;

  /* update the struts edge of this window and check if we need to force
   * a struts update (ie. remove struts that are currently set) */
  struts_edge = panel_window_screen_struts_edge (window);
  if (window->struts_edge != struts_edge && struts_edge == STRUTS_EDGE_NONE)
    force_struts_update = TRUE;
  window->struts_edge = struts_edge;

  /* get the number of monitors */
  n_monitors = gdk_screen_get_n_monitors (screen);
  panel_return_if_fail (n_monitors > 0);

  if (window->span_monitors
      || (n_monitors == 1 && window->output_name == NULL))
    {
      /* get the screen geometry */
      a.x = a.y = 0;
      a.width = gdk_screen_get_width (screen);
      a.height = gdk_screen_get_height (screen);
      panel_return_if_fail (a.width > 0 && a.height > 0);
    }
  else
    {
      if (window->output_name == NULL)
        {
          normal_monitor_positioning:

          /* get the monitor geometry based on the panel position */
          monitor_num = gdk_screen_get_monitor_at_point (screen, window->base_x,
                                                         window->base_y);
          gdk_screen_get_monitor_geometry (screen, monitor_num, &a);
          panel_return_if_fail (a.width > 0 && a.height > 0);
        }
      else
        {
          /* check if the monitor of this panel is present */
          for (n = 0, monitor_num = -1; n < n_monitors && monitor_num == -1; n++)
            {
              name = gdk_screen_get_monitor_plug_name (screen, n);

              /* check if this driver supports output names */
              if (G_UNLIKELY (name == NULL))
                {
                  /* send a warnings why this went wrong */
                  g_critical ("An output is set on the panel window (%s), "
                              "but it looks  like the driver does not "
                              "support output names. Falling back to normal "
                              "monitor positioning, you have to set the output "
                              "again in the preferences to activate this feature.",
                              window->output_name);

                  /* cleanup */
                  g_free (window->output_name);
                  window->output_name = NULL;

                  /* fall back to normal positioning */
                  goto normal_monitor_positioning;
                }

              /* check if this is the monitor we're looking for */
              if (strcmp (window->output_name, name) == 0)
                {
                  /* store the monitor number, get the geometry */
                  monitor_num = n;
                  gdk_screen_get_monitor_geometry (screen, n, &a);
                  panel_return_if_fail (a.width > 0 && a.height > 0);
                }

              /* cleanup */
              g_free (name);
            }

          /* check if a monitor was found */
          if (monitor_num == -1)
            {
              if (GTK_WIDGET_VISIBLE (window))
                gtk_widget_hide (GTK_WIDGET (window));
              return;
            }
        }

      /* don't do the check if we're not setting struts anyways */
      if (window->struts_edge == STRUTS_EDGE_NONE)
        goto done;

      /* traverse the monitors */
      for (n = 0; n < n_monitors; n++)
        {
          /* stop if another window prevent us from settings struts */
          if (window->struts_edge == STRUTS_EDGE_NONE)
            break;

          /* skip the active monitor */
          if (monitor_num == n)
            continue;

          /* get other monitor geometry */
          gdk_screen_get_monitor_geometry (screen, n, &b);

          /* check if this monitor prevents us from setting struts */
          if ((window->struts_edge == STRUTS_EDGE_LEFT && b.x < a.x)
              || (window->struts_edge == STRUTS_EDGE_RIGHT
                  && b.x + b.width > a.x + a.width))
            {
              dest_y = MAX (a.y, b.y);
              dest_h = MIN (a.y + a.height, b.y + b.height) - dest_y;
              if (dest_h > 0)
                window->struts_edge = STRUTS_EDGE_NONE;
            }
          else if ((window->struts_edge == STRUTS_EDGE_TOP && b.y < a.y)
                   || (window->struts_edge == STRUTS_EDGE_BOTTOM
                       && b.y + b.height > a.y + a.height))
            {
              dest_x = MAX (a.x, b.x);
              dest_w = MIN (a.x + a.width, b.x + b.width) - dest_x;
              if (dest_w > 0)
                window->struts_edge = STRUTS_EDGE_NONE;
            }
        }
    }

done:
  /* set the new working area of the panel */
  window->area = a;

  /* update panel borders */
  panel_window_screen_update_borders (window);

  /* queue a resize */
  gtk_widget_queue_resize (GTK_WIDGET (window));

  /* update the struts if needed, ie. we need to reset the struts */
  if (force_struts_update)
    panel_window_screen_struts_set (window);

  if (!GTK_WIDGET_VISIBLE (window))
    gtk_widget_show (GTK_WIDGET (window));
}



static gboolean
panel_window_autohide_timeout (gpointer user_data)
{
  PanelWindow *window = PANEL_WINDOW (user_data);

  panel_return_val_if_fail (window->autohide_state != AUTOHIDE_DISABLED, FALSE);
  panel_return_val_if_fail (window->autohide_state != AUTOHIDE_BLOCKED, FALSE);

  /* update the status */
  if (window->autohide_state == AUTOHIDE_POPDOWN
      || window->autohide_state == AUTOHIDE_POPDOWN_SLOW)
    window->autohide_state = AUTOHIDE_HIDDEN;
  else if (window->autohide_state == AUTOHIDE_POPUP)
    window->autohide_state = AUTOHIDE_VISIBLE;

  /* resize the panel */
  gtk_widget_queue_resize (GTK_WIDGET (window));

  return FALSE;
}



static void
panel_window_autohide_timeout_destroy (gpointer user_data)
{
  PANEL_WINDOW (user_data)->autohide_timeout_id = 0;
}



static void
panel_window_autohide_queue (PanelWindow   *window,
                             AutohideState  new_state)
{
  guint delay;

  panel_return_if_fail (PANEL_IS_WINDOW (window));

  /* stop pending timeout */
  if (window->autohide_timeout_id != 0)
    g_source_remove (window->autohide_timeout_id);

  /* set new autohide state */
  window->autohide_state = new_state;

  /* force a layout update to disable struts */
  if (window->struts_edge != STRUTS_EDGE_NONE
      || window->snap_position != SNAP_POSITION_NONE)
    panel_window_screen_layout_changed (window->screen, window);

  if (new_state == AUTOHIDE_DISABLED || new_state == AUTOHIDE_BLOCKED)
    {
      /* queue a resize to make sure the panel is visible */
      gtk_widget_queue_resize (GTK_WIDGET (window));
    }
  else
    {
      /* timeout delay */
      if (new_state == AUTOHIDE_POPDOWN)
        delay = POPDOWN_DELAY;
      else if (new_state == AUTOHIDE_POPDOWN_SLOW)
        delay = POPDOWN_DELAY * 4;
      else
        delay = POPUP_DELAY;

      /* start a new timeout to hide the panel */
      window->autohide_timeout_id =
        g_timeout_add_full (G_PRIORITY_LOW, delay,
                            panel_window_autohide_timeout, window,
                            panel_window_autohide_timeout_destroy);
    }
}



static gboolean
panel_window_autohide_drag_motion (GtkWidget        *widget,
                                   GdkDragContext   *context,
                                   gint              x,
                                   gint              y,
                                   guint             drag_time,
                                   PanelWindow      *window)
{
  panel_return_val_if_fail (PANEL_IS_WINDOW (window), TRUE);
  panel_return_val_if_fail (window->autohide_window == widget, TRUE);

  /* queue a popup is state is hidden */
  if (window->autohide_state == AUTOHIDE_HIDDEN)
    panel_window_autohide_queue (window, AUTOHIDE_POPUP);

  return TRUE;
}



static void
panel_window_autohide_drag_leave (GtkWidget      *widget,
                                  GdkDragContext *drag_context,
                                  guint           drag_time,
                                  PanelWindow    *window)
{
  panel_return_if_fail (PANEL_IS_WINDOW (window));
  panel_return_if_fail (window->autohide_window == widget);

  /* we left the window before it was hidden, stop the queue */
  if (window->autohide_timeout_id != 0)
    g_source_remove (window->autohide_timeout_id);

  /* update the status */
  if (window->autohide_state == AUTOHIDE_POPUP)
    window->autohide_state = AUTOHIDE_HIDDEN;
}



static gboolean
panel_window_autohide_event (GtkWidget        *widget,
                             GdkEventCrossing *event,
                             PanelWindow      *window)
{
  panel_return_val_if_fail (PANEL_IS_WINDOW (window), FALSE);
  panel_return_val_if_fail (window->autohide_window == widget, FALSE);

  if (event->type == GDK_ENTER_NOTIFY)
    panel_window_autohide_queue (window, AUTOHIDE_POPUP);
  else
    panel_window_autohide_drag_leave (widget, NULL, 0, window);

  return FALSE;
}



static void
panel_window_set_autohide (PanelWindow *window,
                           gboolean     autohide)
{
  GtkWidget   *popup;
  guint        i;
  const gchar *properties[] = { "enter-opacity", "leave-opacity",
                                "background-alpha", "borders" };

  panel_return_if_fail (PANEL_IS_WINDOW (window));

  if ((window->autohide_state != AUTOHIDE_DISABLED) == autohide)
    return;

  /* respond to drag motion */
  gtk_drag_dest_set_track_motion (GTK_WIDGET (window), autohide);

  if (autohide)
    {
      /* create the window */
      panel_return_if_fail (window->autohide_window == NULL);
      popup = g_object_new (PANEL_TYPE_BASE_WINDOW, "type",
                            GTK_WINDOW_POPUP, NULL);

      /* move the window offscreen */
      panel_base_window_move_resize (PANEL_BASE_WINDOW (popup),
                                     -9999, -9999, 3, 3);

      /* bind some properties to sync the two windows */
      for (i = 0; i < G_N_ELEMENTS (properties); i++)
        exo_binding_new (G_OBJECT (window), properties[i],
                         G_OBJECT (popup), properties[i]);

      /* signals for pointer enter/leave events */
      g_signal_connect (G_OBJECT (popup), "enter-notify-event",
          G_CALLBACK (panel_window_autohide_event), window);
      g_signal_connect (G_OBJECT (popup), "leave-notify-event",
          G_CALLBACK (panel_window_autohide_event), window);

      /* show/hide the panel on drag events */
      gtk_drag_dest_set (popup, 0, NULL, 0, 0);
      gtk_drag_dest_set_track_motion (popup, TRUE);
      g_signal_connect (G_OBJECT (popup), "drag-motion",
          G_CALLBACK (panel_window_autohide_drag_motion), window);
      g_signal_connect (G_OBJECT (popup), "drag-leave",
          G_CALLBACK (panel_window_autohide_drag_leave), window);

      /* show the window */
      window->autohide_window = popup;
      gtk_widget_show (popup);

      /* start autohide */
      panel_window_autohide_queue (window,
          window->autohide_block == 0 ? AUTOHIDE_POPDOWN_SLOW : AUTOHIDE_BLOCKED);
    }
  else if (window->autohide_window != NULL)
    {
      /* stop autohide */
      panel_window_autohide_queue (window, AUTOHIDE_DISABLED);

      /* destroy the autohide window */
      panel_return_if_fail (GTK_IS_WINDOW (window->autohide_window));
      gtk_widget_destroy (window->autohide_window);
      window->autohide_window = NULL;
    }
}



static void
panel_window_menu_quit (gpointer boolean)
{
  /* restart or quit */
  dbus_quit_with_restart = !!(GPOINTER_TO_UINT (boolean));

  /* quit main loop */
  gtk_main_quit ();
}



static void
panel_window_menu_deactivate (GtkMenu     *menu,
                              PanelWindow *window)
{
  panel_return_if_fail (GTK_IS_MENU (menu));
  panel_return_if_fail (PANEL_IS_WINDOW (window));

  /* thaw autohide block */
  panel_window_thaw_autohide (window);

  /* destroy the menu */
  g_object_unref (G_OBJECT (menu));
}



static void
panel_window_menu_add_items (PanelWindow *window)
{
  gint              nth;
  PanelApplication *application;

  panel_return_if_fail (PANEL_IS_WINDOW (window));

  application = panel_application_get ();
  nth = panel_application_get_window_index (application, window);
  panel_application_window_select (application, nth);
  g_object_unref (G_OBJECT (application));
  panel_item_dialog_show (gtk_widget_get_screen (GTK_WIDGET (window)));
}



static void
panel_window_menu_popup (PanelWindow *window,
                         guint32      event_time)
{
  GtkWidget *menu;
  GtkWidget *item;
  GtkWidget *image;

  panel_return_if_fail (PANEL_IS_WINDOW (window));

  /* freeze autohide */
  panel_window_freeze_autohide (window);

  /* create menu */
  menu = gtk_menu_new ();

  /* sink the menu and add unref on deactivate */
  g_object_ref_sink (G_OBJECT (menu));
  g_signal_connect (G_OBJECT (menu), "deactivate",
      G_CALLBACK (panel_window_menu_deactivate), window);

  /* label */
  item = gtk_image_menu_item_new_with_label ("Xfce Panel");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_set_sensitive (item, FALSE);
  gtk_widget_show (item);

  /* separator */
  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* add new items */
  item = gtk_image_menu_item_new_with_mnemonic (_("Add _New Items..."));
  g_signal_connect_swapped (G_OBJECT (item), "activate",
      G_CALLBACK (panel_window_menu_add_items), window);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  image = gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  /* customize panel */
  item = gtk_image_menu_item_new_with_mnemonic (_("Panel Pr_eferences..."));
  g_signal_connect_swapped (G_OBJECT (item), "activate",
      G_CALLBACK (panel_preferences_dialog_show), window);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  image = gtk_image_new_from_stock (GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  /* separator */
  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* quit item */
  item = gtk_image_menu_item_new_from_stock (GTK_STOCK_QUIT, NULL);
  g_signal_connect_swapped (G_OBJECT (item), "activate",
      G_CALLBACK (panel_window_menu_quit), GUINT_TO_POINTER (0));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* restart item */
  item = gtk_image_menu_item_new_with_mnemonic (_("_Restart"));
  g_signal_connect_swapped (G_OBJECT (item), "activate",
      G_CALLBACK (panel_window_menu_quit), GUINT_TO_POINTER (1));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  image = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  /* separator */
  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* about item */
  item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ABOUT, NULL);
  g_signal_connect (G_OBJECT (item), "activate",
      G_CALLBACK (panel_dialogs_show_about), NULL);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* popup the menu */
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, event_time);
}



static void
panel_window_set_plugin_orientation (GtkWidget *widget,
                                     gpointer   user_data)
{
  panel_return_if_fail (XFCE_IS_PANEL_PLUGIN_PROVIDER (widget));
  panel_return_if_fail (PANEL_IS_WINDOW (user_data));

  xfce_panel_plugin_provider_set_orientation (XFCE_PANEL_PLUGIN_PROVIDER (widget),
      PANEL_WINDOW (user_data)->horizontal ? GTK_ORIENTATION_HORIZONTAL:
          GTK_ORIENTATION_VERTICAL);
}



static void
panel_window_set_plugin_size (GtkWidget *widget,
                              gpointer   user_data)
{
  panel_return_if_fail (XFCE_IS_PANEL_PLUGIN_PROVIDER (widget));
  panel_return_if_fail (PANEL_IS_WINDOW (user_data));

  xfce_panel_plugin_provider_set_size (XFCE_PANEL_PLUGIN_PROVIDER (widget),
                                       PANEL_WINDOW (user_data)->size);
}



static void
panel_window_set_plugin_screen_position (GtkWidget *widget,
                                         gpointer   user_data)
{
  PanelWindow        *window = PANEL_WINDOW (user_data);
  XfceScreenPosition  position;

  panel_return_if_fail (XFCE_IS_PANEL_PLUGIN_PROVIDER (widget));
  panel_return_if_fail (PANEL_IS_WINDOW (user_data));

  switch (window->snap_position)
    {
      case SNAP_POSITION_NONE:
        position = window->horizontal ? XFCE_SCREEN_POSITION_FLOATING_H :
            XFCE_SCREEN_POSITION_FLOATING_V;
        break;

      case SNAP_POSITION_NW:
        position = window->horizontal ? XFCE_SCREEN_POSITION_NW_H :
            XFCE_SCREEN_POSITION_NW_V;
        break;

      case SNAP_POSITION_NE:
        position = window->horizontal ? XFCE_SCREEN_POSITION_NE_H :
            XFCE_SCREEN_POSITION_NE_V;
        break;

      case SNAP_POSITION_SW:
        position = window->horizontal ? XFCE_SCREEN_POSITION_SW_H :
            XFCE_SCREEN_POSITION_SW_V;
        break;

      case SNAP_POSITION_SE:
        position = window->horizontal ? XFCE_SCREEN_POSITION_SE_H :
            XFCE_SCREEN_POSITION_SE_V;
        break;

      case SNAP_POSITION_W:
      case SNAP_POSITION_WC:
        position = window->horizontal ? XFCE_SCREEN_POSITION_FLOATING_H :
            XFCE_SCREEN_POSITION_W;
        break;

      case SNAP_POSITION_E:
      case SNAP_POSITION_EC:
        position = window->horizontal ? XFCE_SCREEN_POSITION_FLOATING_H :
            XFCE_SCREEN_POSITION_E;
        break;

      case SNAP_POSITION_S:
      case SNAP_POSITION_SC:
        position = window->horizontal ? XFCE_SCREEN_POSITION_S :
            XFCE_SCREEN_POSITION_FLOATING_V;
        break;

      case SNAP_POSITION_N:
      case SNAP_POSITION_NC:
        position = window->horizontal ? XFCE_SCREEN_POSITION_N :
            XFCE_SCREEN_POSITION_FLOATING_V;
        break;

      default:
        panel_assert_not_reached ();
        break;
    }

  xfce_panel_plugin_provider_set_screen_position (XFCE_PANEL_PLUGIN_PROVIDER (widget),
                                                  position);
}



GtkWidget *
panel_window_new (void)
{
  return g_object_new (PANEL_TYPE_WINDOW,
                       "type", GTK_WINDOW_TOPLEVEL,
                       NULL);
}



void
panel_window_set_povider_info (PanelWindow *window,
                               GtkWidget   *provider)
{
  panel_return_if_fail (PANEL_IS_WINDOW (window));
  panel_return_if_fail (XFCE_IS_PANEL_PLUGIN_PROVIDER (provider));

  panel_window_set_plugin_size (provider, window);
  panel_window_set_plugin_orientation (provider, window);
  panel_window_set_plugin_screen_position (provider, window);

  if (PANEL_IS_PLUGIN_EXTERNAL (provider))
    panel_plugin_external_set_background_alpha (PANEL_PLUGIN_EXTERNAL (provider),
        rint (PANEL_BASE_WINDOW (window)->background_alpha * 100.00));
}



void
panel_window_freeze_autohide (PanelWindow *window)
{
  panel_return_if_fail (PANEL_IS_WINDOW (window));
  panel_return_if_fail (window->autohide_block >= 0);

  /* increase autohide block counter */
  window->autohide_block++;

  if (window->autohide_block == 1
      && window->autohide_state != AUTOHIDE_DISABLED)
    panel_window_autohide_queue (window, AUTOHIDE_BLOCKED);
}



void
panel_window_thaw_autohide (PanelWindow *window)
{
  panel_return_if_fail (PANEL_IS_WINDOW (window));
  panel_return_if_fail (window->autohide_block > 0);

  /* decrease autohide block counter */
  window->autohide_block--;

  if (window->autohide_block == 0
      && window->autohide_state != AUTOHIDE_DISABLED)
    panel_window_autohide_queue (window, AUTOHIDE_POPDOWN);
}
