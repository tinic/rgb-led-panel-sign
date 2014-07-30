#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <algorithm>
#include <time.h>

#include "pru.h"

//----------------------------------------------------------

#define PWM_BITS            5

#define PANELS_ON_PRU0      4
#define PANELS_ON_PRU1      4

#define PANELS_ON_PRU0_FLIP false
#define PANELS_ON_PRU1_FLIP true

#define PANEL_WIDTH         32
#define PANEL_HEIGHT        32

//----------------------------------------------------------

class GPIOLinuxKernelBugWorkaround {
public:
    GPIOLinuxKernelBugWorkaround() {
        static const uint8_t gpios0[] = {
            23, 27, 22, 10, 9, 8, 26, 11, 30, 31, 5, 3, 20, 4, 2, 14, 7, 15
        };
        for (uint32_t i = 0 ; i < sizeof(gpios0) ; i++) {
        	pru_gpio(0, gpios0[i], 1, 0);
        }
        static const uint8_t gpios1[] = {
        	13, 15, 12, 14, 29, 16, 17, 28, 18, 19
        };
        for (uint32_t i = 0 ; i < sizeof(gpios1) ; i++) {
    		pru_gpio(1, gpios1[i], 1, 0);
        }
        static const uint8_t gpios2[] = {
        	2, 5, 22, 23, 14, 12, 10, 8, 6, 3, 4, 1, 24, 25, 17, 16, 15, 13, 11, 9, 7
        };
        for (uint32_t i = 0 ; i < sizeof(gpios2) ; i++) {
    		pru_gpio(2, gpios2[i], 1, 0);
        }
        static const uint8_t gpios3[] = {
        	21, 19, 15, 14, 17, 16
        };
    	for (uint32_t i = 0 ; i < sizeof(gpios3) ; i++) {
    		pru_gpio(3, gpios3[i], 1, 0);
    	}
    }
};

class PRU {
public:
    PRU(int32_t num):
        m_pru(0),
        m_num(num),
        m_flipped(false),
        m_frame(0) {
    }

    void copyNewFrame(uint8_t *src, uint32_t src_stride) {
        m_frame ^= 1;
        uint8_t *dst = ddrMem() + (m_frame ? frameSize() : 0);
        uint32_t shift = m_pwmshift;
        uint32_t npanels = m_pruMem->nledpanels;
        uint32_t pixelHeight = m_pruMem->panelheightdiv2 * 2;
        for (uint32_t y = 0; y < pixelHeight/2; y++) {
            if (m_flipped) {
                uint32_t yd = ((pixelHeight/2) - 1 - y) * PANEL_WIDTH * npanels * 3 * 2;
                uint32_t ys = m_ilc[y];
                for (uint32_t x = 0; x < PANEL_WIDTH * npanels; x++) {
                    uint32_t xs = (PANEL_WIDTH * npanels - 1 - x) * 3;
                    uint32_t xd = x * 3 * 2;
                    dst[yd + xd + 3] = src[(ys+ 0)*src_stride + xs + 0]>>shift;
                    dst[yd + xd + 4] = src[(ys+ 0)*src_stride + xs + 1]>>shift;
                    dst[yd + xd + 5] = src[(ys+ 0)*src_stride + xs + 2]>>shift;
                    dst[yd + xd + 0] = src[(ys+16)*src_stride + xs + 0]>>shift;
                    dst[yd + xd + 1] = src[(ys+16)*src_stride + xs + 1]>>shift;
                    dst[yd + xd + 2] = src[(ys+16)*src_stride + xs + 2]>>shift;
                }
            } else {
                uint32_t yd = y * PANEL_WIDTH * npanels * 3 * 2;
                uint32_t ys = m_ilc[y];
                for (uint32_t x = 0; x < PANEL_WIDTH * npanels; x++) {
                    uint32_t xs = x * 3;
                    uint32_t xd = x * 3 * 2;
                    dst[yd + xd + 0] = src[(ys+ 0)*src_stride + xs + 0]>>shift;
                    dst[yd + xd + 1] = src[(ys+ 0)*src_stride + xs + 1]>>shift;
                    dst[yd + xd + 2] = src[(ys+ 0)*src_stride + xs + 2]>>shift;
                    dst[yd + xd + 3] = src[(ys+16)*src_stride + xs + 0]>>shift;
                    dst[yd + xd + 4] = src[(ys+16)*src_stride + xs + 1]>>shift;
                    dst[yd + xd + 5] = src[(ys+16)*src_stride + xs + 2]>>shift;
                }
            }
        }
        setPRUDDRAddr();
    }
    
