// x3dfix: patch and flash Allen & Heath Xone:3D firmware 1.26 so USB MIDI works on modern macOS.
//
// The stock firmware declares its USB-MIDI bulk endpoints (0x03 OUT, 0x83 IN) with
// wMaxPacketSize 4, which is illegal for full-speed bulk endpoints. Current macOS refuses to
// create them, so the Xone:3D's MIDI port never comes online. This tool takes the official
// XONE3D 1.26.BIN, checks it is byte-for-byte the known stock file, changes those endpoints to
// 8 bytes (the size of the hardware buffers the firmware already sets up), fixes the image
// checksum, and writes it to the mixer with A&H's own update protocol.
//
// All traffic is vendor control requests on endpoint 0, wValue 0x0100, wIndex 1:
//   0  IN  16  get version string            5  OUT  0  enter firmware update
//   1  IN   2  read routing word             6  IN   6  update status (busy, error counter)
//   2  OUT  2  write routing word            7  OUT 16  write one 16-byte firmware block
//
// Default is a dry run: checks and patches the file in memory, reads the mixer's version, and
// sends nothing that changes the mixer. Writing needs --flash.
//
// Build: clang -O2 -Wall -o x3dfix x3dfix.c -framework IOKit -framework CoreFoundation

#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define IMAGE_SIZE 0x10000
#define BLOCK      16
#define MAX_TRIES  50       // as in the original A&H utility

// SHA-256 of the official XONE3D 1.26.BIN and of the same file after this patch.
static const char *STOCK_SHA   = "19cb34f028995ec011bb77585c09d3138b5c2c4de0113c9050e90867cae6b2e2";
static const char *PATCHED_SHA = "f7fe65ba554a4201de5206836b70a376b81600ba8deffb669c7e624e179432a6";

// The whole patch: the six wMaxPacketSize bytes of endpoints 0x03/0x83 in the three
// copies of the configuration descriptor, and the low byte of the header checksum (+24).
static const struct { unsigned off; unsigned char from, to; } PATCH[] = {
    {0x00A, 0x67, 0x7F},
    {0x10B, 0x04, 0x08}, {0x119, 0x04, 0x08},
    {0x20A, 0x04, 0x08}, {0x218, 0x04, 0x08},
    {0x309, 0x04, 0x08}, {0x317, 0x04, 0x08},
};

static IOUSBDeviceInterface **dev;

static IOReturn vreq(UInt8 dirType, UInt8 bRequest, void *buf, UInt16 len, UInt32 *done) {
    IOUSBDevRequestTO r = {0};
    r.bmRequestType = dirType;          // 0xC2 IN or 0x42 OUT: vendor, endpoint recipient
    r.bRequest = bRequest;
    r.wValue = 0x0100;
    r.wIndex = 1;
    r.wLength = len;
    r.pData = buf;
    r.noDataTimeout = 5000;
    r.completionTimeout = 5000;
    IOReturn kr = (*dev)->DeviceRequestTO(dev, &r);
    if (done) *done = r.wLenDone;
    return kr;
}
#define IN  0xC2
#define OUT 0x42

static void sha256_hex(const unsigned char *buf, size_t len, char out[65]) {
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(buf, (CC_LONG)len, md);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) sprintf(out + 2 * i, "%02x", md[i]);
}

// Returns 0 if img now holds the patched image, -1 if the file is not one we know.
static int prepare_image(unsigned char *img, long len) {
    if (len != IMAGE_SIZE) { fprintf(stderr, "file is %ld bytes, expected %d\n", len, IMAGE_SIZE); return -1; }
    char sha[65];
    sha256_hex(img, IMAGE_SIZE, sha);
    if (!strcmp(sha, PATCHED_SHA)) {
        printf("image: already patched 1.26 MIDI fix\n");
        return 0;
    }
    if (strcmp(sha, STOCK_SHA)) {
        fprintf(stderr, "this is not the official XONE3D 1.26.BIN (sha256 %s).\n"
                        "Only that exact file is supported; refusing.\n", sha);
        return -1;
    }
    for (size_t i = 0; i < sizeof PATCH / sizeof *PATCH; i++) {
        if (img[PATCH[i].off] != PATCH[i].from) { fprintf(stderr, "unexpected byte at %#x\n", PATCH[i].off); return -1; }
        img[PATCH[i].off] = PATCH[i].to;
    }
    sha256_hex(img, IMAGE_SIZE, sha);
    if (strcmp(sha, PATCHED_SHA)) { fprintf(stderr, "internal error: patched image hash mismatch\n"); return -1; }
    printf("image: official 1.26, patched in memory (MIDI endpoints 4 -> 8 bytes, checksum 0x7267 -> 0x727f)\n");
    return 0;
}

static int open_device(void) {
    CFMutableDictionaryRef m = IOServiceMatching("IOUSBHostDevice");
    int vid = 0x0a4a, pid = 0xff00;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    CFDictionarySetValue(m, CFSTR(kUSBVendorID), v);
    CFDictionarySetValue(m, CFSTR(kUSBProductID), p);
    CFRelease(v); CFRelease(p);
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, m);
    if (!svc) { fprintf(stderr, "Xone:3D (0a4a:ff00) not found on USB\n"); return -1; }
    IOCFPlugInInterface **plug = NULL; SInt32 score;
    IOReturn kr = IOCreatePlugInInterfaceForService(svc, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
    IOObjectRelease(svc);
    if (kr || !plug) { fprintf(stderr, "plugin error 0x%08x\n", kr); return -1; }
    (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&dev);
    (*plug)->Release(plug);
    if (!dev) { fprintf(stderr, "no USB device interface\n"); return -1; }
    return 0;
}

