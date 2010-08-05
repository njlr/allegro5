/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *
 *      See readme.txt for copyright information.
 */

#include <string.h>
#include "allegro5/allegro.h"
#include "allegro5/internal/aintern.h"
#include "allegro5/internal/aintern_vector.h"
#include "allegro5/allegro_font.h"
#include "allegro5/internal/aintern_bitmap.h"

#include "font.h"


typedef struct
{
   ALLEGRO_USTR *extension;
   ALLEGRO_FONT *(*load_font)(char const *filename, int size, int flags);
} FONT_HANDLER;


static _AL_VECTOR font_handlers;


/* al_font_404_character:
 *  This is what we render missing glyphs as.
 */
static int al_font_404_character = '^';



/* font_height:
 *  (mono and color vtable entry)
 *  Returns the height, in pixels of the font.
 */
static int font_height(const ALLEGRO_FONT *f)
{
   ASSERT(f);
   return f->height;
}



static int font_ascent(const ALLEGRO_FONT *f)
{
    return font_height(f);
}



static int font_descent(const ALLEGRO_FONT *f)
{
    (void)f;
    return 0;
}



/* length:
 *  (mono and color vtable entry)
 *  Returns the length, in pixels, of a string as rendered in a font.
 */
static int length(const ALLEGRO_FONT* f, const ALLEGRO_USTR *text)
{
    int ch = 0, w = 0;
    int pos = 0;
    ASSERT(f);

    while ((ch = al_ustr_get_next(text, &pos)) >= 0) {
        w += f->vtable->char_length(f, ch);
    }

    return w;
}



static void color_get_text_dimensions(ALLEGRO_FONT const *f,
   const ALLEGRO_USTR *text,
   int *bbx, int *bby, int *bbw, int *bbh)
{
   /* Dummy implementation - for A4-style bitmap fonts the bounding
    * box of text is its width and line-height.
    */
   int h = al_get_font_line_height(f);
   if (bbx) *bbx = 0;
   if (bby) *bby = 0;
   if (bbw) *bbw = length(f, text);
   if (bbh) *bbh = h;
}



static ALLEGRO_FONT_COLOR_DATA *_al_font_find_page(
   ALLEGRO_FONT_COLOR_DATA *cf, int ch)
{
    while(cf) {
        if(ch >= cf->begin && ch < cf->end)
            return cf;
        cf = cf->next;
    }
    return NULL;
}


/* _color_find_glyph:
 *  Helper for color vtable entries, below.
 */
static ALLEGRO_BITMAP* _al_font_color_find_glyph(const ALLEGRO_FONT* f, int ch)
{
    ALLEGRO_FONT_COLOR_DATA* cf = (ALLEGRO_FONT_COLOR_DATA*)(f->data);

    cf = _al_font_find_page(cf, ch);
    if (cf) {
        return cf->bitmaps[ch - cf->begin];
    }

    /* if we don't find the character, then search for the missing
       glyph, but don't get stuck in a loop. */
    if(ch != al_font_404_character)
        return _al_font_color_find_glyph(f, al_font_404_character);
    return 0;
}



/* color_char_length:
 *  (color vtable entry)
 *  Returns the length of a character, in pixels, as it would be rendered
 *  in this font.
 */
static int color_char_length(const ALLEGRO_FONT* f, int ch)
{
    ALLEGRO_BITMAP* g = _al_font_color_find_glyph(f, ch);
    return g ? al_get_bitmap_width(g) : 0;
}



/* color_render_char:
 *  (color vtable entry)
 *  Renders a color character onto a bitmap, at the specified location,
 *  using
 *  the specified colors. If fg == -1, render as color, else render as
 *  mono; if bg == -1, render as transparent, else render as opaque.
 *  Returns the character width, in pixels.
 */
static int color_render_char(const ALLEGRO_FONT* f,
   ALLEGRO_COLOR color, int ch, float x,
   float y)
{
   int w = 0;
   int h = f->vtable->font_height(f);
   ALLEGRO_BITMAP *g;

   g = _al_font_color_find_glyph(f, ch);
   if(g) {
      al_draw_tinted_bitmap(g, color, x,
         y + ((float)h - al_get_bitmap_height(g))/2.0f, 0);

      w = al_get_bitmap_width(g);
   }

   return w;
}

/* color_render:
 *  (color vtable entry)
 *  Renders a color font onto a bitmap, at the specified location, using
 *  the specified colors. If fg == -1, render as color, else render as
 *  mono; if bg == -1, render as transparent, else render as opaque.
 */
static int color_render(const ALLEGRO_FONT* f, ALLEGRO_COLOR color,
   const ALLEGRO_USTR *text,
    float x, float y)
{
    int pos = 0;
    int advance = 0;
    int32_t ch;
    bool held = al_is_bitmap_drawing_held();

    al_hold_bitmap_drawing(true);
    while ((ch = al_ustr_get_next(text, &pos)) >= 0) {
        advance += f->vtable->render_char(f, color, ch, x + advance, y);
    }
    al_hold_bitmap_drawing(held);
    return advance;
}



