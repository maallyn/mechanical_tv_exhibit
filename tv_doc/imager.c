#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sched.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <assert.h>
#include <unistd.h>

#include <gif_lib.h>
#include <wiringPi.h>

#define BILLION 1000000000
#define BAIRD_ROWS 30
#define BAIRD_COL 40 
#define KIT_ROWS 48
#define KIT_COL 28
#define BAIRD_DELAY 13
#define KIT_DELAY 60

#define BITB0 2
#define BITB1 3
#define BITB2 4
#define BITB3 14

#define BITA0 17
#define BITA1 27
#define BITA2 15
#define BITA3 18

#define BAIRD 22
#define KIT 23

#define RELAY 10
#define BUTTON 24

#define BRAKE_BUT 9
#define BRAKE 25

static char *filename;

static unsigned char *baird_buf = NULL;
static unsigned char *kit_buf = NULL;

static GifRecordType RecordType;
static GifByteType *Extension = NULL;
static GifFileType *GifFile = NULL;

static int Error = 0;
static int brake_status = 0;
static int brake_use = 0;
static int brake_cycle_count = 0;

static int kit_frame_count;
static int baird_frame_count;
static struct timespec baird_duration;
static struct timespec kit_duration;
static long baird_delay;
static int kit_delay;
static struct timespec fix_timer_interval;
static struct timespec baird_frame_start;
static struct timespec kit_frame_start;
static struct timespec baird_delay_time;
static struct timespec kit_delay_time;
static struct timespec baird_frame_end;
static struct timespec kit_frame_end;
static struct timespec button_time;

static int InterlacedOffset[] = { 0, 4, 2, 1 };
static int InterlacedJumps[] = { 8, 8, 4, 2 };
static int ImageNum = 0;
static ColorMapObject *ColorMap = NULL;

static GifWord screen_width = 0;
static GifWord screen_height = 0;
static int image_count = 0;
static size_t movie_pixel_count = 0;
static size_t image_pixel_count = 0;

static pthread_t fix_timer_pthread;
static pthread_t delay_entry_pthread;
static pthread_t baird_timer_pthread;
static pthread_t kit_timer_pthread;
static pthread_t scan_baird_pthread;
static pthread_t scan_kit_pthread;
static pthread_t button_pthread;
static pthread_mutex_t baird_lock;
static pthread_mutex_t kit_lock;

static int stop_it = 0;

/* used for testing */
struct display_struct {
  Display *my_display;
  int blackcolor;
  int whitecolor;
  Window my_window;
  Window my_window_orig;
  GC my_gc;
  GC my_gc_orig;
  XEvent my_event;
  XImage *my_image;
  Visual *my_visual;
  Colormap my_colormap;
};

typedef struct display_struct display_struct_t;

static display_struct_t my_ds;

/* used for testing */
void open_display(display_struct_t *ds)
{

  ds->my_display = XOpenDisplay(0);
  if (ds->my_display == NULL) {
    perror("cant open display");
    exit(EXIT_FAILURE);
  }

  assert(ds->my_display);

  ds->blackcolor = BlackPixel(ds->my_display, DefaultScreen(ds->my_display));
  ds->whitecolor = WhitePixel(ds->my_display, DefaultScreen(ds->my_display));

  ds->my_visual = DefaultVisual(ds->my_display, 0);

  ds->my_window = XCreateSimpleWindow(ds->my_display, DefaultRootWindow(ds->my_display),
    0, 0, BAIRD_ROWS*7, BAIRD_COL*7, 0, ds->blackcolor, ds->blackcolor);

  ds->my_window_orig = XCreateSimpleWindow(ds->my_display, DefaultRootWindow(ds->my_display),
    300, 300, 900, 900, 0, ds->blackcolor, ds->blackcolor);

  XSelectInput(ds->my_display, ds->my_window, StructureNotifyMask);
  XSelectInput(ds->my_display, ds->my_window_orig, StructureNotifyMask);
  XMapWindow(ds->my_display, ds->my_window);
  XMapWindow(ds->my_display, ds->my_window_orig);
  ds->my_gc = XCreateGC(ds->my_display, ds->my_window, 0, 0);
  ds->my_gc_orig = XCreateGC(ds->my_display, ds->my_window_orig, 0, 0);
  XSetForeground(ds->my_display, ds->my_gc, ds->whitecolor);
  XSetBackground(ds->my_display, ds->my_gc, ds->blackcolor);
  XSetFillStyle(ds->my_display, ds->my_gc, FillSolid);
  XSetForeground(ds->my_display, ds->my_gc_orig, ds->whitecolor);
  XSetBackground(ds->my_display, ds->my_gc_orig, ds->blackcolor);
  XSetFillStyle(ds->my_display, ds->my_gc_orig, FillSolid);
  ds->my_colormap = DefaultColormap(ds->my_display, DefaultScreen(ds->my_display));

  XFlush(ds->my_display);

  for(;;) {
    XNextEvent(ds->my_display, &ds->my_event);
    printf("event %d\n", ds->my_event.type);
    if (ds->my_event.type == MapNotify)
      break;
  }
}
  
/* used for testing */
void display_orig(display_struct_t *ds, struct SavedImage *image)
{
  int x_count, y_count, pixel_count;
  unsigned char color_map_index;
  XColor my_color;
  int orig_color;
  pixel_count = 0;
  for (y_count = 0; y_count < screen_height; y_count += 1) {
    for (x_count = 0; x_count < screen_width; x_count += 1) {
      color_map_index = image->RasterBits[pixel_count];
      orig_color = ColorMap->Colors[color_map_index].Red;
      //printf("Colormap index is %d and red is %d\n", color_map_index, orig_color);
      //printf("xcount is %d ycount %d\n", x_count, y_count);
      pixel_count += 1;
      my_color.red = orig_color * 200;
      my_color.green = orig_color * 200;
      my_color.blue = orig_color * 200;
      my_color.flags = DoRed | DoBlue | DoGreen;
      XAllocColor(ds->my_display, ds->my_colormap, &my_color);
      XSetForeground(ds->my_display, ds->my_gc_orig, my_color.pixel);
      XFillRectangle(ds->my_display, ds->my_window_orig, ds->my_gc_orig,
        x_count, y_count, 1, 1);
    }
  }
}

