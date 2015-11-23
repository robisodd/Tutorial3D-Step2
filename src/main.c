#include <pebble.h>
#define MAP_SIZE 100                               // Map is MAP_SIZE * MAP_SIZE squares big (each square is 64x64 pixels)
#define IDCLIP false                               // Walk thru walls if true
Window *window;                                    // The main window and canvas. Drawing to root layer.
GBitmap *texture;                                  // For this example, must be a 1-bit Texture
int32_t player_x = 32 * MAP_SIZE, player_y = -128; // Player's X And Y Starting Position (64 pixels per square)
int16_t player_facing = 16384;                     // Player Direction Facing [-32768 to 32767]
uint32_t offset=0, *data;                          // Used for rendering texture
uint8_t map[MAP_SIZE * MAP_SIZE];                  // 0 is space, any other number is a wall

int32_t abs32(int32_t a) {return (a^(a>>31)) - (a>>31);}  // returns absolute value of a (only works on 32bit signed)

uint8_t getmap(int32_t x, int32_t y) {
  x>>=6; y>>=6;  // convert 64px per block to block position
  return (x<0 || x>=MAP_SIZE || y<0 || y>=MAP_SIZE) ? 0 : map[(y * MAP_SIZE) + x];
}

void main_loop(void *data) { 
  AccelData accel=(AccelData){.x=0, .y=0, .z=0};    // all three are int16_t
  accel_service_peek(&accel);                       // read accelerometer, use y to walk and x to rotate. discard z.
  int32_t dx = (cos_lookup(player_facing) * (accel.y>>5)) / TRIG_MAX_RATIO;  // x distance player attempts to walk
  int32_t dy = (sin_lookup(player_facing) * (accel.y>>5)) / TRIG_MAX_RATIO;  // y distance player attempts to walk
  if(getmap(player_x + dx, player_y) == 0 || IDCLIP) player_x += dx;         // If not running into wall (or no-clip is on), move x
  if(getmap(player_x, player_y + dy) == 0 || IDCLIP) player_y += dy;         // If not running into wall (or no-clip is on), move y
  player_facing += (accel.x<<3);                    // spin based on left/right tilt
  layer_mark_dirty(window_get_root_layer(window));  // Done updating player movement.  Tell Pebble to draw when it's ready
  app_timer_register(50, main_loop, NULL);          // Schedule a Loop in 50ms (~20fps)
}

uint32_t shoot_ray(int32_t start_x, int32_t start_y, int32_t angle) {
  int32_t rx, ry, sin, cos, dx, dy, nx, ny, dist;     // ray x&y, sine & cosine, difference x&y, next x&y, ray length
  sin = sin_lookup(angle); cos = cos_lookup(angle);   // save now to make quicker
  rx = start_x; ry = start_y;                         // Put ray at start
  ny = sin>0 ? 64 : -1;                               // Which side (north or south) of the square the raysegment starts at (which depends on the direction it's heading)
  nx = cos>0 ? 64 : -1;                               // X version  (east or west) of the line above
  while (true) {                                      // Infinite loop, breaks out internally
    dy = ny - (ry & 63);                              // north-south component of distance to next east-west wall
    dx = nx - (rx & 63);                              // east-west component of distance to next north-south wall
    if(abs32(dx * sin) < abs32(dy * cos)) {           // if(distance to north-south wall < distance to east-west wall) See Footnote 1
      rx += dx;                                       // move ray to north-south wall: x part
      ry += ((dx * sin) / cos);                       // move ray to north-south wall: y part
      dist = ((rx - start_x) * TRIG_MAX_RATIO) / cos; // Exit and Return Distance ray traveled.
      offset = cos>0 ? 63-(ry&63) : ry&63;            // Offset is where on wall ray hits: 0 (left edge) to 63 (right edge)
    } else {                                          // else: distance to Y wall < distance to X wall
      rx += (dy * cos) / sin;                         // move ray to east-west wall: x part
      ry += dy;                                       // move ray to east-west wall: y part
      dist = ((ry - start_y) * TRIG_MAX_RATIO) / sin; // Exit and Return Distance ray traveled.
      offset = sin>0 ? rx&63 : 63-(rx&63);            // Get offset: offset is where on wall ray hits: 0 (left edge) to 63 (right edge)
    }                                                 // End if/then/else (x dist < y dist)
    if(rx>=0 && ry>=0 && rx<MAP_SIZE*64 && ry<MAP_SIZE*64) { // If within map bounds
      if(map[((ry>>6) * MAP_SIZE) + (rx>>6)])                // If ray hit a wall
          return dist;                                       // return length of ray
    } else {                                                 // else, ray is not within bounds
      if((sin<=0&&ry<0) || (sin>=0&&ry>=MAP_SIZE*64) || (cos<=0&&rx<0) || (cos>=0&&rx>=MAP_SIZE*64))  // if ray is going further out of bounds
        return 0xFFFFFFFF;                            // ray will never hit a wall, return infinite length
    }
  }
}

