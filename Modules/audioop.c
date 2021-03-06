
/* audioopmodule - Module to detect peak values in arrays */

#define PY_SSIZE_T_CLEAN

#include "Python.h"

typedef short PyInt16;

#if defined(__CHAR_UNSIGNED__)
#if defined(signed)
/* This module currently does not work on systems where only unsigned
   characters are available.  Take it out of Setup.  Sorry. */
#endif
#endif

static const int maxvals[] = {0, 0x7F, 0x7FFF, 0x7FFFFF, 0x7FFFFFFF};
/* -1 trick is needed on Windows to support -0x80000000 without a warning */
static const int minvals[] = {0, -0x80, -0x8000, -0x800000, -0x7FFFFFFF-1};
static const unsigned int masks[] = {0, 0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFFFF};

static int
fbound(double val, double minval, double maxval)
{
    if (val > maxval)
        val = maxval;
    else if (val < minval + 1)
        val = minval;
    return (int)val;
}


/* Code shamelessly stolen from sox, 12.17.7, g711.c
** (c) Craig Reese, Joe Campbell and Jeff Poskanzer 1989 */

/* From g711.c:
 *
 * December 30, 1994:
 * Functions linear2alaw, linear2ulaw have been updated to correctly
 * convert unquantized 16 bit values.
 * Tables for direct u- to A-law and A- to u-law conversions have been
 * corrected.
 * Borge Lindberg, Center for PersonKommunikation, Aalborg University.
 * bli@cpk.auc.dk
 *
 */
#define BIAS 0x84   /* define the add-in bias for 16 bit samples */
#define CLIP 32635
#define SIGN_BIT        (0x80)          /* Sign bit for a A-law byte. */
#define QUANT_MASK      (0xf)           /* Quantization field mask. */
#define SEG_SHIFT       (4)             /* Left shift for segment number. */
#define SEG_MASK        (0x70)          /* Segment field mask. */

static PyInt16 seg_aend[8] = {0x1F, 0x3F, 0x7F, 0xFF,
                              0x1FF, 0x3FF, 0x7FF, 0xFFF};
static PyInt16 seg_uend[8] = {0x3F, 0x7F, 0xFF, 0x1FF,
                              0x3FF, 0x7FF, 0xFFF, 0x1FFF};

static PyInt16
search(PyInt16 val, PyInt16 *table, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        if (val <= *table++)
            return (i);
    }
    return (size);
}
#define st_ulaw2linear16(uc) (_st_ulaw2linear16[uc])
#define st_alaw2linear16(uc) (_st_alaw2linear16[uc])

static PyInt16 _st_ulaw2linear16[256] = {
    -32124,  -31100,  -30076,  -29052,  -28028,  -27004,  -25980,
    -24956,  -23932,  -22908,  -21884,  -20860,  -19836,  -18812,
    -17788,  -16764,  -15996,  -15484,  -14972,  -14460,  -13948,
    -13436,  -12924,  -12412,  -11900,  -11388,  -10876,  -10364,
     -9852,   -9340,   -8828,   -8316,   -7932,   -7676,   -7420,
     -7164,   -6908,   -6652,   -6396,   -6140,   -5884,   -5628,
     -5372,   -5116,   -4860,   -4604,   -4348,   -4092,   -3900,
     -3772,   -3644,   -3516,   -3388,   -3260,   -3132,   -3004,
     -2876,   -2748,   -2620,   -2492,   -2364,   -2236,   -2108,
     -1980,   -1884,   -1820,   -1756,   -1692,   -1628,   -1564,
     -1500,   -1436,   -1372,   -1308,   -1244,   -1180,   -1116,
     -1052,    -988,    -924,    -876,    -844,    -812,    -780,
      -748,    -716,    -684,    -652,    -620,    -588,    -556,
      -524,    -492,    -460,    -428,    -396,    -372,    -356,
      -340,    -324,    -308,    -292,    -276,    -260,    -244,
      -228,    -212,    -196,    -180,    -164,    -148,    -132,
      -120,    -112,    -104,     -96,     -88,     -80,     -72,
       -64,     -56,     -48,     -40,     -32,     -24,     -16,
    -8,       0,   32124,   31100,   30076,   29052,   28028,
     27004,   25980,   24956,   23932,   22908,   21884,   20860,
     19836,   18812,   17788,   16764,   15996,   15484,   14972,
     14460,   13948,   13436,   12924,   12412,   11900,   11388,
     10876,   10364,    9852,    9340,    8828,    8316,    7932,
      7676,    7420,    7164,    6908,    6652,    6396,    6140,
      5884,    5628,    5372,    5116,    4860,    4604,    4348,
      4092,    3900,    3772,    3644,    3516,    3388,    3260,
      3132,    3004,    2876,    2748,    2620,    2492,    2364,
      2236,    2108,    1980,    1884,    1820,    1756,    1692,
      1628,    1564,    1500,    1436,    1372,    1308,    1244,
      1180,    1116,    1052,     988,     924,     876,     844,
       812,     780,     748,     716,     684,     652,     620,
       588,     556,     524,     492,     460,     428,     396,
       372,     356,     340,     324,     308,     292,     276,
       260,     244,     228,     212,     196,     180,     164,
       148,     132,     120,     112,     104,      96,      88,
    80,      72,      64,      56,      48,      40,      32,
    24,      16,       8,       0
};

/*
 * linear2ulaw() accepts a 14-bit signed integer and encodes it as u-law data
 * stored in a unsigned char.  This function should only be called with
 * the data shifted such that it only contains information in the lower
 * 14-bits.
 *
 * In order to simplify the encoding process, the original linear magnitude
 * is biased by adding 33 which shifts the encoding range from (0 - 8158) to
 * (33 - 8191). The result can be seen in the following encoding table:
 *
 *      Biased Linear Input Code        Compressed Code
 *      ------------------------        ---------------
 *      00000001wxyza                   000wxyz
 *      0000001wxyzab                   001wxyz
 *      000001wxyzabc                   010wxyz
 *      00001wxyzabcd                   011wxyz
 *      0001wxyzabcde                   100wxyz
 *      001wxyzabcdef                   101wxyz
 *      01wxyzabcdefg                   110wxyz
 *      1wxyzabcdefgh                   111wxyz
 *
 * Each biased linear code has a leading 1 which identifies the segment
 * number. The value of the segment number is equal to 7 minus the number
 * of leading 0's. The quantization interval is directly available as the
 * four bits wxyz.  * The trailing bits (a - h) are ignored.
 *
 * Ordinarily the complement of the resulting code word is used for
 * transmission, and so the code word is complemented before it is returned.
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
static unsigned char
st_14linear2ulaw(PyInt16 pcm_val)       /* 2's complement (14-bit range) */
{
    PyInt16         mask;
    PyInt16         seg;
    unsigned char   uval;

    /* u-law inverts all bits */
    /* Get the sign and the magnitude of the value. */
    if (pcm_val < 0) {
        pcm_val = -pcm_val;
        mask = 0x7F;
    } else {
        mask = 0xFF;
    }
    if ( pcm_val > CLIP ) pcm_val = CLIP;           /* clip the magnitude */
    pcm_val += (BIAS >> 2);

    /* Convert the scaled magnitude to segment number. */
    seg = search(pcm_val, seg_uend, 8);

    /*
     * Combine the sign, segment, quantization bits;
     * and complement the code word.
     */
    if (seg >= 8)           /* out of range, return maximum value. */
        return (unsigned char) (0x7F ^ mask);
    else {
        uval = (unsigned char) (seg << 4) | ((pcm_val >> (seg + 1)) & 0xF);
        return (uval ^ mask);
    }

}