/* used for testing */
void do_pixel(display_struct_t *ds, int x, int y, int val)
{
  XColor my_color;
  my_color.red = (65000 / 16) * val;
  my_color.blue = (65000 / 16) * val;
  my_color.green = (65000 / 16) * val;
  my_color.flags = DoRed | DoBlue | DoGreen;
  XAllocColor(ds->my_display, ds->my_colormap, &my_color);
  XSetForeground(ds->my_display, ds->my_gc, my_color.pixel);
  XFillRectangle(ds->my_display, ds->my_window, ds->my_gc,
    x * 3, y * 3, 3, 3);
  XFlush(ds->my_display);
}

/* Convert cluster of input pixels to a single output pixel */
int get_converted_pixel(struct SavedImage *image, int out_width,
  int out_height, int out_x, int out_y)
{
  int box_width;
  int box_height;
  int box_x;
  int box_y;
  int cur_x;
  int cur_y;
  int sum;
  unsigned char color_map_index;
  /* horiz size of pixel box */
  box_width = screen_width / out_width;
  box_height = screen_height / out_height;
  box_x = out_x * box_width;
  box_y = out_y * box_height;
  sum = 0;
  for (cur_y = 0; cur_y < box_height; cur_y += 1) {
    for (cur_x = 0; cur_x < box_width; cur_x += 1) {
      color_map_index = image->RasterBits[(box_x + cur_x) + ((box_y + cur_y) * screen_width)];
      sum += ColorMap->Colors[color_map_index].Red;
    }
  }
  return (sum / (box_width * box_height));
}
  
/* We are outputting only four bits to digital to anolog converter */
unsigned char fourbit(unsigned char eightbit)
{
  unsigned char fourbitsum;
  fourbitsum = 0;
  fourbitsum = eightbit/16;
//  if ((0b00000011) & eightbit) fourbitsum |= 0b00000001;
//  if ((0b00001100) & eightbit) fourbitsum |= 0b00000010;
//  if ((0b00110000) & eightbit) fourbitsum |= 0b00000100;
//  if ((0b11000000) & eightbit) fourbitsum |= 0b00001000;
  return fourbitsum;
}
  
/* Convert one frame */
void process_image(int output_width, int output_height,
  struct SavedImage *image, unsigned char *output_buf)
{
  unsigned char *cptr1;
  int working;
  int x_count, y_count, pixel_count;
  x_count = 0;
  y_count = 0;
  pixel_count = 0;
  cptr1 = output_buf;
  for (y_count = 0; y_count < output_height; y_count += 1) {
    for (x_count = 0; x_count < output_width; x_count += 1) {
      working = get_converted_pixel(image, output_width, output_height,
         x_count, y_count);
      *cptr1 = fourbit(working);
      //printf("x_count %d y_count %d working %d fourbit %d\n",
      //  x_count, y_count, working, *cptr1);
      cptr1 += 1;
    }
  }
}

/* convert an entire movie */
void process_movie(int output_width, int output_height, unsigned char *movie_buf)
{
  int frame_count;
  unsigned char *image_buf;
  image_buf = movie_buf;
  for (frame_count = 0; frame_count < image_count; frame_count += 1)
  {
    process_image(output_width, output_height,
      GifFile->SavedImages + frame_count, image_buf);
    image_buf += (output_width * output_height);
    //printf("Image was %d of %d screen width %d screen height %d output_width %d, output_height %d\n",
    //  frame_count, image_count, screen_width, screen_height, output_width, output_height);
  }
}

/* convert both of our movies */
void process_both_movies(void)
{
  /* Note for baird, col and row are reversed as scans are vertical */
  process_movie(BAIRD_ROWS, BAIRD_COL, baird_buf);
  process_movie(KIT_COL, KIT_ROWS, kit_buf);
}

void outputa(char val)
{
  /* printf("%d %d %d %d\n", val & 0x01, val & 0x02, val & 0x04, val & 0x08); */
  if ((val & 0x01) == 0) digitalWrite(BITA0, HIGH);
  if ((val & 0x01) != 0) digitalWrite(BITA0, LOW);

  if ((val & 0x02) == 0) digitalWrite(BITA1, HIGH);
  if ((val & 0x02) != 0) digitalWrite(BITA1, LOW);

  if ((val & 0x04) == 0) digitalWrite(BITA2, HIGH);
  if ((val & 0x04) != 0) digitalWrite(BITA2, LOW);

  if ((val & 0x08) == 0) digitalWrite(BITA3, HIGH);
  if ((val & 0x08) != 0) digitalWrite(BITA3, LOW);
}

void outputb(char val)
{
  /* printf("%d %d %d %d\n", val & 0x01, val & 0x02, val & 0x04, val & 0x08); */
  if ((val & 0x01) == 0) digitalWrite(BITB0, HIGH);
  if ((val & 0x01) != 0) digitalWrite(BITB0, LOW);

  if ((val & 0x02) == 0) digitalWrite(BITB1, HIGH);
  if ((val & 0x02) != 0) digitalWrite(BITB1, LOW);

  if ((val & 0x04) == 0) digitalWrite(BITB2, HIGH);
  if ((val & 0x04) != 0) digitalWrite(BITB2, LOW);

  if ((val & 0x08) == 0) digitalWrite(BITB3, HIGH);
  if ((val & 0x08) != 0) digitalWrite(BITB3, LOW);
}

