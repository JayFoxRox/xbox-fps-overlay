#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include <winapi/winbase.h>
#include <xboxkrnl/xboxkrnl.h>
#include <xbdm/xbdm.h>

#include <hal/winerror.h>
#include <hal/xbox.h>


#include "font.h"


//FIXME: Move into nxdk

#define NV_PVIDEO 0x8000

#define NV_PVIDEO_INTR 0x100
#define NV_PVIDEO_INTR_BUFFER_0 (1 << 0)
#define NV_PVIDEO_INTR_BUFFER_1 (1 << 4)
#define NV_PVIDEO_INTR_REASON 0x104
#define NV_PVIDEO_INTR_REASON_BUFFER_0 (1 << 0)
#define NV_PVIDEO_INTR_REASON_BUFFER_0_NOTIFICATION 0
#define NV_PVIDEO_INTR_REASON_BUFFER_0_PROTECTION_FAULT 1
#define NV_PVIDEO_INTR_REASON_BUFFER_1 (1 << 4)
#define NV_PVIDEO_INTR_REASON_BUFFER_1_NOTIFICATION 0
#define NV_PVIDEO_INTR_REASON_BUFFER_1_PROTECTION_FAULT 1
#define NV_PVIDEO_INTR_EN 0x140
#define NV_PVIDEO_INTR_EN_BUFFER_0 (1 << 0)
#define NV_PVIDEO_INTR_EN_BUFFER_0_DISABLED 0
#define NV_PVIDEO_INTR_EN_BUFFER_0_ENABLED 1
#define NV_PVIDEO_INTR_EN_BUFFER_1 (1 << 4)
#define NV_PVIDEO_INTR_EN_BUFFER_1_DISABLED 0
#define NV_PVIDEO_INTR_EN_BUFFER_1_ENABLED 1
#define NV_PVIDEO_BUFFER 0x700
#define NV_PVIDEO_BUFFER_0_USE (1 << 0)
#define NV_PVIDEO_BUFFER_1_USE (1 << 4)
#define NV_PVIDEO_STOP 0x704
#define NV_PVIDEO_STOP_OVERLAY (1 << 0)
#define NV_PVIDEO_STOP_OVERLAY_INACTIVE 0
#define NV_PVIDEO_STOP_OVERLAY_ACTIVE 1
#define NV_PVIDEO_STOP_METHOD (1 << 4)
#define NV_PVIDEO_STOP_METHOD_IMMEDIATELY 0
#define NV_PVIDEO_STOP_METHOD_NORMALLY 1