static PyInt16 _st_alaw2linear16[256] = {
     -5504,   -5248,   -6016,   -5760,   -4480,   -4224,   -4992,
     -4736,   -7552,   -7296,   -8064,   -7808,   -6528,   -6272,
     -7040,   -6784,   -2752,   -2624,   -3008,   -2880,   -2240,
     -2112,   -2496,   -2368,   -3776,   -3648,   -4032,   -3904,
     -3264,   -3136,   -3520,   -3392,  -22016,  -20992,  -24064,
    -23040,  -17920,  -16896,  -19968,  -18944,  -30208,  -29184,
    -32256,  -31232,  -26112,  -25088,  -28160,  -27136,  -11008,
    -10496,  -12032,  -11520,   -8960,   -8448,   -9984,   -9472,
    -15104,  -14592,  -16128,  -15616,  -13056,  -12544,  -14080,
    -13568,    -344,    -328,    -376,    -360,    -280,    -264,
      -312,    -296,    -472,    -456,    -504,    -488,    -408,
      -392,    -440,    -424,     -88,     -72,    -120,    -104,
       -24,      -8,     -56,     -40,    -216,    -200,    -248,
      -232,    -152,    -136,    -184,    -168,   -1376,   -1312,
     -1504,   -1440,   -1120,   -1056,   -1248,   -1184,   -1888,
     -1824,   -2016,   -1952,   -1632,   -1568,   -1760,   -1696,
      -688,    -656,    -752,    -720,    -560,    -528,    -624,
      -592,    -944,    -912,   -1008,    -976,    -816,    -784,
      -880,    -848,    5504,    5248,    6016,    5760,    4480,
      4224,    4992,    4736,    7552,    7296,    8064,    7808,
      6528,    6272,    7040,    6784,    2752,    2624,    3008,
      2880,    2240,    2112,    2496,    2368,    3776,    3648,
      4032,    3904,    3264,    3136,    3520,    3392,   22016,
     20992,   24064,   23040,   17920,   16896,   19968,   18944,
     30208,   29184,   32256,   31232,   26112,   25088,   28160,
     27136,   11008,   10496,   12032,   11520,    8960,    8448,
      9984,    9472,   15104,   14592,   16128,   15616,   13056,
     12544,   14080,   13568,     344,     328,     376,     360,
       280,     264,     312,     296,     472,     456,     504,
       488,     408,     392,     440,     424,      88,      72,
       120,     104,      24,       8,      56,      40,     216,
       200,     248,     232,     152,     136,     184,     168,
      1376,    1312,    1504,    1440,    1120,    1056,    1248,
      1184,    1888,    1824,    2016,    1952,    1632,    1568,
      1760,    1696,     688,     656,     752,     720,     560,
       528,     624,     592,     944,     912,    1008,     976,
       816,     784,     880,     848
};

/*
 * linear2alaw() accepts an 13-bit signed integer and encodes it as A-law data
 * stored in a unsigned char.  This function should only be called with
 * the data shifted such that it only contains information in the lower
 * 13-bits.
 *
 *              Linear Input Code       Compressed Code
 *      ------------------------        ---------------
 *      0000000wxyza                    000wxyz
 *      0000001wxyza                    001wxyz
 *      000001wxyzab                    010wxyz
 *      00001wxyzabc                    011wxyz
 *      0001wxyzabcd                    100wxyz
 *      001wxyzabcde                    101wxyz
 *      01wxyzabcdef                    110wxyz
 *      1wxyzabcdefg                    111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
static unsigned char
st_linear2alaw(PyInt16 pcm_val) /* 2's complement (13-bit range) */
{
    PyInt16         mask;
    short           seg;
    unsigned char   aval;

    /* A-law using even bit inversion */
    if (pcm_val >= 0) {
        mask = 0xD5;            /* sign (7th) bit = 1 */
    } else {
        mask = 0x55;            /* sign bit = 0 */
        pcm_val = -pcm_val - 1;
    }

    /* Convert the scaled magnitude to segment number. */
    seg = search(pcm_val, seg_aend, 8);

    /* Combine the sign, segment, and quantization bits. */

    if (seg >= 8)           /* out of range, return maximum value. */
        return (unsigned char) (0x7F ^ mask);
    else {
        aval = (unsigned char) seg << SEG_SHIFT;
        if (seg < 2)
            aval |= (pcm_val >> 1) & QUANT_MASK;
        else
            aval |= (pcm_val >> seg) & QUANT_MASK;
        return (aval ^ mask);
    }
}
/* End of code taken from sox */

/* Intel ADPCM step variation table */
static int indexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static int stepsizeTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

#define GETINTX(T, cp, i)  (*(T *)((unsigned char *)(cp) + (i)))
#define SETINTX(T, cp, i, val)  do {                    \
        *(T *)((unsigned char *)(cp) + (i)) = (T)(val); \
    } while (0)


#define GETINT8(cp, i)          GETINTX(signed char, (cp), (i))
#define GETINT16(cp, i)         GETINTX(short, (cp), (i))
#define GETINT32(cp, i)         GETINTX(PY_INT32_T, (cp), (i))

#if WORDS_BIGENDIAN
#define GETINT24(cp, i)  (                              \
        ((unsigned char *)(cp) + (i))[2] +              \
        (((unsigned char *)(cp) + (i))[1] << 8) +       \
        (((signed char *)(cp) + (i))[0] << 16) )
#else
#define GETINT24(cp, i)  (                              \
        ((unsigned char *)(cp) + (i))[0] +              \
        (((unsigned char *)(cp) + (i))[1] << 8) +       \
        (((signed char *)(cp) + (i))[2] << 16) )
#endif