void setupgif(void)
{
  /* Open gif file and do slurp read */
  printf("Opening %s\n", filename);
  GifFile = DGifOpenFileName(filename);
  if (GifFile == NULL) {
    fprintf(stderr, "cannot open gif file\n");
    perror("Error opening gif file");
    exit(EXIT_FAILURE);
  }
  Error = DGifSlurp(GifFile);
  if (Error != GIF_OK) {
    fprintf(stderr, "slurp returns %d\n", Error);
    PrintGifError();
    exit(EXIT_FAILURE);
  }

  screen_width = GifFile->SWidth;
  screen_height = GifFile->SHeight;
  image_count = GifFile->ImageCount;
  ColorMap = GifFile->SColorMap;

  /* Allocate the baird and kit video buffers */
  /* These are monochrome and much smaller than */
  /* original gif; so we will open entire movie */
  /* for each and then do full conversion prior */
  /* to playback to save performance */
  baird_buf = calloc(BAIRD_ROWS * BAIRD_COL * image_count, 1);
  if (baird_buf == NULL) {
    fprintf(stderr, "cannot open baird buffer\n");
    perror("Error opening baird buffer");
    exit(EXIT_FAILURE);
  }
  kit_buf = calloc(KIT_ROWS * KIT_COL * image_count, 1);
  if (kit_buf == NULL) {
    fprintf(stderr, "cannot open kit buffer\n");
    perror("Error opening kit buffer");
    exit(EXIT_FAILURE);
  }
}

/* time functions */
/* Add timespec */
struct timespec time_add(struct timespec time1, struct timespec time2)
{
  struct timespec working;
  working.tv_nsec = (long)0;
  working.tv_sec = (time_t)0;
  if (time1.tv_nsec + time2.tv_nsec >= (long)BILLION) {
    working.tv_sec += 1;
    working.tv_sec += time1.tv_sec + time2.tv_sec;
    working.tv_nsec = time1.tv_nsec + time2.tv_nsec - (long)BILLION;
  } else {
    working.tv_sec = time1.tv_sec + time2.tv_sec;
    working.tv_nsec = time1.tv_nsec + time2.tv_nsec;
  }
  return working;
}
    
/* Subtract timespec (result must be positive) */
/* Value into *time_result; returns 1 for positive */
/* or -1 for negative */
int time_sub(struct timespec *time_result, struct timespec time1,
  struct timespec time2)
{
  /* time1 - time2 */
  int result;
  struct timespec working;
  working.tv_nsec = (long)0;
  working.tv_sec = (time_t)0;

  if ((time1.tv_nsec - time2.tv_nsec) < 0) {
    working.tv_sec = time1.tv_sec - time2.tv_sec - 1;
    working.tv_nsec = time1.tv_nsec - time2.tv_nsec + (long)BILLION;
  } else {
    working.tv_sec = time1.tv_sec - time2.tv_sec;
    working.tv_nsec = time1.tv_nsec - time2.tv_nsec;
  }

  if (working.tv_sec < 0) result = -1;
  else result = 1;
  *time_result = working;
  return result;
}
  
/* Return time in nanoseconds (risky if over 1 second duration) */
long nsec_time(struct timespec input_time)
  {
  long working;
  working = (long)0;
  if (input_time.tv_sec > 0) {
    working += (long)BILLION;
  }
  working += input_time.tv_nsec;
  return working;
}

/* Division; this only works for up to 2 seconds on the total time */
struct timespec devide_time(struct timespec total_time, int devider)
{
  struct timespec working;
  int ct1;
  working.tv_sec = total_time.tv_sec;
  working.tv_nsec = total_time.tv_nsec;
  if (working.tv_sec > 2) working.tv_sec = 2;
  working.tv_nsec += (working.tv_sec * (long)BILLION);
  working.tv_nsec = working.tv_nsec / (long)devider;
  for(working.tv_sec = 0; working.tv_nsec >= (long)BILLION; working.tv_sec += 1) {
    working.tv_nsec -= (long)BILLION;
  }
  return working;
}

void *fixtimer(void *nothing)
{
  static struct timespec start_time;
  static struct timespec stop_time;
  static long stop_nsec;
  static long start_nsec;
  static long duration;
  /* avg_duration needs to be long long in case totaling for average */
  /* results in value over 4 billion (max for long) */
  static long long avg_duration_total;
  static struct timespec duration_timespec;
  static struct timespec working_baird_duration;
  static struct timespec working_baird_delay;
  static struct timespec working_baird_frame_start;
  static struct timespec working_baird_frame_end;
  static struct timespec diff_timespec;
  static long avg_bucket[20];
  static int ct1;
  static int baird_working_frame_count;
  static int last_state;
  static int my_error;
  static int my_direction;

  for (ct1 = 0; ct1 < 20; ct1 += 1) avg_bucket[ct1]  = (long)0;
  last_state = 0;

  /* initial grab of times */
  my_error = clock_gettime(CLOCK_REALTIME, &stop_time);
  if (my_error) {
    perror("clock_gettime");
    exit(1);
  }
  
  while(1) {

    if (brake_use == 0) {
      /* not being used if normal timing */
      sched_yield();
      continue;
    }

    do {
      my_error = clock_gettime(CLOCK_REALTIME, &start_time);
      if (my_error) {
        perror("clock_gettime");
        exit(1);
      }

      my_direction = time_sub(&diff_timespec, start_time, stop_time);
      if (my_direction < 0) {
        sched_yield();
        continue;
      }
    } while (my_direction < 0);

    /* We reached stop time */
    /* Now bump up stop time */
    stop_time = time_add(start_time, fix_timer_interval);

    working_baird_duration.tv_sec = fix_timer_interval.tv_sec;
    working_baird_duration.tv_nsec = fix_timer_interval.tv_nsec;

    /* Calculate next frame start; needs to use delay, which is */
    /* percentage of the total duration time */
    working_baird_delay.tv_sec = (time_t)0;
    working_baird_delay.tv_nsec = working_baird_duration.tv_nsec / (long)100;
    working_baird_delay.tv_nsec = working_baird_delay.tv_nsec * baird_delay;
    working_baird_frame_start = time_add(start_time, 
      working_baird_delay);
    working_baird_frame_end = time_add(working_baird_frame_start,
      working_baird_duration);

    /* now copy in after getting lock */
    pthread_mutex_lock(&baird_lock);
    baird_frame_count += 1;
    baird_duration = working_baird_duration;
    baird_frame_start = working_baird_frame_start;
    baird_frame_end = working_baird_frame_end;
    baird_delay_time = working_baird_delay;
    pthread_mutex_unlock(&baird_lock);
    // printf("fix duration %ld sec %ld nsec\n", baird_duration.tv_sec, baird_duration.tv_nsec);

  }
  pthread_exit(NULL);
}