static int read_version(char *out) {
    unsigned char v[17] = {0}; UInt32 n = 0;
    IOReturn kr = vreq(IN, 0, v, 16, &n);
    if (kr) { fprintf(stderr, "get version failed 0x%08x\n", kr); return -1; }
    v[16] = 0;
    strncpy(out, (char *)v, 16); out[16] = 0;
    return 0;
}

static const char *RECOVERY =
    "If the mixer misbehaves afterwards (dead display, mixer or LFO), power-cycle it and run\n"
    "x3dfix --flash again straight away. A repeated update has always recovered it so far.\n";

int main(int argc, char **argv) {
    const char *path = NULL, *save = NULL; int flash = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--flash")) flash = 1;
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: %s [--flash | --save OUT.BIN] \"XONE3D 1.26.BIN\"\n"
                        "  (no option)  check and patch in memory, read the mixer, change nothing\n"
                        "  --flash      write the patched firmware to the mixer\n"
                        "  --save OUT   write the patched image to a file, no mixer needed\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    static unsigned char img[IMAGE_SIZE + 1];
    long len = (long)fread(img, 1, sizeof img, f);
    fclose(f);
    if (prepare_image(img, len)) return 1;

    if (save) {
        FILE *o = fopen(save, "wb");
        if (!o || fwrite(img, 1, IMAGE_SIZE, o) != IMAGE_SIZE || fclose(o)) { perror(save); return 1; }
        printf("wrote %s\n", save);
        return 0;
    }

    if (open_device()) return 1;
    char ver[17];
    if (read_version(ver)) return 1;
    UInt16 routing = 0;
    if (vreq(IN, 1, &routing, 2, NULL)) { fprintf(stderr, "read routing failed\n"); return 1; }
    printf("mixer: firmware \"%s\", routing word 0x%04x\n", ver, routing);

    int blocks = 0;
    for (int off = BLOCK; off <= 0xFFFF; off += BLOCK) blocks++;   // the original never sends the last block
    printf("%s: %d blocks of %d bytes (0x0000-0x%04x), last 16 bytes not sent\n",
           flash ? "FLASHING" : "DRY RUN", blocks, BLOCK, blocks * BLOCK - 1);

    if (!flash) {
        printf("dry run complete, nothing was sent to the mixer. Add --flash to write it.\n");
        return 0;
    }

    // From here on an interruption leaves a half-written EEPROM: don't die on Ctrl-C / hangup.
    signal(SIGINT, SIG_IGN); signal(SIGHUP, SIG_IGN); signal(SIGTERM, SIG_IGN);

    IOReturn kr = vreq(OUT, 5, NULL, 0, NULL);
    if (kr) { fprintf(stderr, "start-update failed 0x%08x (nothing written yet)\n", kr); return 1; }
    printf("update mode started\n"); fflush(stdout);

    unsigned lastCounter = 0; long totalRetries = 0;
    for (int off = 0; ; ) {
        unsigned char block[BLOCK];
        memcpy(block, img + off, BLOCK);
        off += BLOCK;
        if (off > 0xFFFF) break;

        int tries;
        for (tries = 0; tries < MAX_TRIES; tries++) {
            kr = vreq(OUT, 7, block, BLOCK, NULL);
            if (kr) { printf("block 0x%04x: write error 0x%08x\n", off - BLOCK, kr); fflush(stdout); }

            unsigned char st[6];
            memset(st, 0xFF, 5);
            int polls = 0;
            do {
                usleep(10000);
                kr = vreq(IN, 6, st, 6, NULL);
                if (kr) {
                    fprintf(stderr, "\nSTATUS READ FAILED 0x%08x at block 0x%04x.\n%s", kr, off - BLOCK, RECOVERY);
                    return 3;
                }
                if (++polls % 500 == 0) {                         // ~5 s: report, give up after ~60 s
                    printf("block 0x%04x: still busy after %d polls, status %02x %02x %02x %02x %02x %02x\n",
                           off - BLOCK, polls, st[0], st[1], st[2], st[3], st[4], st[5]);
                    fflush(stdout);
                    if (polls >= 6000) {
                        fprintf(stderr, "\nMIXER STAYED BUSY at block 0x%04x. Stopping.\n%s", off - BLOCK, RECOVERY);
                        return 4;
                    }
                }
            } while (st[5] & 1);                                  // busy

            unsigned counter = st[2] << 8 | st[3];
            if (counter == lastCounter) break;                    // no new error: block accepted
            lastCounter = counter;                                // error counter moved: resend
            if (tries > 0) { printf("block 0x%04x: retrying (%d)\n", off - BLOCK, tries); usleep(100000); }
        }
        if (tries >= MAX_TRIES) {
            fprintf(stderr, "\nBLOCK 0x%04x FAILED after %d tries.\n%s", off - BLOCK, MAX_TRIES, RECOVERY);
            return 3;
        }
        totalRetries += tries;
        if ((off / BLOCK) % 256 == 0) { printf("  %3d%%  (0x%04x)\n", off * 100 / IMAGE_SIZE, off); fflush(stdout); }
    }
    printf("firmware update complete (%ld retries). Power the Xone:3D off, wait, and on again.\n", totalRetries);
    return 0;
}
