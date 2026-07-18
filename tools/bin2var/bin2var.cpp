#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

void put_usage(void)
{
    fprintf(stderr, "bin2var /path/to/binary.rom varName [u8|u16|u16l|u16b]\n");
}

uint16_t tobe(uint16_t n)
{
    uint8_t w[2];
    memcpy(w, &n, 2);
    uint8_t ww = w[0];
    w[0] = w[1];
    w[1] = ww;
    memcpy(&n, w, 2);
    return n;
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        put_usage();
        return -1;
    }
    bool isU8 = true;
    bool isLE = true;
    if (4 <= argc) {
        if (0 == strcasecmp(argv[3], "u8")) {
            isU8 = true;
        } else if (0 == strcasecmp(argv[3], "u16") || 0 == strcasecmp(argv[3], "u16l")) {
            isU8 = false;
            isLE = true;
        } else if (0 == strcasecmp(argv[3], "u16b")) {
            isU8 = false;
            isLE = false;
        } else {
            put_usage();
            return -1;
        }
    }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "file open error\n");
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    int size = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    const char* varName = argv[2];
    printf("#include <vgs.h>\n\n");
    if (isU8) {
        printf("const uint8_t %s[%d] = {\n", varName, size);
        bool firstLine = true;
        while (1) {
            unsigned char buf[16];
            int readSize = (int)fread(buf, 1, sizeof(buf), fp);
            if (readSize < 1) {
                printf("\n");
                break;
            }
            if (firstLine) {
                firstLine = false;
            } else {
                printf(",\n");
            }
            printf("    ");
            for (int i = 0; i < readSize; i++) {
                if (i) {
                    printf(", 0x%02X", buf[i]);
                } else {
                    printf("0x%02X", buf[i]);
                }
            }
        }
    } else {
        printf("const uint16_t %s[%d] = {\n", varName, size / 2);
        bool firstLine = true;
        while (1) {
            unsigned short buf[8];
            int readSize = (int)fread(buf, 1, sizeof(buf), fp);
            readSize /= 2;
            if (readSize < 1) {
                printf("\n");
                break;
            }
            if (firstLine) {
                firstLine = false;
            } else {
                printf(",\n");
            }
            printf("    ");
            for (int i = 0; i < readSize; i++) {
                if (i) {
                    printf(", 0x%04X", isLE ? buf[i] : tobe(buf[i]));
                } else {
                    printf("0x%04X", isLE ? buf[i] : tobe(buf[i]));
                }
            }
        }
    }
    printf("};\n");
    fclose(fp);
    return 0;
}