void bairdtimer(void)
{
  static struct timespec start_time;
  static struct timespec stop_time;
  static long stop_nsec;
  static long start_nsec;
  static long duration;
  /* avg_duration needs to be long long in case totaling for average */
  /* results in value over 4 billion (max for long) */
  static long long avg_duration_total;
  static struct timespec duration_timespec;
  static struct timespec working_baird_duration;
  static struct timespec working_baird_delay;
  static struct timespec working_baird_frame_start;
  static struct timespec working_baird_frame_end;
  static long avg_bucket[20];
  static int ct1;
  static int baird_working_frame_count;
  static int last_state;
  static int my_error;
  static int my_direction;

  if (brake_use == 1) return;

  my_error = clock_gettime(CLOCK_REALTIME, &stop_time);
  if (my_error) {
    perror("clock_gettime");
    exit(1);
  }
  
  my_direction = time_sub(&duration_timespec, stop_time, start_time);
  
  /* Corner case if machine first start or glitch */
  if ((my_direction < 0) || (duration_timespec.tv_sec > 2)) {
    duration = (long)1000000;
  } else {
    duration = nsec_time(duration_timespec);
  }

  working_baird_duration.tv_sec = 0;
  working_baird_duration.tv_nsec = (long)duration;

  /* Calculate next frame start; needs to use delay, which is */
  /* percentage of the total duration time */
  working_baird_delay.tv_sec = (time_t)0;
  working_baird_delay.tv_nsec = working_baird_duration.tv_nsec / (long)100;
  working_baird_delay.tv_nsec = working_baird_delay.tv_nsec * baird_delay;
  working_baird_frame_start = time_add(stop_time, 
    working_baird_delay);
  working_baird_frame_end = time_add(working_baird_frame_start,
    working_baird_duration);

  /* now copy in after getting lock */
  baird_frame_count += 1;
  baird_duration = working_baird_duration;
  baird_frame_start = working_baird_frame_start;
  baird_frame_end = working_baird_frame_end;
  baird_delay_time = working_baird_delay;

  /* New Rotation Starting, Move Stop to Start */
  start_time = stop_time;
}