    size_t frameSize() const {
        uint32_t npanels = m_pruMem->nledpanels;
        uint32_t pixelHeight = m_pruMem->panelheightdiv2 * 2;
        return npanels * 32 * pixelHeight * 3;
    }

    bool init() {
        m_pru = pru_init(m_num);
        if (!m_pru) {
            return false;
        }
        m_pruMem = reinterpret_cast<PRUMem *>(m_pru->data_ram);
        m_ddrMem = reinterpret_cast<uint8_t *>(m_pru->ddr) + (m_num ? 0x00020000 : 0x00000000);
        memset(m_ddrMem, 0, 0x20000);
        memset(m_pruMem, 0, m_pru->data_ram_size);
        return true;
    }
    
    void setPanelGeometry(uint8_t pwm2n, uint8_t paneln, uint8_t pixelsHeight, bool flipped) {
        if (pwm2n >= 8 ) {  
            pwm2n = 7;
        }
        m_pruMem->pwmlength = 1UL<<pwm2n;
        m_pwmshift = 8-pwm2n;
        if (pixelsHeight != 16 && pixelsHeight != 32 ) {
            pixelsHeight = 32;
        }
        m_pruMem->panelheightdiv2 = pixelsHeight / 2;
        m_pruMem->panelheightdiv2p1 = (pixelsHeight / 2) + 1;
        if (paneln > 4 ) {  
            paneln = 4;
        }
        m_pruMem->nledpanels = paneln;
        m_pruMem->ntotalrowpixels = paneln * PANEL_WIDTH;
        m_flipped = flipped;
        
        uint32_t i = 0;
        for (uint32_t c = 0; c < pixelsHeight / 2; c += 2) {
            m_ilc[i++] = c;
        }
        for (uint32_t c = 1; c < pixelsHeight / 2; c += 2) {
            m_ilc[i++] = c;
        }
    }
    
    void setBrightnessGamma(float brightness = 6.f, float gamma = 2.5f) {
        uint32_t n = m_pruMem->pwmlength;
        static float pwmadjust[8] = {
             0.25f/8.f,
             0.50f/8.f,
             1.00f/8.f,
             2.00f/8.f,
             4.00f/8.f,
             8.00f/8.f,
            16.00f/8.f,
            32.00f/8.f
        };
        for (uint32_t c = 0; c < n; c++) {
            float g = pow(float(c)/float(n-1),gamma) * float(n-1);
        	#define bmin(a,b) ((a<b)?(a):(b))
    		#define bmax(a,b) ((a>b)?(a):(b))
            m_pruMem->gamma[c] = bmin(255,bmax(0,int(g * brightness * pwmadjust[m_pwmshift] * (PANELS_ON_PRU0 + PANELS_ON_PRU1))));
        }
    }
    
    void start() {
        char str[16] = { 0 };
        sprintf(str, "pru%d.bin", m_num);
        pru_exec(m_pru, str);
        while(!m_pruMem->ready) {}
        setPRUDDRAddr();
    }
    
    void pause() {
        m_pruMem->addr = 0;
    }
    
    void resume() {
        setPRUDDRAddr();
    }
    
    void setPRUDDRAddr() {
        m_pruMem->addr = m_pru->ddr_addr + (m_num ? 0x00020000 : 0x00000000) + (m_frame ? frameSize() : 0);
    }

    void stop() {
        if (!m_pru) {
            return;
        }
        m_pruMem->addr = 0x01;
        pru_close(m_pru);
    }

    uint8_t *pruMem() { return ((uint8_t *)m_pruMem); }
    uint8_t *ddrMem() { return m_ddrMem; }

private:

    struct __attribute__((packed, aligned(4))) PRUMem {
        uint32_t    addr;
        uint32_t    ready;
        uint8_t     pwmlength;
        uint8_t     nledpanels;
        uint8_t     ntotalrowpixels;
        uint8_t     panelheightdiv2;
        uint8_t     panelheightdiv2p1;
        uint8_t     dummy0;
        uint16_t    dummy1;
        uint8_t     gamma[128];
    } *m_pruMem;

    uint8_t *   m_ddrMem;
    int32_t     m_num;
    pru_t   *   m_pru;
    bool        m_flipped;
    uint8_t     m_pwmshift;
    uint8_t     m_frame;
    uint8_t     m_ilc[256];
};

class LEDBuffer {
public:
    LEDBuffer(uint8_t panels, uint8_t pixelheight, uint8_t clipBits):
        m_panels(panels),
        m_pixelheight(pixelheight),
        m_clipBits(clipBits) { 
        m_buf = new uint8_t[m_panels * PANEL_WIDTH * m_pixelheight * 3];
        memset(m_buf, 0, sizeof(m_buf)); 
    };
    
    ~LEDBuffer() {
        delete [] m_buf;
    }
    
    void dither() {
        static uint8_t bayer16x16[256] = {
    		0x00,0xc0,0x30,0xf0,0x0c,0xcc,0x3c,0xfc,0x03,0xc3,0x33,0xf3,0x0f,0xcf,0x3f,0xff,
    		0x80,0x40,0xb0,0x70,0x8c,0x4c,0xbc,0x7c,0x83,0x43,0xb3,0x73,0x8f,0x4f,0xbf,0x7f,
    		0x20,0xe0,0x10,0xd0,0x2c,0xec,0x1c,0xdc,0x23,0xe3,0x13,0xd3,0x2f,0xef,0x1f,0xdf,
    		0xa0,0x60,0x90,0x50,0xac,0x6c,0x9c,0x5c,0xa3,0x63,0x93,0x53,0xaf,0x6f,0x9f,0x5f,
    		0x08,0xc8,0x38,0xf8,0x04,0xc4,0x34,0xf4,0x0b,0xcb,0x3b,0xfb,0x07,0xc7,0x37,0xf7,
    		0x88,0x48,0xb8,0x78,0x84,0x44,0xb4,0x74,0x8b,0x4b,0xbb,0x7b,0x87,0x47,0xb7,0x77,
    		0x28,0xe8,0x18,0xd8,0x24,0xe4,0x14,0xd4,0x2b,0xeb,0x1b,0xdb,0x27,0xe7,0x17,0xd7,
    		0xa8,0x68,0x98,0x58,0xa4,0x64,0x94,0x54,0xab,0x6b,0x9b,0x5b,0xa7,0x67,0x97,0x57,
    		0x02,0xc2,0x32,0xf2,0x0e,0xce,0x3e,0xfe,0x01,0xc1,0x31,0xf1,0x0d,0xcd,0x3d,0xfd,
    		0x82,0x42,0xb2,0x72,0x8e,0x4e,0xbe,0x7e,0x81,0x41,0xb1,0x71,0x8d,0x4d,0xbd,0x7d,
    		0x22,0xe2,0x12,0xd2,0x2e,0xee,0x1e,0xde,0x21,0xe1,0x11,0xd1,0x2d,0xed,0x1d,0xdd,
    		0xa2,0x62,0x92,0x52,0xae,0x6e,0x9e,0x5e,0xa1,0x61,0x91,0x51,0xad,0x6d,0x9d,0x5d,
    		0x0a,0xca,0x3a,0xfa,0x06,0xc6,0x36,0xf6,0x09,0xc9,0x39,0xf9,0x05,0xc5,0x35,0xf5,
    		0x8a,0x4a,0xba,0x7a,0x86,0x46,0xb6,0x76,0x89,0x49,0xb9,0x79,0x85,0x45,0xb5,0x75,
    		0x2a,0xea,0x1a,0xda,0x26,0xe6,0x16,0xd6,0x29,0xe9,0x19,0xd9,0x25,0xe5,0x15,0xd5,
    		0xaa,0x6a,0x9a,0x5a,0xa6,0x66,0x96,0x56,0xa9,0x69,0x99,0x59,0xa5,0x65,0x95,0x55
    	};
    	if (m_clipBits >= 8) {
    		m_clipBits = 8;
    	}
        uint8_t *ptr = m_buf;
    	for (int32_t y = 0; y < m_pixelheight; y++) {
    		for (int32_t x = 0; x < (m_panels * PANEL_WIDTH); x++) {
    			int32_t sr = int32_t(ptr[0]);
    			int32_t sg = int32_t(ptr[1]);
    			int32_t sb = int32_t(ptr[2]);
    			
    			sr = (sr << 8) + (int32_t(bayer16x16[(y & 0xF) * 16 + (x & 0xF)]) << m_clipBits);
    			sg = (sg << 8) + (int32_t(bayer16x16[(y & 0xF) * 16 + (x & 0xF)]) << m_clipBits);
    			sb = (sb << 8) + (int32_t(bayer16x16[(y & 0xF) * 16 + (x & 0xF)]) << m_clipBits);
    			
    			uint32_t sh = 8 + m_clipBits;
    
    			#define pmin(a,b) ((a<b)?(a):(b))
    			#define pmax(a,b) ((a>b)?(a):(b))
    			
    			int32_t dr = pmin(65535,pmax(0,(sr >> sh) << sh));
    			int32_t dg = pmin(65535,pmax(0,(sg >> sh) << sh));
    			int32_t db = pmin(65535,pmax(0,(sb >> sh) << sh));
    			
    			*ptr++ = uint8_t(dr >> 8);
    			*ptr++ = uint8_t(dg >> 8);
    			*ptr++ = uint8_t(db >> 8);
    		}
    	}
    }
    