#define SETINT8(cp, i, val)     SETINTX(signed char, (cp), (i), (val))
#define SETINT16(cp, i, val)    SETINTX(short, (cp), (i), (val))
#define SETINT32(cp, i, val)    SETINTX(PY_INT32_T, (cp), (i), (val))

#if WORDS_BIGENDIAN
#define SETINT24(cp, i, val)  do {                              \
        ((unsigned char *)(cp) + (i))[2] = (int)(val);          \
        ((unsigned char *)(cp) + (i))[1] = (int)(val) >> 8;     \
        ((signed char *)(cp) + (i))[0] = (int)(val) >> 16;      \
    } while (0)
#else
#define SETINT24(cp, i, val)  do {                              \
        ((unsigned char *)(cp) + (i))[0] = (int)(val);          \
        ((unsigned char *)(cp) + (i))[1] = (int)(val) >> 8;     \
        ((signed char *)(cp) + (i))[2] = (int)(val) >> 16;      \
    } while (0)
#endif


#define GETRAWSAMPLE(size, cp, i)  (                    \
        (size == 1) ? (int)GETINT8((cp), (i)) :         \
        (size == 2) ? (int)GETINT16((cp), (i)) :        \
        (size == 3) ? (int)GETINT24((cp), (i)) :        \
                      (int)GETINT32((cp), (i)))

#define SETRAWSAMPLE(size, cp, i, val)  do {    \
        if (size == 1)                          \
            SETINT8((cp), (i), (val));          \
        else if (size == 2)                     \
            SETINT16((cp), (i), (val));         \
        else if (size == 3)                     \
            SETINT24((cp), (i), (val));         \
        else                                    \
            SETINT32((cp), (i), (val));         \
    } while(0)


#define GETSAMPLE32(size, cp, i)  (                     \
        (size == 1) ? (int)GETINT8((cp), (i)) << 24 :   \
        (size == 2) ? (int)GETINT16((cp), (i)) << 16 :  \
        (size == 3) ? (int)GETINT24((cp), (i)) << 8 :   \
                      (int)GETINT32((cp), (i)))

#define SETSAMPLE32(size, cp, i, val)  do {     \
        if (size == 1)                          \
            SETINT8((cp), (i), (val) >> 24);    \
        else if (size == 2)                     \
            SETINT16((cp), (i), (val) >> 16);   \
        else if (size == 3)                     \
            SETINT24((cp), (i), (val) >> 8);    \
        else                                    \
            SETINT32((cp), (i), (val));         \
    } while(0)


static PyObject *AudioopError;

static int
audioop_check_size(int size)
{
    if (size < 1 || size > 4) {
        PyErr_SetString(AudioopError, "Size should be 1, 2, 3 or 4");
        return 0;
    }
    else
        return 1;
}

static int
audioop_check_parameters(Py_ssize_t len, int size)
{
    if (!audioop_check_size(size))
        return 0;
    if (len % size != 0) {
        PyErr_SetString(AudioopError, "not a whole number of frames");
        return 0;
    }
    return 1;
}

static PyObject *
audioop_getsample(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size;
    int val;

    if (!PyArg_ParseTuple(args, "y*in:getsample", &view, &size, &i))
        return NULL;
    if (!audioop_check_parameters(view.len, size))
        goto error;
    if (i < 0 || i >= view.len/size) {
        PyErr_SetString(AudioopError, "Index out of range");
        goto error;
    }
    val = GETRAWSAMPLE(size, view.buf, i*size);
    PyBuffer_Release(&view);
    return PyLong_FromLong(val);

  error:
    PyBuffer_Release(&view);
    return NULL;
}