void *scan_baird(void *nothing)
{
  static int my_x, my_y, my_image;
  static unsigned char *bufptr;
  static unsigned char *work_pointer;
  static unsigned char testchar;
  static struct timespec my_baird_frame_duration;
  static struct timespec my_baird_frame_start;
  static struct timespec my_baird_frame_end;
  static struct timespec my_baird_delay_time;
  static struct timespec working_baird_frame_start;
  static struct timespec baird_line_start;
  static struct timespec baird_line_duration;
  static struct timespec baird_pixel_start;
  static struct timespec baird_pixel_duration;
  static struct timespec current_time;
  static struct timespec time_difference;
  static struct timespec time_sum;
  static int time_direction;
  static int horizontal_offset;
  static int work_y, work_x;
  static int my_baird_frame_count;
  static int working_baird_frame_count;
  static int image_rows;
  static int image_col;
  static int this_image_row;
  static int this_image_col;

  /* Initialize to 0 */
  working_baird_frame_start.tv_sec = 0;
  working_baird_frame_start.tv_nsec = (long)0;
  
  image_rows = BAIRD_COL;
  image_col = BAIRD_ROWS;

  // /* TESTING */
  // printf("image_count %d image rows %d image columns %d\n", image_count,
  //   BAIRD_COL, BAIRD_ROWS);
  // bufptr = baird_buf;
  // if (baird_frame_count < 3) {
  //   for(work_y = 0; work_y < image_rows; work_y += 1) {
  //     int my_test_count;
  //     for (work_x = 0; work_x < image_col; work_x += 1) {
  //       *bufptr = (unsigned char)0;
  //     if ((work_x == 5) && (work_y >= 5) && (work_y < image_rows -5)) *bufptr = (unsigned char)10;
  //     if ((work_x == image_col -5) && (work_y >= 5) && (work_y < image_rows -5)) *bufptr = (unsigned char)10;
  //     if ((work_y == 5) && (work_x >= 5) && (work_x < image_col -5)) *bufptr = (unsigned char)10;
  //     if ((work_y == image_rows -5) && (work_x >= 5) && (work_x < image_col -5)) *bufptr = (unsigned char)10;
  //     printf("val %d for x %d of %d and y %d of %d\n",
  //       (unsigned char)*bufptr, work_x, image_col, work_y, image_rows);
  //     bufptr += 1;
  //     } 
  //   }
  // }

  while(1) {
    /* Continually loop on film */
    bufptr = baird_buf;
    for (my_image = 0; my_image < image_count; my_image += 1) {
      /* on each frame */
      /* set pixel buffer to start of the new frame */
      this_image_row = 0;
      this_image_col = 0;
      bufptr = baird_buf + (BAIRD_ROWS * BAIRD_COL * my_image);

      /* Now wait for new frame start */
      /* new frame starts when my frame start time is no longer */
      /* equel to system frame start time */
      do {
        /* first wait for timer frame count to increment */
        pthread_mutex_lock(&baird_lock);
        working_baird_frame_count = baird_frame_count;
        pthread_mutex_unlock(&baird_lock);
        if (working_baird_frame_count == my_baird_frame_count) {
          /* timer's baird frame count has not advanced yet */
          sched_yield();
        }
      } while (working_baird_frame_count == my_baird_frame_count);
      /* Now we have new frame count; we still must wait for
         new frame start as we still may be in the delay */
      do {
        pthread_mutex_lock(&baird_lock);
        working_baird_frame_start = baird_frame_start;
        pthread_mutex_unlock(&baird_lock);

        clock_gettime(CLOCK_REALTIME, &current_time);

        time_direction = time_sub(&time_difference,
          current_time, working_baird_frame_start);
        if (time_direction < 0) {
          /* We are still before the new frame's start */
          sched_yield();
        }
      } while (time_direction < 0);
 
      /* Now at new frame start */
      /* set new times */
      pthread_mutex_lock(&baird_lock);
      my_baird_frame_count = baird_frame_count;
      my_baird_frame_duration = baird_duration;
      my_baird_frame_start = baird_frame_start;
      my_baird_frame_end = baird_frame_end;
      my_baird_delay_time = baird_delay_time;
      pthread_mutex_unlock(&baird_lock);
      baird_line_start = my_baird_frame_start;
      baird_pixel_start = my_baird_frame_start;
      /* Baird lines are vertical */
      /* Scan is from upper right to lower left, spot going down for each row
         and moving left for each column. Paint vertical rows from right
         to left; moving spot from top to bottom */
      baird_line_duration = devide_time(my_baird_frame_duration, BAIRD_ROWS);
      baird_pixel_duration = devide_time(baird_line_duration, BAIRD_COL);
      // printf("frame: %ld line: %ld pixel: %ld\n",
        // my_baird_frame_duration.tv_nsec, baird_line_duration.tv_nsec,
        // baird_pixel_duration.tv_nsec);

      /* Now wait for the start of this frame in real time*/
      do {
        clock_gettime(CLOCK_REALTIME, &current_time);
        time_direction = time_sub(&time_difference,
          current_time, my_baird_frame_start);
      } while (time_direction < 0);

      // printf("Processing Frame\n");
      
      /* Baird Rows are vertical; columns are horizontal */
      /* Now the housekeeping is done for the frame; now output it by line */
      for (my_x = 0; my_x < BAIRD_ROWS; my_x += 1) {
        /* Lines are vertical; starting from top right */
        horizontal_offset = (BAIRD_COL - 1) - my_x;
        /* Lets make sure that we have not run out of time for the frame */
        pthread_mutex_lock(&baird_lock);
        working_baird_frame_start = baird_frame_start;
        pthread_mutex_unlock(&baird_lock);
        time_direction = time_sub(&time_difference, my_baird_frame_start,
          working_baird_frame_start);
        if ((time_difference.tv_sec != 0) ||
          (time_difference.tv_nsec != 0)) {
          /* System has new time - lets make sure we are still within the
             time allocated for this current frame. Grab the system time;
             we still may be in the delay window */
          clock_gettime(CLOCK_REALTIME, &current_time);
          time_direction = time_sub(&time_difference, working_baird_frame_start,
            current_time);
          if (time_direction < 0) {
            /* We've shot beyond the time for the new frame; bail out */
            // printf("baird turnover at %d\n", my_y);
            break;
          }
        }
        /* now wait for this line's start time */
        do {
          clock_gettime(CLOCK_REALTIME, &current_time);
          time_direction = time_sub(&time_difference,
            current_time, baird_line_start);
        } while (time_direction < 0);
        // printf("line\n");
        /* now output the individual pixels for the line */
        for (my_y = 0; my_y < BAIRD_COL; my_y += 1) {
          /* work values to point to pixel in memory, which */
          /* is by horizontal line for GIF files */
          work_y = (BAIRD_COL -1 - my_y) * BAIRD_ROWS;
          work_x = horizontal_offset;
          work_pointer = bufptr;
          work_pointer = work_pointer + work_y + work_x;
          /* Wait for the pixel start */
          clock_gettime(CLOCK_REALTIME, &current_time);
          time_direction = time_sub(&time_difference,
            current_time, baird_pixel_start);
          if (time_direction < 0) {
            do {
              clock_gettime(CLOCK_REALTIME, &current_time);
              // printf("pw\n");
              time_direction = time_sub(&time_difference,
                current_time, baird_pixel_start);
            } while (time_direction < 0);
          }
          /* twiddle the brake pulse if appropriate */
          if (brake_use) {
            if (my_y < (BAIRD_COL / 2)) {
              brake_status = 1;
              digitalWrite(BRAKE, HIGH);
            } else {
              brake_status = 0;
              digitalWrite(BRAKE, LOW);
            }
          }

          /* Send the pixel value to the device */

          /* TESTING */
          //if ((my_y > 10) && (my_y < 20) && (my_x > 10) && (my_x < 20))
          //  testchar = (unsigned char)10;
          //else
          //  testchar = (unsigned char)0;
          //outputa(testchar);
          //printf("x: %d y: %d y multiplied %d output %x\n", my_x, my_y, work_y, (unsigned int)testchar);
    
          outputa(*work_pointer);

          /* bump up the start time for the next pixel */
          baird_pixel_start = time_add(baird_pixel_start, baird_pixel_duration);
        }
        /* work is done - update times */
        baird_line_start = time_add(baird_line_start, baird_line_duration);
        baird_pixel_start = baird_line_start;
      }
    }
  }
  pthread_exit(NULL);
}