    size_t size() const { return m_panels * PANEL_WIDTH * m_pixelheight * 3; }
    uint8_t *buffer() { return (uint8_t*)m_buf; }
private:
    uint8_t  m_panels;
    uint8_t  m_pixelheight;
    uint8_t *m_buf;
    uint8_t  m_clipBits;
};

static GPIOLinuxKernelBugWorkaround gpioBugWorkaround;

static PRU pru0(0);
static PRU pru1(1);

static LEDBuffer ledbuf(PANELS_ON_PRU0 + PANELS_ON_PRU1, PANEL_HEIGHT, 8 - PWM_BITS);

static void intHandler(int) {
    pru0.stop();
    pru1.stop();
    
    pru_exit_driver();
    
    exit(0);
}

int main() {
    pru_init_driver();
    
    signal(SIGINT, intHandler);    
    
    if (pru0.init() && pru1.init()) {
        
        pru0.setPanelGeometry(PWM_BITS, PANELS_ON_PRU0, PANEL_HEIGHT, PANELS_ON_PRU0_FLIP);
        pru1.setPanelGeometry(PWM_BITS, PANELS_ON_PRU1, PANEL_HEIGHT, PANELS_ON_PRU1_FLIP);

        pru0.setBrightnessGamma();
        pru1.setBrightnessGamma();

        pru0.start();
        pru1.start();
        
        for ( ; ; ) {
            clock_t timeA = clock();

            if (feof(stdin)) {
                break;
            }

            fread(ledbuf.buffer(), 1, ledbuf.size(), stdin);

            ledbuf.dither();
            
            // Left/right configuration, i.e. 4x1, 8x1 etc.
            // This code needs to be adjusted for other configurations like 4x2 or 2x2
            pru0.copyNewFrame(ledbuf.buffer() +                               0, (PANELS_ON_PRU0 + PANELS_ON_PRU1) * PANEL_WIDTH * 3);
            pru1.copyNewFrame(ledbuf.buffer() + PANELS_ON_PRU0 * PANEL_WIDTH *3, (PANELS_ON_PRU0 + PANELS_ON_PRU1) * PANEL_WIDTH * 3);
            
            clock_t timeB = clock();
            clock_t delta = timeB - timeA;

            usleep(std::max(int64_t(0), int64_t(1000000/70) - delta));
        }
        
        pru0.stop();
        pru1.stop();
        
    } else {
        printf("Could not initialize pru(s)!\n");
        exit(-1);
    }
    
    pru_exit_driver();
    return 0;
}
