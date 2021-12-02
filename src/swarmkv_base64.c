#include "swarmkv_base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cmove_bits(unsigned char src, unsigned lnum, unsigned rnum)
{
    src <<= lnum; // src = src << lnum;
    src >>= rnum; // src = src >> rnum;
    return src;
}

size_t base64_encode(const char *indata, size_t inlen, char *outdata)
{
    size_t out_len = -1;
    if (indata == NULL || inlen == 0)
    {
        return out_len;
    }

    size_t in_len = 0;
    size_t pad_num = 0;
    if (inlen % 3 != 0)
    {
        pad_num = 3 - inlen % 3;
    }
    in_len = inlen + pad_num;

    out_len = in_len * 8 / 6;
    char *p = outdata;
    for (int i = 0; i < in_len; i += 3)
    {
        int value = *indata >> 2;
        char c = base64_alphabet[value];
        *p = c;
        if (i == inlen + pad_num - 3 && pad_num != 0)
        {
            if (pad_num == 1)
            {
                *(p + 1) = base64_alphabet[(int)(cmove_bits(*indata, 6, 2) + cmove_bits(*(indata + 1), 0, 4))];
                *(p + 2) = base64_alphabet[(int)cmove_bits(*(indata + 1), 4, 2)];
                *(p + 3) = '=';
            }
            else if (pad_num == 2)
            {
                *(p + 1) = base64_alphabet[(int)cmove_bits(*indata, 6, 2)];
                *(p + 2) = '=';
                *(p + 3) = '=';
            }
        }
        else
        {
            *(p + 1) = base64_alphabet[cmove_bits(*indata, 6, 2) + cmove_bits(*(indata + 1), 0, 4)];
            *(p + 2) = base64_alphabet[cmove_bits(*(indata + 1), 4, 2) + cmove_bits(*(indata + 2), 0, 6)];
            *(p + 3) = base64_alphabet[*(indata + 2) & 0x3f];
        }
        p += 4;
        indata += 3;
    }

    return out_len;
}

size_t base64_decode(const char *indata, size_t inlen, char *outdata)
{
    size_t out_len = -1;
    int ret = 0;
    if (indata == NULL || outdata == NULL)
    {
        return out_len;
    }
    if (inlen % 4 != 0)
    {
        return out_len = -2;
    }

    int t = 0;
    int x = 0;
    int y = 0;
    int i = 0;
    unsigned char c = 0;
    int g = 3;

    while (indata[x] != 0)
    {
        c = base64_suffix_map[indata[x++]];
        if (c == 255)
            return -1;
        if (c == 253)
            continue;
        if (c == 254)
        {
            c = 0;
            g--;
        }
        t = (t << 6) | c;
        if (++y == 4)
        {
            outdata[i++] = (unsigned char)((t >> 16) & 0xff);
            if (g > 1)
                outdata[i++] = (unsigned char)((t >> 8) & 0xff);
            if (g > 2)
                outdata[i++] = (unsigned char)(t & 0xff);
            y = t = 0;
        }
    }
    out_len = i;
    return out_len;
}