static PyObject *
audioop_max(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size;
    unsigned int absval, max = 0;

    if (!PyArg_ParseTuple(args, "y*i:max", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    for (i = 0; i < view.len; i += size) {
        int val = GETRAWSAMPLE(size, view.buf, i);
        if (val < 0) absval = (-val);
        else absval = val;
        if (absval > max) max = absval;
    }
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(max);
}

static PyObject *
audioop_minmax(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size;
    /* -1 trick below is needed on Windows to support -0x80000000 without
    a warning */
    int min = 0x7fffffff, max = -0x7FFFFFFF-1;

    if (!PyArg_ParseTuple(args, "y*i:minmax", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    for (i = 0; i < view.len; i += size) {
        int val = GETRAWSAMPLE(size, view.buf, i);
        if (val > max) max = val;
        if (val < min) min = val;
    }
    PyBuffer_Release(&view);
    return Py_BuildValue("(ii)", min, max);
}

static PyObject *
audioop_avg(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size, avg;
    double sum = 0.0;

    if (!PyArg_ParseTuple(args, "y*i:avg", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    for (i = 0; i < view.len; i += size)
        sum += GETRAWSAMPLE(size, view.buf, i);
    if (view.len == 0)
        avg = 0;
    else
        avg = (int)floor(sum / (double)(view.len/size));
    PyBuffer_Release(&view);
    return PyLong_FromLong(avg);
}

static PyObject *
audioop_rms(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size;
    unsigned int res;
    double sum_squares = 0.0;

    if (!PyArg_ParseTuple(args, "y*i:rms", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    for (i = 0; i < view.len; i += size) {
        double val = GETRAWSAMPLE(size, view.buf, i);
        sum_squares += val*val;
    }
    if (view.len == 0)
        res = 0;
    else
        res = (unsigned int)sqrt(sum_squares / (double)(view.len/size));
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(res);
}

static double _sum2(const short *a, const short *b, Py_ssize_t len)
{
    Py_ssize_t i;
    double sum = 0.0;

    for( i=0; i<len; i++) {
        sum = sum + (double)a[i]*(double)b[i];
    }
    return sum;
}

/*
** Findfit tries to locate a sample within another sample. Its main use
** is in echo-cancellation (to find the feedback of the output signal in
** the input signal).
** The method used is as follows:
**
** let R be the reference signal (length n) and A the input signal (length N)
** with N > n, and let all sums be over i from 0 to n-1.
**
** Now, for each j in {0..N-n} we compute a factor fj so that -fj*R matches A
** as good as possible, i.e. sum( (A[j+i]+fj*R[i])^2 ) is minimal. This
** equation gives fj = sum( A[j+i]R[i] ) / sum(R[i]^2).
**
** Next, we compute the relative distance between the original signal and
** the modified signal and minimize that over j:
** vj = sum( (A[j+i]-fj*R[i])^2 ) / sum( A[j+i]^2 )  =>
** vj = ( sum(A[j+i]^2)*sum(R[i]^2) - sum(A[j+i]R[i])^2 ) / sum( A[j+i]^2 )
**
** In the code variables correspond as follows:
** cp1          A
** cp2          R
** len1         N
** len2         n
** aj_m1        A[j-1]
** aj_lm1       A[j+n-1]
** sum_ri_2     sum(R[i]^2)
** sum_aij_2    sum(A[i+j]^2)
** sum_aij_ri   sum(A[i+j]R[i])
**
** sum_ri is calculated once, sum_aij_2 is updated each step and sum_aij_ri
** is completely recalculated each step.
*/
static PyObject *
audioop_findfit(PyObject *self, PyObject *args)
{
    Py_buffer view1;
    Py_buffer view2;
    const short *cp1, *cp2;
    Py_ssize_t len1, len2;
    Py_ssize_t j, best_j;
    double aj_m1, aj_lm1;
    double sum_ri_2, sum_aij_2, sum_aij_ri, result, best_result, factor;

    if (!PyArg_ParseTuple(args, "y*y*:findfit", &view1, &view2))
        return NULL;
    if (view1.len & 1 || view2.len & 1) {
        PyErr_SetString(AudioopError, "Strings should be even-sized");
        goto error;
    }
    cp1 = (const short *)view1.buf;
    len1 = view1.len >> 1;
    cp2 = (const short *)view2.buf;
    len2 = view2.len >> 1;

    if (len1 < len2) {
        PyErr_SetString(AudioopError, "First sample should be longer");
        goto error;
    }
    sum_ri_2 = _sum2(cp2, cp2, len2);
    sum_aij_2 = _sum2(cp1, cp1, len2);
    sum_aij_ri = _sum2(cp1, cp2, len2);

    result = (sum_ri_2*sum_aij_2 - sum_aij_ri*sum_aij_ri) / sum_aij_2;

    best_result = result;
    best_j = 0;

    for ( j=1; j<=len1-len2; j++) {
        aj_m1 = (double)cp1[j-1];
        aj_lm1 = (double)cp1[j+len2-1];

        sum_aij_2 = sum_aij_2 + aj_lm1*aj_lm1 - aj_m1*aj_m1;
        sum_aij_ri = _sum2(cp1+j, cp2, len2);

        result = (sum_ri_2*sum_aij_2 - sum_aij_ri*sum_aij_ri)
            / sum_aij_2;

        if ( result < best_result ) {
            best_result = result;
            best_j = j;
        }

    }

    factor = _sum2(cp1+best_j, cp2, len2) / sum_ri_2;

    PyBuffer_Release(&view1);
    PyBuffer_Release(&view2);
    return Py_BuildValue("(nf)", best_j, factor);

  error:
    PyBuffer_Release(&view1);
    PyBuffer_Release(&view2);
    return NULL;
}

/*
** findfactor finds a factor f so that the energy in A-fB is minimal.
** See the comment for findfit for details.
*/
static PyObject *
audioop_findfactor(PyObject *self, PyObject *args)
{
    Py_buffer view1;
    Py_buffer view2;
    const short *cp1, *cp2;
    Py_ssize_t len;
    double sum_ri_2, sum_aij_ri, result;

    if (!PyArg_ParseTuple(args, "y*y*:findfactor", &view1, &view2))
        return NULL;
    if (view1.len & 1 || view2.len & 1) {
        PyErr_SetString(AudioopError, "Strings should be even-sized");
        goto error;
    }
    if (view1.len != view2.len) {
        PyErr_SetString(AudioopError, "Samples should be same size");
        goto error;
    }
    cp1 = (const short *)view1.buf;
    cp2 = (const short *)view2.buf;
    len = view1.len >> 1;
    sum_ri_2 = _sum2(cp2, cp2, len);
    sum_aij_ri = _sum2(cp1, cp2, len);

    result = sum_aij_ri / sum_ri_2;

    PyBuffer_Release(&view1);
    PyBuffer_Release(&view2);
    return PyFloat_FromDouble(result);

  error:
    PyBuffer_Release(&view1);
    PyBuffer_Release(&view2);
    return NULL;
}

/*
** findmax returns the index of the n-sized segment of the input sample
** that contains the most energy.
*/
static PyObject *
audioop_findmax(PyObject *self, PyObject *args)
{
    Py_buffer view;
    const short *cp1;
    Py_ssize_t len1, len2;
    Py_ssize_t j, best_j;
    double aj_m1, aj_lm1;
    double result, best_result;

    if (!PyArg_ParseTuple(args, "y*n:findmax", &view, &len2))
        return NULL;
    if (view.len & 1) {
        PyErr_SetString(AudioopError, "Strings should be even-sized");
        goto error;
    }
    cp1 = (const short *)view.buf;
    len1 = view.len >> 1;

    if (len2 < 0 || len1 < len2) {
        PyErr_SetString(AudioopError, "Input sample should be longer");
        goto error;
    }

    result = _sum2(cp1, cp1, len2);

    best_result = result;
    best_j = 0;

    for ( j=1; j<=len1-len2; j++) {
        aj_m1 = (double)cp1[j-1];
        aj_lm1 = (double)cp1[j+len2-1];

        result = result + aj_lm1*aj_lm1 - aj_m1*aj_m1;

        if ( result > best_result ) {
            best_result = result;
            best_j = j;
        }

    }

    PyBuffer_Release(&view);
    return PyLong_FromSsize_t(best_j);

  error:
    PyBuffer_Release(&view);
    return NULL;
}

static PyObject *
audioop_avgpp(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size, prevval, prevextremevalid = 0,
        prevextreme = 0;
    double sum = 0.0;
    unsigned int avg;
    int diff, prevdiff, nextreme = 0;

    if (!PyArg_ParseTuple(args, "y*i:avgpp", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    if (view.len <= size) {
        PyBuffer_Release(&view);
        return PyLong_FromLong(0);
    }
    prevval = GETRAWSAMPLE(size, view.buf, 0);
    prevdiff = 17; /* Anything != 0, 1 */
    for (i = size; i < view.len; i += size) {
        int val = GETRAWSAMPLE(size, view.buf, i);
        if (val != prevval) {
            diff = val < prevval;
            if (prevdiff == !diff) {
                /* Derivative changed sign. Compute difference to last
                ** extreme value and remember.
                */
                if (prevextremevalid) {
                    if (prevval < prevextreme)
                        sum += (double)((unsigned int)prevextreme -
                                        (unsigned int)prevval);
                    else
                        sum += (double)((unsigned int)prevval -
                                        (unsigned int)prevextreme);
                    nextreme++;
                }
                prevextremevalid = 1;
                prevextreme = prevval;
            }
            prevval = val;
            prevdiff = diff;
        }
    }
    if ( nextreme == 0 )
        avg = 0;
    else
        avg = (unsigned int)(sum / (double)nextreme);
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(avg);
}

static PyObject *
audioop_maxpp(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size, prevval, prevextremevalid = 0,
        prevextreme = 0;
    unsigned int max = 0, extremediff;
    int diff, prevdiff;

    if (!PyArg_ParseTuple(args, "y*i:maxpp", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    if (view.len <= size) {
        PyBuffer_Release(&view);
        return PyLong_FromLong(0);
    }
    prevval = GETRAWSAMPLE(size, view.buf, 0);
    prevdiff = 17; /* Anything != 0, 1 */
    for (i = size; i < view.len; i += size) {
        int val = GETRAWSAMPLE(size, view.buf, i);
        if (val != prevval) {
            diff = val < prevval;
            if (prevdiff == !diff) {
                /* Derivative changed sign. Compute difference to
                ** last extreme value and remember.
                */
                if (prevextremevalid) {
                    if (prevval < prevextreme)
                        extremediff = (unsigned int)prevextreme -
                                      (unsigned int)prevval;
                    else
                        extremediff = (unsigned int)prevval -
                                      (unsigned int)prevextreme;
                    if ( extremediff > max )
                        max = extremediff;
                }
                prevextremevalid = 1;
                prevextreme = prevval;
            }
            prevval = val;
            prevdiff = diff;
        }
    }
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(max);
}

static PyObject *
audioop_cross(PyObject *self, PyObject *args)
{
    Py_buffer view;
    Py_ssize_t i;
    int size;
    int prevval;
    Py_ssize_t ncross;

    if (!PyArg_ParseTuple(args, "y*i:cross", &view, &size))
        return NULL;
    if (!audioop_check_parameters(view.len, size)) {
        PyBuffer_Release(&view);
        return NULL;
    }
    ncross = -1;
    prevval = 17; /* Anything <> 0,1 */
    for (i = 0; i < view.len; i += size) {
        int val = GETRAWSAMPLE(size, view.buf, i) < 0;
        if (val != prevval) ncross++;
        prevval = val;
    }
    PyBuffer_Release(&view);
    return PyLong_FromSsize_t(ncross);
}

static PyObject *
audioop_mul(PyObject *self, PyObject *args)
{
    Py_buffer view;
    signed char *ncp;
    Py_ssize_t i;
    int size;
    double factor, maxval, minval;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*id:mul", &view, &size, &factor))
        return NULL;
    if (!audioop_check_parameters(view.len, size))
        goto exit;

    maxval = (double) maxvals[size];
    minval = (double) minvals[size];

    rv = PyBytes_FromStringAndSize(NULL, view.len);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);

    for (i = 0; i < view.len; i += size) {
        double val = GETRAWSAMPLE(size, view.buf, i);
        val *= factor;
        val = floor(fbound(val, minval, maxval));
        SETRAWSAMPLE(size, ncp, i, (int)val);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_tomono(PyObject *self, PyObject *args)
{
    Py_buffer pcp;
    signed char *cp, *ncp;
    Py_ssize_t len, i;
    int size;
    double fac1, fac2, maxval, minval;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*idd:tomono",
                          &pcp, &size, &fac1, &fac2))
        return NULL;
    cp = pcp.buf;
    len = pcp.len;
    if (!audioop_check_parameters(len, size))
        goto exit;
    if (((len / size) & 1) != 0) {
        PyErr_SetString(AudioopError, "not a whole number of frames");
        goto exit;
    }

    maxval = (double) maxvals[size];
    minval = (double) minvals[size];

    rv = PyBytes_FromStringAndSize(NULL, len/2);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);

    for (i = 0; i < len; i += size*2) {
        double val1 = GETRAWSAMPLE(size, cp, i);
        double val2 = GETRAWSAMPLE(size, cp, i + size);
        double val = val1*fac1 + val2*fac2;
        val = floor(fbound(val, minval, maxval));
        SETRAWSAMPLE(size, ncp, i/2, val);
    }
  exit:
    PyBuffer_Release(&pcp);
    return rv;
}

static PyObject *
audioop_tostereo(PyObject *self, PyObject *args)
{
    Py_buffer view;
    signed char *ncp;
    Py_ssize_t i;
    int size;
    double fac1, fac2, maxval, minval;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*idd:tostereo",
                          &view, &size, &fac1, &fac2))
        return NULL;
    if (!audioop_check_parameters(view.len, size))
        goto exit;

    maxval = (double) maxvals[size];
    minval = (double) minvals[size];

    if (view.len > PY_SSIZE_T_MAX/2) {
        PyErr_SetString(PyExc_MemoryError,
                        "not enough memory for output buffer");
        goto exit;
    }

    rv = PyBytes_FromStringAndSize(NULL, view.len*2);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);

    for (i = 0; i < view.len; i += size) {
        double val = GETRAWSAMPLE(size, view.buf, i);
        int val1 = (int)floor(fbound(val*fac1, minval, maxval));
        int val2 = (int)floor(fbound(val*fac2, minval, maxval));
        SETRAWSAMPLE(size, ncp, i*2, val1);
        SETRAWSAMPLE(size, ncp, i*2 + size, val2);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_add(PyObject *self, PyObject *args)
{
    Py_buffer view1;
    Py_buffer view2;
    signed char *ncp;
    Py_ssize_t i;
    int size, minval, maxval, newval;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*y*i:add",
                          &view1, &view2, &size))
        return NULL;
    if (!audioop_check_parameters(view1.len, size))
        goto exit;
    if (view1.len != view2.len) {
        PyErr_SetString(AudioopError, "Lengths should be the same");
        goto exit;
    }

    maxval = maxvals[size];
    minval = minvals[size];

    rv = PyBytes_FromStringAndSize(NULL, view1.len);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);

    for (i = 0; i < view1.len; i += size) {
        int val1 = GETRAWSAMPLE(size, view1.buf, i);
        int val2 = GETRAWSAMPLE(size, view2.buf, i);

        if (size < 4) {
            newval = val1 + val2;
            /* truncate in case of overflow */
            if (newval > maxval)
                newval = maxval;
            else if (newval < minval)
                newval = minval;
        }
        else {
            double fval = (double)val1 + (double)val2;
            /* truncate in case of overflow */
            newval = (int)floor(fbound(fval, minval, maxval));
        }

        SETRAWSAMPLE(size, ncp, i, newval);
    }
  exit:
    PyBuffer_Release(&view1);
    PyBuffer_Release(&view2);
    return rv;
}

static PyObject *
audioop_bias(PyObject *self, PyObject *args)
{
    Py_buffer view;
    signed char *ncp;
    Py_ssize_t i;
    int size, bias;
    unsigned int val = 0, mask;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*ii:bias",
                          &view, &size, &bias))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;

    rv = PyBytes_FromStringAndSize(NULL, view.len);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);

    mask = masks[size];

    for (i = 0; i < view.len; i += size) {
        if (size == 1)
            val = GETINTX(unsigned char, view.buf, i);
        else if (size == 2)
            val = GETINTX(unsigned short, view.buf, i);
        else if (size == 3)
            val = ((unsigned int)GETINT24(view.buf, i)) & 0xffffffu;
        else {
            assert(size == 4);
            val = GETINTX(PY_UINT32_T, view.buf, i);
        }

        val += (unsigned int)bias;
        /* wrap around in case of overflow */
        val &= mask;

        if (size == 1)
            SETINTX(unsigned char, ncp, i, val);
        else if (size == 2)
            SETINTX(unsigned short, ncp, i, val);
        else if (size == 3)
            SETINT24(ncp, i, (int)val);
        else {
            assert(size == 4);
            SETINTX(PY_UINT32_T, ncp, i, val);
        }
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_reverse(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *ncp;
    Py_ssize_t i;
    int size;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*i:reverse",
                          &view, &size))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;

    rv = PyBytes_FromStringAndSize(NULL, view.len);
    if (rv == NULL)
        goto exit;
    ncp = (unsigned char *)PyBytes_AsString(rv);

    for (i = 0; i < view.len; i += size) {
        int val = GETRAWSAMPLE(size, view.buf, i);
        SETRAWSAMPLE(size, ncp, view.len - i - size, val);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_byteswap(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *ncp;
    Py_ssize_t i;
    int size;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*i:swapbytes",
                          &view, &size))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;

    rv = PyBytes_FromStringAndSize(NULL, view.len);
    if (rv == NULL)
        goto exit;
    ncp = (unsigned char *)PyBytes_AsString(rv);

    for (i = 0; i < view.len; i += size) {
        int j;
        for (j = 0; j < size; j++)
            ncp[i + size - 1 - j] = ((unsigned char *)view.buf)[i + j];
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_lin2lin(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *ncp;
    Py_ssize_t i, j;
    int size, size2;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*ii:lin2lin",
                          &view, &size, &size2))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;
    if (!audioop_check_size(size2))
        goto exit;

    if (view.len/size > PY_SSIZE_T_MAX/size2) {
        PyErr_SetString(PyExc_MemoryError,
                        "not enough memory for output buffer");
        goto exit;
    }
    rv = PyBytes_FromStringAndSize(NULL, (view.len/size)*size2);
    if (rv == NULL)
        goto exit;
    ncp = (unsigned char *)PyBytes_AsString(rv);

    for (i = j = 0; i < view.len; i += size, j += size2) {
        int val = GETSAMPLE32(size, view.buf, i);
        SETSAMPLE32(size2, ncp, j, val);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static int
gcd(int a, int b)
{
    while (b > 0) {
        int tmp = a % b;
        a = b;
        b = tmp;
    }
    return a;
}

static PyObject *
audioop_ratecv(PyObject *self, PyObject *args)
{
    Py_buffer view;
    char *cp, *ncp;
    Py_ssize_t len;
    int size, nchannels, inrate, outrate, weightA, weightB;
    int chan, d, *prev_i, *cur_i, cur_o;
    PyObject *state, *samps, *str, *rv = NULL;
    int bytes_per_frame;

    weightA = 1;
    weightB = 0;
    if (!PyArg_ParseTuple(args, "y*iiiiO|ii:ratecv", &view, &size,
                          &nchannels, &inrate, &outrate, &state,
                          &weightA, &weightB))
        return NULL;
    if (!audioop_check_size(size))
        goto exit2;
    if (nchannels < 1) {
        PyErr_SetString(AudioopError, "# of channels should be >= 1");
        goto exit2;
    }
    if (size > INT_MAX / nchannels) {
        /* This overflow test is rigorously correct because
           both multiplicands are >= 1.  Use the argument names
           from the docs for the error msg. */
        PyErr_SetString(PyExc_OverflowError,
                        "width * nchannels too big for a C int");
        goto exit2;
    }
    bytes_per_frame = size * nchannels;
    if (weightA < 1 || weightB < 0) {
        PyErr_SetString(AudioopError,
            "weightA should be >= 1, weightB should be >= 0");
        goto exit2;
    }
    if (view.len % bytes_per_frame != 0) {
        PyErr_SetString(AudioopError, "not a whole number of frames");
        goto exit2;
    }
    if (inrate <= 0 || outrate <= 0) {
        PyErr_SetString(AudioopError, "sampling rate not > 0");
        goto exit2;
    }
    /* divide inrate and outrate by their greatest common divisor */
    d = gcd(inrate, outrate);
    inrate /= d;
    outrate /= d;
    /* divide weightA and weightB by their greatest common divisor */
    d = gcd(weightA, weightB);
    weightA /= d;
    weightA /= d;

    if ((size_t)nchannels > PY_SIZE_MAX/sizeof(int)) {
        PyErr_SetString(PyExc_MemoryError,
                        "not enough memory for output buffer");
        goto exit2;
    }
    prev_i = (int *) PyMem_Malloc(nchannels * sizeof(int));
    cur_i = (int *) PyMem_Malloc(nchannels * sizeof(int));
    if (prev_i == NULL || cur_i == NULL) {
        (void) PyErr_NoMemory();
        goto exit;
    }

    len = view.len / bytes_per_frame; /* # of frames */

    if (state == Py_None) {
        d = -outrate;
        for (chan = 0; chan < nchannels; chan++)
            prev_i[chan] = cur_i[chan] = 0;
    }
    else {
        if (!PyArg_ParseTuple(state,
                        "iO!;audioop.ratecv: illegal state argument",
                        &d, &PyTuple_Type, &samps))
            goto exit;
        if (PyTuple_Size(samps) != nchannels) {
            PyErr_SetString(AudioopError,
                            "illegal state argument");
            goto exit;
        }
        for (chan = 0; chan < nchannels; chan++) {
            if (!PyArg_ParseTuple(PyTuple_GetItem(samps, chan),
                                  "ii:ratecv", &prev_i[chan],
                                               &cur_i[chan]))
                goto exit;
        }
    }

    /* str <- Space for the output buffer. */
    if (len == 0)
        str = PyBytes_FromStringAndSize(NULL, 0);
    else {
        /* There are len input frames, so we need (mathematically)
           ceiling(len*outrate/inrate) output frames, and each frame
           requires bytes_per_frame bytes.  Computing this
           without spurious overflow is the challenge; we can
           settle for a reasonable upper bound, though, in this
           case ceiling(len/inrate) * outrate. */

        /* compute ceiling(len/inrate) without overflow */
        Py_ssize_t q = len > 0 ? 1 + (len - 1) / inrate : 0;
        if (outrate > PY_SSIZE_T_MAX / q / bytes_per_frame)
            str = NULL;
        else
            str = PyBytes_FromStringAndSize(NULL,
                                            q * outrate * bytes_per_frame);
    }
    if (str == NULL) {
        PyErr_SetString(PyExc_MemoryError,
            "not enough memory for output buffer");
        goto exit;
    }
    ncp = PyBytes_AsString(str);
    cp = view.buf;

    for (;;) {
        while (d < 0) {
            if (len == 0) {
                samps = PyTuple_New(nchannels);
                if (samps == NULL)
                    goto exit;
                for (chan = 0; chan < nchannels; chan++)
                    PyTuple_SetItem(samps, chan,
                        Py_BuildValue("(ii)",
                                      prev_i[chan],
                                      cur_i[chan]));
                if (PyErr_Occurred())
                    goto exit;
                /* We have checked before that the length
                 * of the string fits into int. */
                len = (Py_ssize_t)(ncp - PyBytes_AsString(str));
                rv = PyBytes_FromStringAndSize
                    (PyBytes_AsString(str), len);
                Py_DECREF(str);
                str = rv;
                if (str == NULL)
                    goto exit;
                rv = Py_BuildValue("(O(iO))", str, d, samps);
                Py_DECREF(samps);
                Py_DECREF(str);
                goto exit; /* return rv */
            }
            for (chan = 0; chan < nchannels; chan++) {
                prev_i[chan] = cur_i[chan];
                cur_i[chan] = GETSAMPLE32(size, cp, 0);
                cp += size;
                /* implements a simple digital filter */
                cur_i[chan] = (int)(
                    ((double)weightA * (double)cur_i[chan] +
                     (double)weightB * (double)prev_i[chan]) /
                    ((double)weightA + (double)weightB));
            }
            len--;
            d += outrate;
        }
        while (d >= 0) {
            for (chan = 0; chan < nchannels; chan++) {
                cur_o = (int)(((double)prev_i[chan] * (double)d +
                         (double)cur_i[chan] * (double)(outrate - d)) /
                    (double)outrate);
                SETSAMPLE32(size, ncp, 0, cur_o);
                ncp += size;
            }
            d -= inrate;
        }
    }
  exit:
    PyMem_Free(prev_i);
    PyMem_Free(cur_i);
  exit2:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_lin2ulaw(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *ncp;
    Py_ssize_t i;
    int size;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*i:lin2ulaw",
                          &view, &size))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;

    rv = PyBytes_FromStringAndSize(NULL, view.len/size);
    if (rv == NULL)
        goto exit;
    ncp = (unsigned char *)PyBytes_AsString(rv);

    for (i = 0; i < view.len; i += size) {
        int val = GETSAMPLE32(size, view.buf, i);
        *ncp++ = st_14linear2ulaw(val >> 18);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_ulaw2lin(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *cp;
    signed char *ncp;
    Py_ssize_t i;
    int size;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*i:ulaw2lin",
                          &view, &size))
        return NULL;

    if (!audioop_check_size(size))
        goto exit;

    if (view.len > PY_SSIZE_T_MAX/size) {
        PyErr_SetString(PyExc_MemoryError,
                        "not enough memory for output buffer");
        goto exit;
    }
    rv = PyBytes_FromStringAndSize(NULL, view.len*size);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);

    cp = view.buf;
    for (i = 0; i < view.len*size; i += size) {
        int val = st_ulaw2linear16(*cp++) << 16;
        SETSAMPLE32(size, ncp, i, val);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_lin2alaw(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *ncp;
    Py_ssize_t i;
    int size;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*i:lin2alaw",
                          &view, &size))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;

    rv = PyBytes_FromStringAndSize(NULL, view.len/size);
    if (rv == NULL)
        goto exit;
    ncp = (unsigned char *)PyBytes_AsString(rv);

    for (i = 0; i < view.len; i += size) {
        int val = GETSAMPLE32(size, view.buf, i);
        *ncp++ = st_linear2alaw(val >> 19);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_alaw2lin(PyObject *self, PyObject *args)
{
    Py_buffer view;
    unsigned char *cp;
    signed char *ncp;
    Py_ssize_t i;
    int size, val;
    PyObject *rv = NULL;

    if (!PyArg_ParseTuple(args, "y*i:alaw2lin",
                          &view, &size))
        return NULL;

    if (!audioop_check_size(size))
        goto exit;

    if (view.len > PY_SSIZE_T_MAX/size) {
        PyErr_SetString(PyExc_MemoryError,
                        "not enough memory for output buffer");
        goto exit;
    }
    rv = PyBytes_FromStringAndSize(NULL, view.len*size);
    if (rv == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(rv);
    cp = view.buf;

    for (i = 0; i < view.len*size; i += size) {
        val = st_alaw2linear16(*cp++) << 16;
        SETSAMPLE32(size, ncp, i, val);
    }
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_lin2adpcm(PyObject *self, PyObject *args)
{
    Py_buffer view;
    signed char *ncp;
    Py_ssize_t i;
    int size, step, valpred, delta,
        index, sign, vpdiff, diff;
    PyObject *rv = NULL, *state, *str;
    int outputbuffer = 0, bufferstep;

    if (!PyArg_ParseTuple(args, "y*iO:lin2adpcm",
                          &view, &size, &state))
        return NULL;

    if (!audioop_check_parameters(view.len, size))
        goto exit;

    str = PyBytes_FromStringAndSize(NULL, view.len/(size*2));
    if (str == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(str);

    /* Decode state, should have (value, step) */
    if ( state == Py_None ) {
        /* First time, it seems. Set defaults */
        valpred = 0;
        index = 0;
    } else if (!PyArg_ParseTuple(state, "ii", &valpred, &index))
        goto exit;

    step = stepsizeTable[index];
    bufferstep = 1;

    for (i = 0; i < view.len; i += size) {
        int val = GETSAMPLE32(size, view.buf, i) >> 16;

        /* Step 1 - compute difference with previous value */
        if (val < valpred) {
            diff = valpred - val;
            sign = 8;
        }
        else {
            diff = val - valpred;
            sign = 0;
        }

        /* Step 2 - Divide and clamp */
        /* Note:
        ** This code *approximately* computes:
        **    delta = diff*4/step;
        **    vpdiff = (delta+0.5)*step/4;
        ** but in shift step bits are dropped. The net result of this
        ** is that even if you have fast mul/div hardware you cannot
        ** put it to good use since the fixup would be too expensive.
        */
        delta = 0;
        vpdiff = (step >> 3);

        if ( diff >= step ) {
            delta = 4;
            diff -= step;
            vpdiff += step;
        }
        step >>= 1;
        if ( diff >= step  ) {
            delta |= 2;
            diff -= step;
            vpdiff += step;
        }
        step >>= 1;
        if ( diff >= step ) {
            delta |= 1;
            vpdiff += step;
        }

        /* Step 3 - Update previous value */
        if ( sign )
            valpred -= vpdiff;
        else
            valpred += vpdiff;

        /* Step 4 - Clamp previous value to 16 bits */
        if ( valpred > 32767 )
            valpred = 32767;
        else if ( valpred < -32768 )
            valpred = -32768;

        /* Step 5 - Assemble value, update index and step values */
        delta |= sign;

        index += indexTable[delta];
        if ( index < 0 ) index = 0;
        if ( index > 88 ) index = 88;
        step = stepsizeTable[index];

        /* Step 6 - Output value */
        if ( bufferstep ) {
            outputbuffer = (delta << 4) & 0xf0;
        } else {
            *ncp++ = (delta & 0x0f) | outputbuffer;
        }
        bufferstep = !bufferstep;
    }
    rv = Py_BuildValue("(O(ii))", str, valpred, index);
    Py_DECREF(str);
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyObject *
audioop_adpcm2lin(PyObject *self, PyObject *args)
{
    Py_buffer view;
    signed char *cp;
    signed char *ncp;
    Py_ssize_t i, outlen;
    int size, valpred, step, delta, index, sign, vpdiff;
    PyObject *rv = NULL, *str, *state;
    int inputbuffer = 0, bufferstep;

    if (!PyArg_ParseTuple(args, "y*iO:adpcm2lin",
                          &view, &size, &state))
        return NULL;

    if (!audioop_check_size(size))
        goto exit;

    /* Decode state, should have (value, step) */
    if ( state == Py_None ) {
        /* First time, it seems. Set defaults */
        valpred = 0;
        index = 0;
    } else if (!PyArg_ParseTuple(state, "ii", &valpred, &index))
        goto exit;

    if (view.len > (PY_SSIZE_T_MAX/2)/size) {
        PyErr_SetString(PyExc_MemoryError,
                        "not enough memory for output buffer");
        goto exit;
    }
    outlen = view.len*size*2;
    str = PyBytes_FromStringAndSize(NULL, outlen);
    if (str == NULL)
        goto exit;
    ncp = (signed char *)PyBytes_AsString(str);
    cp = view.buf;

    step = stepsizeTable[index];
    bufferstep = 0;

    for (i = 0; i < outlen; i += size) {
        /* Step 1 - get the delta value and compute next index */
        if ( bufferstep ) {
            delta = inputbuffer & 0xf;
        } else {
            inputbuffer = *cp++;
            delta = (inputbuffer >> 4) & 0xf;
        }

        bufferstep = !bufferstep;

        /* Step 2 - Find new index value (for later) */
        index += indexTable[delta];
        if ( index < 0 ) index = 0;
        if ( index > 88 ) index = 88;

        /* Step 3 - Separate sign and magnitude */
        sign = delta & 8;
        delta = delta & 7;

        /* Step 4 - Compute difference and new predicted value */
        /*
        ** Computes 'vpdiff = (delta+0.5)*step/4', but see comment
        ** in adpcm_coder.
        */
        vpdiff = step >> 3;
        if ( delta & 4 ) vpdiff += step;
        if ( delta & 2 ) vpdiff += step>>1;
        if ( delta & 1 ) vpdiff += step>>2;

        if ( sign )
            valpred -= vpdiff;
        else
            valpred += vpdiff;

        /* Step 5 - clamp output value */
        if ( valpred > 32767 )
            valpred = 32767;
        else if ( valpred < -32768 )
            valpred = -32768;

        /* Step 6 - Update step value */
        step = stepsizeTable[index];

        /* Step 6 - Output value */
        SETSAMPLE32(size, ncp, i, valpred << 16);
    }

    rv = Py_BuildValue("(O(ii))", str, valpred, index);
    Py_DECREF(str);
  exit:
    PyBuffer_Release(&view);
    return rv;
}

static PyMethodDef audioop_methods[] = {
    { "max", audioop_max, METH_VARARGS },
    { "minmax", audioop_minmax, METH_VARARGS },
    { "avg", audioop_avg, METH_VARARGS },
    { "maxpp", audioop_maxpp, METH_VARARGS },
    { "avgpp", audioop_avgpp, METH_VARARGS },
    { "rms", audioop_rms, METH_VARARGS },
    { "findfit", audioop_findfit, METH_VARARGS },
    { "findmax", audioop_findmax, METH_VARARGS },
    { "findfactor", audioop_findfactor, METH_VARARGS },
    { "cross", audioop_cross, METH_VARARGS },
    { "mul", audioop_mul, METH_VARARGS },
    { "add", audioop_add, METH_VARARGS },
    { "bias", audioop_bias, METH_VARARGS },
    { "ulaw2lin", audioop_ulaw2lin, METH_VARARGS },
    { "lin2ulaw", audioop_lin2ulaw, METH_VARARGS },
    { "alaw2lin", audioop_alaw2lin, METH_VARARGS },
    { "lin2alaw", audioop_lin2alaw, METH_VARARGS },
    { "lin2lin", audioop_lin2lin, METH_VARARGS },
    { "adpcm2lin", audioop_adpcm2lin, METH_VARARGS },
    { "lin2adpcm", audioop_lin2adpcm, METH_VARARGS },
    { "tomono", audioop_tomono, METH_VARARGS },
    { "tostereo", audioop_tostereo, METH_VARARGS },
    { "getsample", audioop_getsample, METH_VARARGS },
    { "reverse", audioop_reverse, METH_VARARGS },
    { "byteswap", audioop_byteswap, METH_VARARGS },
    { "ratecv", audioop_ratecv, METH_VARARGS },
    { 0,          0 }
};


static struct PyModuleDef audioopmodule = {
    PyModuleDef_HEAD_INIT,
    "audioop",
    NULL,
    -1,
    audioop_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_audioop(void)
{
    PyObject *m, *d;
    m = PyModule_Create(&audioopmodule);
    if (m == NULL)
        return NULL;
    d = PyModule_GetDict(m);
    if (d == NULL)
        return NULL;
    AudioopError = PyErr_NewException("audioop.error", NULL, NULL);
    if (AudioopError != NULL)
         PyDict_SetItemString(d,"error",AudioopError);
    return m;
}
