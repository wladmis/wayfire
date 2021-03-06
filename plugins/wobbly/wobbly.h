/**************************************************************************
 *
 * Copyright 2014 Scott Moreau <oreaus@gmail.com>
 * All Rights Reserved.
 *
 **************************************************************************/

#include <stdio.h>

#include <GLES2/gl2.h>


int wobbly_settings_get_friction();
int wobbly_settings_get_spring_k();
int wobbly_settings_get_mass();

struct wobbly_surface
{
   void *ww;
   int x, y, width, height;
   int x_cells, y_cells;
   int grabbed, synced;
   int vertex_count;

   GLfloat *v, *uv;
};

struct wobbly_rect
{
    float tlx, tly;
    float brx, bry;
};

int  wobbly_init(struct wobbly_surface *surface);
void wobbly_fini(struct wobbly_surface *surface);
void wobbly_grab_notify(struct wobbly_surface *surface, int x, int y);
void wobbly_ungrab_notify(struct wobbly_surface *surface);
void wobbly_resize_notify(struct wobbly_surface *surface);
void wobbly_move_notify(struct wobbly_surface *surface, int dx, int dy);
void wobbly_prepare_paint(struct wobbly_surface *surface, int msSinceLastPaint);
void wobbly_done_paint(struct wobbly_surface *surface);
void wobbly_add_geometry(struct wobbly_surface *surface);
struct wobbly_rect wobbly_boundingbox(struct wobbly_surface *surface);

void wobbly_force_geometry(struct wobbly_surface *surface, int x, int y, int w, int h);
void wobbly_unenforce_geometry(struct wobbly_surface *surface);

void wobbly_translate(struct wobbly_surface *surface, int dx, int dy);
