#include <stdio.h>
#include <stdlib.h>

#include <cairo.h>
#include <gtk/gtk.h>

#include "sail_boat.h"
#include "sail_boat_draw.h"
#include "sail_overlay.h"
#include "sail_physics.h"
#include "sail_view.h"
#include "sail_viewstate.h"
#include "sail_wind.h"

#define SAILS_MIN_WIDTH 640
#define SAILS_MIN_HEIGHT 360
#define SAILS_DEFAULT_WIDTH 854
#define SAILS_DEFAULT_HEIGHT 480

#define SAILS_SHOW_OVERLAY FALSE

#define SAILS_FRAMERATE 40
#define SAILS_EVENT_TIMEOUT 1000 / SAILS_FRAMERATE

typedef struct _sail_states {
    Boat *boat;
    Wind *wind;
    ViewState *view;
    GtkWidget *draw;
} SailState;

static SailState* sail_state_new(GtkWidget *draw) {
    SailState *states = malloc(sizeof(SailState));

    states->boat = sail_boat_new();
    states->view = sail_viewstate_new();
    states->wind = sail_wind_new();
    states->draw = draw;

    return states;
}

static void sail_state_free(SailState *state) {
    sail_boat_free(state->boat);
    sail_viewstate_free(state->view);
    free(state);
}

static gboolean on_scroll_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    GdkScrollDirection scroll = 0;
    ViewState *sim = state->view;

    if (gdk_event_get_scroll_deltas(ev, 0, 0) == FALSE) {
        gdk_event_get_scroll_direction(ev, &scroll);

        if (sim->ctrl_held) {
            if (scroll == GDK_SCROLL_UP) {
                sim->scale += sim->scale * 0.05;
            } else if (scroll == GDK_SCROLL_DOWN) {
                sim->scale -= sim->scale * 0.05;
            }
        } else {

            double scroll_distance = 3 / sim->scale;
            switch (scroll) {
                case GDK_SCROLL_UP:
                    sim->translation_y += scroll_distance;
                    break;
                case GDK_SCROLL_DOWN:
                    sim->translation_y -= scroll_distance;
                    break;
                case GDK_SCROLL_LEFT:
                    sim->translation_x += scroll_distance;
                    break;
                case GDK_SCROLL_RIGHT:
                    sim->translation_x -= scroll_distance;
                    break;
                case GDK_SCROLL_SMOOTH:
                    break;
            }
        }
    }
    return FALSE;
}

static gboolean on_motion_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    if (state->view->button_middle_held) {
        gdouble x, y;
        gdk_event_get_coords(ev, &x, &y);

        state->view->translation_x -= (state->view->last_motion_x - x) / state->view->scale;
        state->view->translation_y -= (state->view->last_motion_y - y) / state->view->scale;

        state->view->last_motion_x = x;
        state->view->last_motion_y = y;
    }
    return FALSE;
}

static void on_quit(SailState *state) {
    g_message("qutting...");
    sail_state_free(state);
    gtk_main_quit();
}

static gboolean on_destroy_event(GtkWidget *widget, SailState *state) {
    on_quit(state);
    return FALSE;
}

static gboolean on_button_press_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    guint button;
    gdk_event_get_button(ev, &button);

    gdouble x, y;
    gdk_event_get_coords(ev, &x, &y);

    if (button == 2) {
        state->view->button_middle_held = TRUE;
        state->view->last_motion_x = x;
        state->view->last_motion_y = y;
    }
    return FALSE;
}

static gboolean on_button_release_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    guint button;
    gdk_event_get_button(ev, &button);

    if (button == 2) {
        state->view->button_middle_held = FALSE;
    }
    return FALSE;
}

static void toggle_tracking(SailState *state) {
    state->view->tracking_boat = !state->view->tracking_boat;
    state->view->translation_x = -state->boat->x * SAILS_GRID_SPACING;
    state->view->translation_y = state->boat->y * SAILS_GRID_SPACING;
}

static gboolean on_key_press_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    guint val = 0;
    gdk_event_get_keyval(ev, &val);
    if (val == GDK_KEY_Escape) {
        on_quit(state);
    } else if (val == GDK_KEY_r) {
        state->boat->rudder_angle += 0.05;
    } else if (val == GDK_KEY_e) {
        state->boat->rudder_angle -= 0.05;
    } else if (val == GDK_KEY_space) {
        toggle_tracking(state);
    } else if (val == GDK_KEY_p) {
        state->view->simulator_running = !state->view->simulator_running;
    } else if (val == GDK_KEY_s) {
        state->boat->sail_is_free = !state->boat->sail_is_free;
        if (state->boat->sail_is_free) {
            g_message("sail is now free");
        } else {
            g_message("sail is now blocked");
        }

    } else if (val == GDK_KEY_u) { 
        state->boat->sheet_length += 0.05;
        if (state->boat->sheet_length > SHEET_MAX){
            state->boat->sheet_length = SHEET_MAX;
        }
    } else if (val == GDK_KEY_i) {
        state->boat->sheet_length -= 0.05;
        if (state->boat->sheet_length < SHEET_MIN){
            state->boat->sheet_length = SHEET_MIN;
        }
    } else if (val == GDK_KEY_F11) {
        state->view->is_fullscreen = !state->view->is_fullscreen;
        if (state->view->is_fullscreen) {
            gtk_window_fullscreen(GTK_WINDOW(widget));
        } else {
            gtk_window_unfullscreen(GTK_WINDOW(widget));
        }
    } else if (val == GDK_KEY_Control_L || val == GDK_KEY_Control_R) {
        state->view->ctrl_held = TRUE;
    }
    return FALSE;
}