void *set_baird_delay(void *nothing)
{
  while(1) {
    scanf("%ld", &baird_delay);
    printf("You entered %ld\n", baird_delay);
    sleep(1);
  }
  pthread_exit(NULL);
}

void kittimer(void)
{
  static struct timespec start_time;
  static struct timespec stop_time;
  static long stop_nsec;
  static long start_nsec;
  static long duration;
  /* avg_duration needs to be long long in case totaling for average */
  /* results in value over 4 billion (max for long) */
  static long long avg_duration_total;
  static struct timespec duration_timespec;
  static struct timespec working_kit_duration;
  static struct timespec working_kit_delay;
  static struct timespec working_kit_frame_start;
  static struct timespec working_kit_frame_end;
  static long avg_bucket[20];
  static int ct1;
  static int kit_working_frame_count;
  static int last_state;
  static int my_error;
  static int my_direction;

  my_error = clock_gettime(CLOCK_REALTIME, &stop_time);
  if (my_error) {
    perror("clock_gettime");
    exit(1);
  }
  
  my_direction = time_sub(&duration_timespec, stop_time, start_time);
 
  /* Corner case if machine first start or glitch */
  if ((my_direction < 0) || (duration_timespec.tv_sec > 0)) {
    duration = (long)1000000;
  } else {
    duration = nsec_time(duration_timespec);
  }

  working_kit_duration.tv_sec = 0;
  working_kit_duration.tv_nsec = (long)duration;

  /* Calculate next frame start; needs to use delay, which is */
  /* percentage of the total duration time */
  working_kit_delay.tv_sec = (time_t)0;
  working_kit_delay.tv_nsec = working_kit_duration.tv_nsec / (long)100;
  working_kit_delay.tv_nsec = working_kit_delay.tv_nsec * kit_delay;
  working_kit_frame_start = time_add(stop_time, 
    working_kit_delay);
  working_kit_frame_end = time_add(working_kit_frame_start,
    working_kit_duration);

  /* now copy in after getting lock */
  kit_frame_count += 1;
  kit_duration = working_kit_duration;
  kit_frame_start = working_kit_frame_start;
  kit_frame_end = working_kit_frame_end;
  kit_delay_time = working_kit_delay;

  /* New Rotation Starting, Move Stop to Start */
  start_time = stop_time;
}