void layer_update_proc(Layer *me, GContext *ctx) {
  uint8_t  *screen8  =  (uint8_t*)*(size_t*)ctx;                        //  8bit Pointer to Framebuffer (i.e. screen RAM), for color use
  uint32_t *screen32 = (uint32_t*)*(size_t*)ctx;                        // 32bit Pointer to Framebuffer (i.e. screen RAM), for b&w use
  for(int16_t i = 0; i < 168*PBL_IF_BW_ELSE(5, 36); ++i)                // For every pixel on the screen (36 32bit uints per row. = 144/4)
    screen32[i]=PBL_IF_BW_ELSE(0, (i<168/2*36)?0xC3C3C3C3:0xC8C8C8C8);  // Black background on B&W, Blue Sky w/ Green Floor on Color.
  for(int16_t x = 0; x < 144; ++x) {                                    // Begin RayTracing Loop
    int16_t angle = atan2_lookup((64*x/144)-32, 64);                    // Angle away from [+/-] center column: dx = (64*(col-(box.size.w/2)))/box.size.w; dy = 64; angle = atan2_lookup(dx, dy);
    int32_t dist = shoot_ray(player_x, player_y, player_facing + angle) * cos_lookup(angle); // Shoot the ray, get distance to nearest wall.  Multiply dist by cos to stop fisheye lens.
    int16_t colheight = (168 << 21) / (dist);  // wall segment height = screenheight * wallheight * 64(the "zoom factor") / (distance >> 16) (>>16 is basically quickly doing "/TRIG_MAX_RATIO")
    if(colheight>84) colheight=84;                                      // Make sure line isn't drawn beyond screen edge
    int16_t addr = PBL_IF_BW_ELSE((x>>5) + (168/2*5), x + (168/2*144)); // address of pixel vertically centered at X. (Address=xaddr + yaddr = Pixel.X + 144*Pixel.Y)
    for(int16_t y=0, yoffset=0; y<colheight; y++, yoffset+=PBL_IF_BW_ELSE(5,144)) {
      int32_t xoffset = (y * dist / 168) >> 16;                         // xoffset = which pixel of the texture is hit (0-31).
      PBL_IF_BW_ELSE(screen32[addr-yoffset]|=((*(data+(offset*2)  )>>(31-xoffset))&1)<<(x&31), screen8[addr-yoffset]=((*(data+offset*2  )>>(31-xoffset))&1)?0b11110000:0b11000000);  // Draw Top Half of wall
      PBL_IF_BW_ELSE(screen32[addr+yoffset]|=((*(data+(offset*2)+1)>>(   xoffset))&1)<<(x&31), screen8[addr+yoffset]=((*(data+offset*2+1)>>(   xoffset))&1)?0b11110000:0b11000000);  // Draw Bottom Half of wall
    }
  } // End RayTracing Loop
  graphics_release_frame_buffer(ctx, graphics_capture_frame_buffer(ctx));  // Needed on Aplite to force screen to draw
}

void window_load(Window *window) {
  layer_set_update_proc(window_get_root_layer(window), layer_update_proc);        // Drawing to root layer of the window
  main_loop(NULL);
}

int main(void) {
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {.load = window_load});
  window_stack_push(window, false);
  accel_data_service_subscribe(0, NULL);       // Start accelerometer
  srand(time(NULL));                           // Seed randomizer so different map every time
  for (int16_t i=0; i<MAP_SIZE*MAP_SIZE; i++)  // generate a randomly dotted map
    map[i] = (rand()%3==0) ? 255 : 0;          // Randomly 1/3 of spots are blocks
  data = (uint32_t*)gbitmap_get_data(texture=gbitmap_create_with_resource(RESOURCE_ID_BRICK_WHITE));
  app_event_loop();
  gbitmap_destroy(texture);
  accel_data_service_unsubscribe();
  window_destroy(window);
}