/* color_destroy:
 *  (color vtable entry)
 *  Destroys a color font.
 */
static void color_destroy(ALLEGRO_FONT* f)
{
    ALLEGRO_FONT_COLOR_DATA* cf;
    ALLEGRO_BITMAP *glyphs = NULL;

    if(!f)
        return;

    cf = (ALLEGRO_FONT_COLOR_DATA*)(f->data);

    if (cf)
        glyphs = cf->glyphs;

    while(cf) {
        ALLEGRO_FONT_COLOR_DATA* next = cf->next;
        int i = 0;

        for(i = cf->begin; i < cf->end; i++) al_destroy_bitmap(cf->bitmaps[i - cf->begin]);
        /* Each range might point to the same bitmap. */
        if (cf->glyphs != glyphs) {
            al_destroy_bitmap(cf->glyphs);
            cf->glyphs = NULL;
        }

        if (!next && cf->glyphs)
            al_destroy_bitmap(cf->glyphs);

        al_free(cf->bitmaps);
        al_free(cf);

        cf = next;
    }

    al_free(f);
}


/********
 * vtable declarations
 ********/

ALLEGRO_FONT_VTABLE _al_font_vtable_color = {
    font_height,
    font_ascent,
    font_descent,
    color_char_length,
    length,
    color_render_char,
    color_render,
    color_destroy,
    color_get_text_dimensions,
};

ALLEGRO_FONT_VTABLE* al_font_vtable_color = &_al_font_vtable_color;


static void font_shutdown(void)
{
    while (!_al_vector_is_empty(&font_handlers)) {
       FONT_HANDLER *h = _al_vector_ref_back(&font_handlers);
       al_ustr_free(h->extension);
       _al_vector_delete_at(&font_handlers, _al_vector_size(&font_handlers)-1);
    }
    _al_vector_free(&font_handlers);
}


/* Function: al_init_font_addon
 */
void al_init_font_addon(void)
{
   _al_vector_init(&font_handlers, sizeof(FONT_HANDLER));

   al_register_font_loader(".bmp", _al_load_bitmap_font);
   al_register_font_loader(".jpg", _al_load_bitmap_font);
   al_register_font_loader(".pcx", _al_load_bitmap_font);
   al_register_font_loader(".png", _al_load_bitmap_font);
   al_register_font_loader(".tga", _al_load_bitmap_font);

   _al_add_exit_func(font_shutdown, "font_shutdown");
}


/* Function: al_shutdown_font_addon
 */
void al_shutdown_font_addon(void)
{
   font_shutdown();
}


static FONT_HANDLER *find_extension(char const *extension)
{
   int i;
   /* Go backwards so a handler registered later for the same extension
    * has precedence.
    */
   for (i = _al_vector_size(&font_handlers) - 1; i >= 0 ; i--) {
      FONT_HANDLER *handler = _al_vector_ref(&font_handlers, i);
      if (0 == stricmp(al_cstr(handler->extension), extension))
         return handler;
   }
   return NULL;
}



/* Function: al_register_font_loader
 */
bool al_register_font_loader(char const *extension,
   ALLEGRO_FONT *(*load_font)(char const *filename, int size, int flags))
{
   FONT_HANDLER *handler = find_extension(extension);
   if (!handler) {
      if (!load_font)
         return false; /* Nothing to remove. */
      handler = _al_vector_alloc_back(&font_handlers);
      handler->extension = al_ustr_new(extension);
   }
   else {
      if (!load_font)
         return _al_vector_find_and_delete(&font_handlers, handler);
   }
   handler->load_font = load_font;
   return true;
}



/* Function: al_load_font
 */
ALLEGRO_FONT *al_load_font(char const *filename, int size, int flags)
{
   int i;
   const char *ext;
   FONT_HANDLER *handler;

   ASSERT(filename);

   ext = strrchr(filename, '.');
   if (!ext)
      return NULL;
   handler = find_extension(ext);
   if (handler)
      return handler->load_font(filename, size, flags);

   /* No handler for the extension was registered - try to load with
    * all registered font_handlers and see if one works. So if the user
    * does:
    *
    * al_init_font_addon()
    * al_init_ttf_addon()
    *
    * This will first try to load an unknown (let's say Type1) font file
    * with Freetype (and load it successfully in this case), then try
    * to load it as a bitmap font.
    */
   for (i = _al_vector_size(&font_handlers) - 1; i >= 0 ; i--) {
      FONT_HANDLER *handler = _al_vector_ref(&font_handlers, i);
      ALLEGRO_FONT *try = handler->load_font(filename, size, flags);
      if (try)
         return try;
   }

   return NULL;
}



/* Function: al_get_allegro_font_version
 */
uint32_t al_get_allegro_font_version(void)
{
   return ALLEGRO_VERSION_INT;
}


/* vim: set sts=4 sw=4 et: */