void *scan_kit(void *nothing)
{
  static int my_x, my_y, my_image;
  static unsigned char *bufptr;
  static unsigned char *work_pointer;
  static struct timespec my_kit_frame_duration;
  static struct timespec my_kit_frame_start;
  static struct timespec my_kit_frame_end;
  static struct timespec my_kit_delay_time;
  static struct timespec working_kit_frame_start;
  static struct timespec kit_line_start;
  static struct timespec kit_line_duration;
  static struct timespec kit_pixel_start;
  static struct timespec kit_pixel_duration;
  static struct timespec current_time;
  static struct timespec time_difference;
  static struct timespec time_sum;
  static int time_direction;
  static int vertical_offset;
  static int work_y, work_x;
  static int my_kit_frame_count;
  static int working_kit_frame_count;

  /* Initialize to 0 */
  working_kit_frame_start.tv_sec = 0;
  working_kit_frame_start.tv_nsec = (long)0;
  
  while(1) {
    /* Continually loop on film */
    bufptr = kit_buf;
    for (my_image = 0; my_image < image_count; my_image += 1) {
      /* on each frame */
      /* set pixel buffer to start of the new frame */
      bufptr = kit_buf + (KIT_ROWS * KIT_COL * my_image);

      /* Now wait for new frame start */
      /* new frame starts when my frame start time is no longer */
      /* equel to system frame start time */
      do {
        /* first wait for timer frame count to increment */
        pthread_mutex_lock(&kit_lock);
        working_kit_frame_count = kit_frame_count;
        pthread_mutex_unlock(&kit_lock);
        if (working_kit_frame_count == my_kit_frame_count) {
          /* timer's kit frame count has not advanced yet */
          sched_yield();
        }
      } while (working_kit_frame_count == my_kit_frame_count);
      /* Now we have new frame count; we still must wait for
         new frame start as we still may be in the delay */
      do {
        pthread_mutex_lock(&kit_lock);
        working_kit_frame_start = kit_frame_start;
        pthread_mutex_unlock(&kit_lock);

        clock_gettime(CLOCK_REALTIME, &current_time);

        time_direction = time_sub(&time_difference,
          current_time, working_kit_frame_start);
        if (time_direction < 0) {
          /* We are still before the new frame's start */
          sched_yield();
        }
      } while (time_direction < 0);
 
      /* Now at new frame start */
      /* set new times */
      pthread_mutex_lock(&kit_lock);
      my_kit_frame_count = kit_frame_count;
      my_kit_frame_duration = kit_duration;
      my_kit_frame_start = kit_frame_start;
      my_kit_frame_end = kit_frame_end;
      my_kit_delay_time = kit_delay_time;
      pthread_mutex_unlock(&kit_lock);
      // printf("kit_frame_start count %d sec %ld nsec %ld\n", my_kit_frame_count, my_kit_frame_start.tv_sec, my_kit_frame_start.tv_nsec);
      kit_line_start = my_kit_frame_start;
      kit_pixel_start = my_kit_frame_start;
      /* Kit lines are horizontal backwards */
      kit_line_duration = devide_time(my_kit_frame_duration, KIT_ROWS);
      kit_pixel_duration = devide_time(kit_line_duration, KIT_COL);

      // printf("frame: %ld line: %ld pixel: %ld\n",
      //   my_kit_frame_duration.tv_nsec, kit_line_duration.tv_nsec,
      //   kit_pixel_duration.tv_nsec);

      /* Now wait for the start of this frame in real time*/
      do {
        clock_gettime(CLOCK_REALTIME, &current_time);
        time_direction = time_sub(&time_difference,
          current_time, my_kit_frame_start);
      } while (time_direction < 0);

      // printf("Processing Frame\n");
      
      /* Now the housekeeping is done for the frame; now output it by line */
      for (my_y = 0; my_y < KIT_ROWS; my_y += 1) {
        /* Lines are horizontal; starting from top right */
        vertical_offset = my_y;
        /* Lets make sure that we have not run out of time for the frame */
        pthread_mutex_lock(&kit_lock);
        working_kit_frame_start = kit_frame_start;
        pthread_mutex_unlock(&kit_lock);
        time_direction = time_sub(&time_difference, my_kit_frame_start,
          working_kit_frame_start);
        if ((time_difference.tv_sec != 0) ||
          (time_difference.tv_nsec != 0)) {
          /* System has new time - lets make sure we are still within the
             time allocated for this current frame. Grab the system time;
             we still may be in the delay window */
          clock_gettime(CLOCK_REALTIME, &current_time);
          time_direction = time_sub(&time_difference, working_kit_frame_start,
            current_time);
          if (time_direction < 0) {
            /* We've shot beyond the time for the new frame; bail out */
            // printf("kit turnover at %d\n", my_y);
            break;
          }
        }
        /* now wait for this line's start time */
        do {
          clock_gettime(CLOCK_REALTIME, &current_time);
          time_direction = time_sub(&time_difference,
            current_time, kit_line_start);
        } while (time_direction < 0);
        // printf("line\n");

        /* now output the individual pixels for the line */
        for (my_x = 0; my_x < KIT_COL; my_x += 1) {
          /* work values to point to pixel in memory, which */
          /* is by horizontal line for GIF files */
          work_x = KIT_COL - my_x - 1;
          work_y = vertical_offset * KIT_COL;
          work_pointer = bufptr;
          work_pointer = work_pointer + work_y + work_x;
          /* Wait for the pixel start */
          clock_gettime(CLOCK_REALTIME, &current_time);
          time_direction = time_sub(&time_difference,
            current_time, kit_pixel_start);
          if (time_direction < 0) {
            do {
              clock_gettime(CLOCK_REALTIME, &current_time);
              // printf("pw\n");
              time_direction = time_sub(&time_difference,
                current_time, kit_pixel_start);
            } while (time_direction < 0);
          }
          /* Send the pixel value to the device */

          outputb(*work_pointer);
 
          /* bump up the start time for the next pixel */
          kit_pixel_start = time_add(kit_pixel_start, kit_pixel_duration);
        }
        /* work is done - update times */
        kit_line_start = time_add(kit_line_start, kit_line_duration);
        kit_pixel_start = kit_line_start;
      }
    }
  }
  pthread_exit(NULL);
}

void *process_button(void *nothing)
{
  static int my_error;
  static struct timespec this_time;
  static struct timespec difference_time;
  static struct timespec brake_last_pushed;
  static int direction;

  brake_last_pushed.tv_sec = 0;
  brake_last_pushed.tv_nsec = (long)0;

  while(1) {
    /* Has button been pressed */
    if (digitalRead(BUTTON) != 1) {
      /* Button Pressed */
      // printf("Button pressed\n");
      my_error = clock_gettime(CLOCK_REALTIME, &button_time);
      digitalWrite(RELAY, HIGH);
    }

    /* Has brake button been pressed while running */
    if (digitalRead(BRAKE_BUT) != 1) {
      /* Make sure not bounce */
      my_error = clock_gettime(CLOCK_REALTIME, &this_time);
      direction = time_sub(&difference_time, this_time, brake_last_pushed);
      if ((difference_time.tv_sec > 0) || (difference_time.tv_nsec > 1000000000)) {
        /* Legitimate press, this is not button bounce after one second */
        if (brake_use == 0) {
          /* Turn brake on */
          brake_use = 1;
          brake_status = 0;
          brake_cycle_count = 0;
        } else {
          /* Turn brake off */
          brake_use = 0;
          brake_status = 0;
          brake_cycle_count = 0;
          digitalWrite(BRAKE, LOW);
        }
        /* Update brake button pushed time to current */
        brake_last_pushed = this_time;
      }
    }
    sched_yield();
    my_error = clock_gettime(CLOCK_REALTIME, &this_time);
    direction = time_sub(&difference_time, this_time, button_time);
    if (difference_time.tv_sec > (1 * 60)) {
      /* Three minutes of viewing - turn off motors */
      // printf("turn off time elapsed\n");
      digitalWrite(RELAY, LOW);
      digitalWrite(BRAKE, LOW);
      brake_use = 0;
      brake_status = 0;
      brake_cycle_count = 0;
    }
    sched_yield();
  }
  pthread_exit(NULL);
}