static gboolean on_key_release_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    guint val = 0;
    gdk_event_get_keyval(ev, &val);
    if (val == GDK_KEY_Control_L || val == GDK_KEY_Control_R) {
        state->view->ctrl_held = FALSE;
    }
    return FALSE;
}

static gboolean on_configure_event(GtkWidget *widget, GdkEvent *ev, SailState *state) {
    gtk_window_get_size(GTK_WINDOW(widget), &state->view->width, &state->view->height);
    return FALSE;
}

static gboolean on_draw_event(GtkWidget *widget, cairo_t *cr, SailState *state) {
    cairo_surface_t *buffer_surface = cairo_surface_create_similar(
            cairo_get_target(cr),
            CAIRO_CONTENT_COLOR_ALPHA,
            state->view->width,
            state->view->height);
    cairo_t *buffer = cairo_create(buffer_surface);

    double translation_x;
    double translation_y;

    if (state->view->tracking_boat) {
        translation_x = -state->boat->x * SAILS_GRID_SPACING;
        translation_y = state->boat->y * SAILS_GRID_SPACING;
    } else {
        translation_x = state->view->translation_x;
        translation_y = state->view->translation_y;
    }

    sail_view_draw(buffer,
            state->view->width, state->view->height,
            translation_x, translation_y,
            state->view->scale);
    sail_boat_draw(buffer, state->boat);

    if (SAILS_SHOW_OVERLAY) {
        sail_overlay_draw(buffer, state->boat, state->view);
    }

    cairo_set_source_surface(cr, buffer_surface, 0, 0);
    cairo_paint(cr);

    cairo_destroy(buffer);
    cairo_surface_destroy(buffer_surface);

    return FALSE;
}

static gboolean event_loop(gpointer state_p) {
    SailState *state = (SailState*) state_p;

    if (state->view->simulator_running) {
        int i;
        for (i=0; i<10000; i++) {
            // FIXME make timestep proportional to framerate
            // make Euler integration a bit more accurate
            sail_physics_update(state->boat, state->wind, 0.000001);
        }
    }

    gtk_widget_queue_draw(state->draw);
    return TRUE;
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *draw;
    GdkGeometry hints;
    hints.min_width = SAILS_MIN_WIDTH;
    hints.min_height = SAILS_MIN_HEIGHT;

    gtk_init(&argc, &argv);

    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", TRUE,
                 NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    draw = gtk_drawing_area_new();

    SailState *states = sail_state_new(draw);

    gtk_container_add(GTK_CONTAINER(window), draw);

    g_signal_connect(G_OBJECT(draw), "draw",
            G_CALLBACK(on_draw_event), states);
    g_signal_connect(window, "destroy",
            G_CALLBACK(on_destroy_event), states);
    g_signal_connect(window, "scroll-event",
            G_CALLBACK(on_scroll_event), states);
    g_signal_connect(window, "motion-notify-event",
            G_CALLBACK(on_motion_event), states);
    g_signal_connect(window, "button-press-event",
            G_CALLBACK(on_button_press_event), states) ;
    g_signal_connect(window, "button-release-event",
            G_CALLBACK(on_button_release_event), states) ;
    g_signal_connect(window, "key-press-event",
            G_CALLBACK(on_key_press_event), states);
    g_signal_connect(window, "key-release-event",
            G_CALLBACK(on_key_release_event), states);
    g_signal_connect(window, "configure-event",
            G_CALLBACK(on_configure_event), states);

    gtk_widget_add_events(window, GDK_SCROLL_MASK);
    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    gtk_widget_add_events(window , GDK_BUTTON_PRESS_MASK);

    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                SAILS_DEFAULT_WIDTH, SAILS_DEFAULT_HEIGHT);
    gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL,
                                  &hints, GDK_HINT_MIN_SIZE);
    gtk_window_set_title(GTK_WINDOW(window), "Sails");

    gtk_widget_show_all(window);

    #if GTK_CHECK_VERSION(3, 14, 0)
        gtk_widget_set_app_paintable(window, TRUE);
        gtk_widget_set_double_buffered(window, FALSE);
    #endif

    gdk_threads_add_timeout(SAILS_EVENT_TIMEOUT, event_loop, (gpointer) states);

    gtk_main();

    return 0;
}