// There's 2 video overlays, so each of the following regs is an array
#define NV_PVIDEO_BASE 0x900 // 64 byte aligned
#define NV_PVIDEO_LIMIT 0x908 // 64 byte aligned
#define NV_PVIDEO_LUMINANCE 0x910
#define NV_PVIDEO_CHROMINANCE 0x918
#define NV_PVIDEO_OFFSET 0x920 // 64 byte aligned
#define NV_PVIDEO_SIZE_IN 0x928
#define NV_PVIDEO_SIZE_IN_WIDTH 0x000007FE
#define NV_PVIDEO_SIZE_IN_HEIGHT 0x03FF0000
#define NV_PVIDEO_POINT_IN 0x930
#define NV_PVIDEO_POINT_IN_S 0x00007FFF
#define NV_PVIDEO_POINT_IN_T 0xFFFE0000
#define NV_PVIDEO_DS_DX 0x938 // Default: 0x100000
#define NV_PVIDEO_DT_DY 0x940 // Default: 0x100000
#define NV_PVIDEO_POINT_OUT 0x948
#define NV_PVIDEO_POINT_OUT_X 0x00000FFF
#define NV_PVIDEO_POINT_OUT_Y 0x0FFF0000
#define NV_PVIDEO_SIZE_OUT 0x950
#define NV_PVIDEO_SIZE_OUT_WIDTH 0x00000FFF
#define NV_PVIDEO_SIZE_OUT_HEIGHT 0x0FFF0000
#define NV_PVIDEO_FORMAT 0x958
#define NV_PVIDEO_FORMAT_PITCH 0x00001FC0
#define NV_PVIDEO_FORMAT_COLOR 0x00030000
#define NV_PVIDEO_FORMAT_COLOR_LE_YB8CR8YA8CB8 0x0
#define NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8 0x1
#define NV_PVIDEO_FORMAT_COLOR_LE_EYB8ECR8EYA8ECB8 0x2
#define NV_PVIDEO_FORMAT_COLOR_LE_ECR8EYB8ECB8EYA8 0x3
#define NV_PVIDEO_FORMAT_DISPLAY (1 << 20)
#define NV_PVIDEO_FORMAT_DISPLAY_ALWAYS 0
#define NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY_EQUAL 1
#define NV_PVIDEO_FORMAT_MATRIX (1 << 24)
#define NV_PVIDEO_FORMAT_MATRIX_ITURBT601 0
#define NV_PVIDEO_FORMAT_MATRIX_ITURBT709 1
#define NV_PVIDEO_FORMAT_FIELD (1 << 28)
#define NV_PVIDEO_FORMAT_FIELD_TOP 0
#define NV_PVIDEO_FORMAT_FIELD_BOTTOM 1

// Color key is shared for both buffers
#define NV_PVIDEO_COLOR_KEY 0xB00



// We will disply 8 digits, with 16x16 pixels each
#define WIDTH 64
#define HEIGHT 32

#define OUT_X 50
#define OUT_Y 50

#define PITCH (WIDTH * 2)


//FIXME: Won't be necessary in the future
int main() { return 0; }