/* main program; please note that commented code is used for testing */
main (int argc, char **argv)
{
  char outchar;
  int img_ct = 0;
  int movie_done = 0;
  size_t pixel_ct = 0;
  size_t row_ct = 0;
  size_t gif_row_size;

  long test1, test2, test3, time_tot, time_avg;
  int counter1, counter2;
  int my_error;

  Display *my_display;
  int my_screen;
  Window my_window;
  GC my_gc;

  filename = argv[1];

  setupgif();
  process_both_movies();

  button_time.tv_sec = 0;
  button_time.tv_nsec = (long)0;

  brake_status = 0;
  brake_use = 0;

  fix_timer_interval.tv_sec = 0;
  fix_timer_interval.tv_nsec = (long)53160000;

  baird_delay = (long)BAIRD_DELAY;

  kit_delay = (long)KIT_DELAY;

  kit_frame_count = 0;

  if (pthread_mutex_init(&baird_lock, NULL) != 0) {
    printf("baird mutex lock failed\n");
    perror("baird mutex lock");
    exit(-1);
  }

  if (pthread_mutex_init(&kit_lock, NULL) != 0) {
    printf("kit mutex lock failed\n");
    perror("kit mutex lock");
    exit(-1);
  }

//  open_display(&my_ds);
//  while (1) {
//    int xxx, yyy, zzz, pixel_counter;
//    for (zzz = 0; zzz < image_count; zzz += 1) {
//      //display_orig(&my_ds, GifFile->SavedImages + zzz);
//      for (yyy = 0; yyy < BAIRD_ROWS; yyy += 1) {
//        for (xxx = 0; xxx < BAIRD_COL; xxx += 1) {
//          pixel_counter = zzz * BAIRD_ROWS * BAIRD_COL;
//          pixel_counter += yyy * BAIRD_COL;
//          pixel_counter += xxx;
//          do_pixel(&my_ds, xxx, yyy, *(baird_buf + pixel_counter));
//          printf("outputting pixel %d\n",*(baird_buf + pixel_counter));
//       }
//      }
//    }
//  }

//  do_pixel(&my_ds, 0, 0, 1);
//  do_pixel(&my_ds, 1, 0, 8);
//  do_pixel(&my_ds, 2, 0, 14);
//  sleep(5);
//  exit(0);

  wiringPiSetupGpio();
  pinMode(BITA0, OUTPUT);
  pullUpDnControl(BITA0, PUD_UP);
  pinMode(BITA1, OUTPUT);
  pullUpDnControl(BITA1, PUD_UP);
  pinMode(BITA2, OUTPUT);
  pullUpDnControl(BITA2, PUD_UP);
  pinMode(BITA3, OUTPUT);
  pullUpDnControl(BITA3, PUD_UP);

  pinMode(BITB0, OUTPUT);
  pullUpDnControl(BITB0, PUD_UP);
  pinMode(BITB1, OUTPUT);
  pullUpDnControl(BITB1, PUD_UP);
  pinMode(BITB2, OUTPUT);
  pullUpDnControl(BITB2, PUD_UP);
  pinMode(BITB3, OUTPUT);
  pullUpDnControl(BITB3, PUD_UP);

  pinMode(BAIRD, INPUT);

  pinMode(KIT, INPUT);

  pinMode(BRAKE_BUT, INPUT);
  pullUpDnControl(BRAKE_BUT, PUD_UP);

  pinMode(BUTTON, INPUT);
  pullUpDnControl(BUTTON, PUD_UP);

  pinMode(RELAY, OUTPUT);
  pullUpDnControl(RELAY, PUD_UP);

  pinMode(BRAKE, OUTPUT);
  pullUpDnControl(BRAKE, PUD_UP);


  img_ct = 0;

  digitalWrite(RELAY, LOW);
  digitalWrite(BRAKE, LOW);

  outputa((unsigned char) 0);
  outputb((unsigned char) 0);

  brake_use = 0;
  brake_status = 0;
  brake_cycle_count = 0;

  Error = pthread_create(&button_pthread, NULL, process_button, NULL);
//  Error = pthread_create(&delay_entry_pthread, NULL, set_baird_delay, NULL);
  Error = pthread_create(&fix_timer_pthread, NULL, fixtimer, NULL);
//  Error = pthread_create(&baird_timer_pthread, NULL, bairdtimer, NULL);
  Error = wiringPiISR(BAIRD, INT_EDGE_RISING, bairdtimer);
  Error = pthread_create(&scan_baird_pthread, NULL, scan_baird, NULL);
//  Error = pthread_create(&kit_timer_pthread, NULL, kittimer, NULL);
  Error = wiringPiISR(KIT, INT_EDGE_RISING, kittimer);
  Error = pthread_create(&scan_kit_pthread, NULL, scan_kit, NULL);

/* TESTING */
  while(1) {
    sleep(1);
  }


//  open_display(&my_ds);
//  while (1) {
//    int xxx, yyy, zzz, pixel_counter;
//    for (zzz = 0; zzz < image_count; zzz += 1) {
//      //display_orig(&my_ds, GifFile->SavedImages + zzz);
//      for (yyy = 0; yyy < BAIRD_COL; yyy += 1) {
//        for (xxx = 0; xxx < BAIRD_ROWS; xxx += 1) {
//          pixel_counter = zzz * BAIRD_ROWS * BAIRD_COL;
//          pixel_counter += yyy * BAIRD_ROWS;
//          pixel_counter += xxx;
//          do_pixel(&my_ds, xxx, yyy, *(baird_buf + pixel_counter));
//          printf("outputting pixel %d for x %d of %d and y %d of %d pix count %d\n",
//            *(baird_buf + pixel_counter), xxx, BAIRD_ROWS, yyy, BAIRD_COL, pixel_counter);
//        }
//      }
//    }
//  }

//  while(1) {
//    for (outchar = 0; outchar < 16; outchar += 1) {
//      outputa(outchar);
//      outputb(outchar);
//      if ((outchar <= 8) || (outchar >=10)) {
//        outputb(15);
//        outputa(15);
//      } else {
//        outputb(0);
//        outputa(0);
//      }
//      for (img_ct = 0; img_ct < 30000; img_ct += 1) {
//        pixel_ct = 0;
//      }
//    }
//  }
}

