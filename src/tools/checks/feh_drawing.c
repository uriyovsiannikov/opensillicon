#include "feh.h"
#include "filelist.h"
#include "options.h"
#include <stdint.h>
#include <sys/stat.h>

extern const unsigned char font_start[], font_end[];
void feh_imlib_image_fill_text_bg(Imlib_Image, int, int);
#define CHECK(test) do { if (!(test)) { printf("FEH_DRAWING_FAIL:%d\n", __LINE__); return 1; } } while (0)

int main(void)
{
 FILE *file = fopen("/tmp/NotoSans-Medium.ttf", "wb");
 size_t font_size = (size_t)(font_end - font_start);
 CHECK(file && fwrite(font_start, 1, font_size, file) == font_size && !fclose(file));
 imlib_add_path_to_font_path("/tmp");
 Imlib_Font font = imlib_load_font("NotoSans-Medium/11");
 CHECK(font);
 gib_list *lines = feh_wrap_string("one\ntwo", 0, font, NULL);
 CHECK(lines && lines->next && !lines->next->next);
 CHECK(!strcmp(lines->data, "one") && !strcmp(lines->next->data, "two"));
 gib_list_free_and_data(lines);
 int width, height;
 gib_imlib_get_text_size(font, "one two", NULL, &width, &height, IMLIB_TEXT_TO_RIGHT);
 lines = feh_wrap_string("one two three four", width, font, NULL);
 CHECK(lines && lines->next);
 for (gib_list *line = lines; line; line = line->next) {
  int actual;
  gib_imlib_get_text_size(font, line->data, NULL, &actual, NULL, IMLIB_TEXT_TO_RIGHT);
  CHECK(actual <= width);
 }
 gib_list_free_and_data(lines);
 Imlib_Image image = imlib_create_image(8, 8);
 CHECK(image);
 for (int tinted = 0; tinted < 2; tinted++) {
  opt.text_bg = tinted ? TEXT_BG_TINTED : TEXT_BG_CLEAR;
  feh_imlib_image_fill_text_bg(image, 8, 8);
  imlib_context_set_image(image);
  const uint32_t *pixels = imlib_image_get_data_for_reading_only();
  CHECK(pixels);
  for (int i = 0; i < 64; i++) CHECK(pixels[i] == (tinted ? 0x7f000000U : 0U));
 }
 gib_imlib_free_image_and_decache(image);
 feh_file photo = {.filename = "/tmp/photo.ppm", .name = "photo.ppm"};
 opt.caption_path = "captions";
 CHECK(!build_caption_filename(&photo, 0));
 struct stat st;
 CHECK(stat("/tmp/captions", &st) < 0);
 char *path = build_caption_filename(&photo, 1);
 CHECK(path && !strcmp(path, "/tmp/captions/photo.ppm.txt"));
 CHECK(!stat("/tmp/captions", &st) && S_ISDIR(st.st_mode));
 free(path);
 path = build_caption_filename(&photo, 0);
 CHECK(path && !strcmp(path, "/tmp/captions/photo.ppm.txt"));
 free(path);
 imlib_context_set_font(font);
 imlib_free_font();
 puts("FEH_DRAWING_PASS");
 return 0;
}