void update_framebuffer(uint8_t* fb) {

  // Open performance counter
  HANDLE h;
  HRESULT hr = DmOpenPerformanceCounter("frames", &h);
  if(!SUCCEEDED(hr)) {
    OutputDebugString("Failed to open counter");
    return;
  }
 
  // Read counter
  DM_COUNTDATA count_data;
  hr = DmQueryPerformanceCounterHandle(h, 0x11, &count_data);
  DmClosePerformanceCounter(h);

  // Get frame info, but avoid frame 0
  uint64_t frame = count_data.CountValue.QuadPart;
  if (frame == 0) {
    OutputDebugString("Counter still at frame zero");
    return;
  }

  // Bootstrap counter
  //FIXME: Doesn't work.. it still gives garbage on first run
  static uint64_t last_frame = 0;
  if (last_frame == 0) {
    last_frame = frame;
    return;
  }

  // Calculate number of frames since last frame
  uint64_t delta = frame - last_frame;
  last_frame = frame;

  // Debug message
  char buf[256];
  sprintf(buf, "Got %" PRIu64 " frames", delta);
  OutputDebugString(buf);

  // Clear framebuffer to white
  for(int i = 0; i < WIDTH * HEIGHT; i++) {
    fb[i * 2 + 0] = 0xFF;
    fb[i * 2 + 1] = 0x7F;
  }

  // Draw on framebuffer
  if (delta > 99999) {
    strcpy(buf, "...");
  } else {
    sprintf(buf, "%" PRIu64, delta);
  }
  int x = 1;
  for(unsigned int i = 0; i < strlen(buf); i++) {
    int gap = WIDTH - x;

    // Don't draw outside of image
    if (gap <= 0) {
      break;
    }

    int symbol_sizes[] = { 12, 10, 12, 12, 13, 13, 13, 10, 12, 13, 5 };

    int symbol = buf[i] - '0';
    if ((symbol < 0) || (symbol > 9)) {
      symbol = 10; // Drawn as '.'
    }
    int symbol_size = symbol_sizes[symbol];
    int symbol_offset = 0;
    while(symbol--) { symbol_offset += symbol_sizes[symbol]; }

    if (symbol_size >= gap) {
      symbol_size = gap;
    }

    for(int y = 0; y < HEIGHT; y++) {

      uint8_t* fb_row = &fb[y * PITCH + x * 2];
      const uint8_t* font_row = &font.pixel_data[(font.width * y + symbol_offset) * 3];

      for(int dx = 0; dx < symbol_size; dx++) {
        fb_row[dx * 2 + 0] = font_row[dx * 3];
      }
    }

    x += symbol_size;
  }
  int width = x + 2;
  if (width > WIDTH) {
    width = WIDTH;
  }
  int out_width = 2 * width;
  int height = HEIGHT;
  int out_height = 2 * height;
  
  // Enable PVIDEO
  {
    uint32_t base = 0xFD000000 + NV_PVIDEO;

    *(volatile uint32_t*)(base + NV_PVIDEO_STOP) = 0; // Stop PVIDEO
    *(volatile uint32_t*)(base + NV_PVIDEO_INTR_EN) = 0; // Disable interrupts
    *(volatile uint32_t*)(base + NV_PVIDEO_INTR) = *(volatile uint32_t*)(base + NV_PVIDEO_INTR); // Clear interrupts

    *(volatile uint32_t*)(base + NV_PVIDEO_LUMINANCE) = (0x0 << 16) | 0x1000;
    *(volatile uint32_t*)(base + NV_PVIDEO_CHROMINANCE) = (0x0 << 16) | 0x1000;

    *(volatile uint32_t*)(base + NV_PVIDEO_BASE) = 0x00000000;
    *(volatile uint32_t*)(base + NV_PVIDEO_LIMIT) = 0xFFFFFFFF;
    *(volatile uint32_t*)(base + NV_PVIDEO_OFFSET) = (uint32_t)MmGetPhysicalAddress(fb);

    *(volatile uint32_t*)(base + NV_PVIDEO_POINT_IN) = (0 << 16) | 0;
    *(volatile uint32_t*)(base + NV_PVIDEO_SIZE_IN) = (HEIGHT << 16) | width;

    *(volatile uint32_t*)(base + NV_PVIDEO_POINT_OUT) = (OUT_Y << 16) | OUT_X;
    *(volatile uint32_t*)(base + NV_PVIDEO_SIZE_OUT) = (out_height << 16) | out_width;

    *(volatile uint32_t*)(base + NV_PVIDEO_DS_DX) = ((width - 1) << 20) / (out_width - 1);
    *(volatile uint32_t*)(base + NV_PVIDEO_DT_DY) = ((height - 1) << 20) / (out_height - 1);

    *(volatile uint32_t*)(base + NV_PVIDEO_FORMAT) = NV_PVIDEO_FORMAT_MATRIX | (NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8 << 16) | PITCH;

    *(volatile uint32_t*)(base + NV_PVIDEO_BUFFER) = NV_PVIDEO_BUFFER_0_USE;
  }

  // Flush all caches
  __asm__ __volatile__("wbinvd");
}


static VOID NTAPI main_thread(PKSTART_ROUTINE StartRoutine, PVOID StartContext) {
  void* fb = StartContext;
  while(1) {    
    update_framebuffer((uint8_t*)fb);
    XSleep(1000);
  }
}

VOID NTAPI DxtEntry(ULONG* pfUnload) {

  // Get a framebuffer
  void* fb = MmAllocateContiguousMemory(PITCH * HEIGHT);
  
  //FIXME: Create a thread for our frame-counter
  ULONG stack_size = 8192;
  ULONG tls_size = 0;
  HANDLE handle;
  HANDLE id;
  NTSTATUS status = PsCreateSystemThreadEx(&handle, 0, stack_size, tls_size, &id, (PKSTART_ROUTINE)NULL, fb, FALSE, FALSE, main_thread);

  // Remain in memory
  *pfUnload = FALSE;

}
