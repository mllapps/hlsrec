/*
 * HTTP Live Streaming Audio Record Application
 *
 * @mainpage
 * usage: hlsrec hw:0,0 1000
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <ctype.h>
#include <unistd.h>


#include <alsa/asoundlib.h>
#include <lame/lame.h>

/**
 * Number of samples read by one iteration
 */
#define HLSREC_SAMPLES_PER_ITERATION        (128)
#define HLSREC_SAMPLE_ITERATIONS            (1722)

#define HLSREC_PCM_BUFFER_SIZE              (HLSREC_SAMPLES_PER_ITERATION * HLSREC_SAMPLE_ITERATIONS)

#define HLSREC_SAMPLES_TO_ENCODE            (HLSREC_PCM_BUFFER_SIZE)
/**
 * Global flags
 */
typedef struct {
    int byte_per_sample;        /** Number of bytes for one sample */
    unsigned int sample_rate;   /** Sample rate to record */
    int num_channels;           /** Number of channels to record */
    short level;                /** level to detect if baby is crying */
    short intensity;            /** number of detected samples to switch the state crying */
} hlsrec_global_flags;

/*
 * foreward declaration
 */
void hlsrec_loop(snd_pcm_t *capture_handle, short buf[HLSREC_PCM_BUFFER_SIZE], hlsrec_global_flags* gfp);
int hlsrec_configure_hw(snd_pcm_t * capture_handle, hlsrec_global_flags * gfp);
int hlsrec_prepare_input_device(snd_pcm_t **capture_handle, const char * device, hlsrec_global_flags * gfp);
int hlsrec_write_m3u8(int i);

/**
 * Main entry point
 */
int main (int argc, char *argv[])
{
    int i, res, nencoded, nwrite, hflag, c;
    short int pcm_buf[HLSREC_PCM_BUFFER_SIZE];
    snd_pcm_t *capture_handle;
    FILE *fpOut;
    hlsrec_global_flags hlsrec_gf;
    lame_global_flags * lame_gfp;
    const int mp3buffer_size = 1.25 * HLSREC_PCM_BUFFER_SIZE + 7200;
    unsigned char mp3_buffer[mp3buffer_size];

    /* Setup the global flags */
    hlsrec_gf.sample_rate               = 44100;
    hlsrec_gf.num_channels              = 1;
    hlsrec_gf.level                     = 10000;
    hlsrec_gf.intensity                 = 500;

    /** @todo write a better check for the arguments */
    if(argc != 3) {
        fprintf(stderr, "invalid number of arguments\n");
        return -1;
    }
    
    opterr = 0;
    while ((c = getopt (argc, argv, "hl:i:")) != -1)
    {
    switch (c)
      {
      case 'h':
        hflag = 1;
        break;
      case 'l':
        hlsrec_gf.level = atoi(optarg);
        
        if (hlsrec_gf.level < 0) {
            fprintf(stderr, "invalid parameter value for level\n");
            exit(0);
        }
        break;
      case 'i':
        hlsrec_gf.intensity = atoi(optarg);
        
        if(hlsrec_gf.intensity < 0){
            fprintf(stderr, "invalid parameter for intensity\n");
            exit(0);
        }
        break;
      case '?':
        if (optopt == 'l' || optopt == 'i') {
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        }
        else if (isprint (optopt)) {
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        }
        else {
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
        }
        return 1;
      default:
        abort ();
      }
    }
    
    if (hflag ) {
        fprintf(stderr, "print usage and menu");
        exit(0);
    }

    fprintf(stderr, "start...\n");
    
    if( (res = hlsrec_prepare_input_device(&capture_handle, argv[1], &hlsrec_gf) ) < 0) {
        exit(res);
    }
    
    /*
     * start here to configure the LAME library and prepare something
     *
     * Initialize the encoder.  sets default for all encoder parameters.
     * #include "lame.h"
     */
    lame_gfp = lame_init();
    /*
     * The default (if you set nothing) is a  J-Stereo, 44.1khz
     * 128kbps CBR mp3 file at quality 5.  Override various default settings
     * as necessary, for example:
     *
     * See lame.h for the complete list of options.  Note that there are
     * some lame_set_*() calls not documented in lame.h.  These functions
     * are experimental and for testing only.  They may be removed in
     * the future.
     */
    lame_set_num_samples(lame_gfp,       HLSREC_PCM_BUFFER_SIZE);
    lame_set_num_channels(lame_gfp,      hlsrec_gf.num_channels);
    lame_set_in_samplerate(lame_gfp,     hlsrec_gf.sample_rate);
    lame_set_brate(lame_gfp,             128);
    /* mode = 0,1,2,3 = stereo, jstereo, dual channel (not supported), mono */
    lame_set_mode(lame_gfp,              3);
    lame_set_quality(lame_gfp,           2);   /* 2=high  5 = medium  7=low */

    /*
     * Set more internal configuration based on data provided above,
     * as well as checking for problems.  Check that ret_code >= 0.
     */
    res = lame_init_params(lame_gfp);
    if(res < 0) {
        fprintf(stderr, "invalid lame params (%d)\n", res);
    }

    fprintf(stderr, "prepared\n");
    
    
    char filename[100];
    for (i = 0; i < 20; i++) {
        
        memset(filename, 0, 100);
        sprintf(filename, "/var/www/test%d.mp3", i);
        /* open the output file */
        fpOut = fopen(filename, "wb"); /* open the output file*/

        /* record data from input device and write to pcm_buf */
        hlsrec_loop(capture_handle, pcm_buf, &hlsrec_gf);

        fprintf(stderr, "recorded\n");

        /* encode data from pcm_buf and write to mp3_buffer */
        nencoded = lame_encode_buffer(
                    lame_gfp,
                    pcm_buf, pcm_buf,
                    HLSREC_SAMPLES_TO_ENCODE,
                    (unsigned char*)&mp3_buffer[0],
                mp3buffer_size);
        if(nencoded < 0) {
            fprintf(stderr, "error encoding (%d)\n", nencoded);
        }

        /* write the encoded data to the output file */
        nwrite = fwrite((void *)&mp3_buffer[0], sizeof(char), nencoded, fpOut);
        if(nencoded != nwrite){
            fprintf(stderr, "error write (%d) should be %d\n", nwrite, nencoded);
        }
        fclose(fpOut);
        
        hlsrec_write_m3u8(i);
    }

    /*
     * free the internal data structures from lame
     * close the output file and close the alsa input device.
     */
    lame_close(lame_gfp);
    snd_pcm_close (capture_handle);
    
    fprintf(stderr, "successfuly closed\n");
    
    exit (0);
}

/**
 * @brief Update the m3u8 file with the given index
 * @param i Index of the next audio file
 *
 * @return 0 on success. Otherwise a negative error code
 */
int hlsrec_write_m3u8(int i)
{
    int nwrite, towrite, b;
    FILE * fp;
    char * buf = malloc(400);
    char str[100];
    static const char * head = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:5\n";
    /*static const char * head2 = "#EXT-X-PLAYLIST-TYPE:EVENT\n"; */
    /*static const char* tail = "#EXT-X-ENDLIST\n"; */

    memset(buf, 0, 400);
    memset(str, 0, 100);
    
    strcat(buf, head);
    
    if(i > 1) {
        b = i - 2;
        sprintf((char*)&str[0], "#EXT-X-MEDIA-SEQUENCE:%d\n", b);
        strcat(buf, str);
    }else if (i > 0) {
        b = i - 1;
        sprintf((char*)&str[0], "#EXT-X-MEDIA-SEQUENCE:%d\n", b);
        strcat(buf, str);
    }else {
        b = 0;
        sprintf((char*)&str[0], "#EXT-X-MEDIA-SEQUENCE:%d\n", b);
        strcat(buf, str);
    }
    
    //    strcat(buf, head2);

    if (i > 1) {
        b = i - 2;
        sprintf((char*)&str[0], "#EXTINF:4.9,\nhttp://192.168.1.146/test%d.mp3\n", b);
        strcat(buf, str);
    }
    
    if (i > 0) {
        b = i - 1;
        sprintf((char*)&str[0], "#EXTINF:4.9,\nhttp://192.168.1.146/test%d.mp3\n", b);
        strcat(buf, str);
    }

    sprintf((char*)&str[0], "#EXTINF:4.9,\nhttp://192.168.1.146/test%d.mp3\n", i);
    strcat(buf, str);

    //    strcat(buf, tail);
    
    
    if( (fp = fopen("/var/www/index.m3u8", "wb")) == NULL){
        fprintf(stderr, "error open index.m3u8\n");
        return -1;
    }
    
    towrite = strlen(buf);
    nwrite = fwrite((void *)&buf[0], 1, towrite, fp);
    if(towrite != nwrite){
        fprintf(stderr, "error write (%d) should be %d\n", nwrite, towrite);
    }
    fclose(fp);
    
    free(buf);

    return 0; /* success */
}

/** 
 * \brief prepare the input device
 *
 * \param capture_handle
 * \param device
 * \param gfp Pointer to the global flags of the hlsrec
 */
int hlsrec_prepare_input_device(snd_pcm_t **capture_handle, const char * device, hlsrec_global_flags * gfp)
{
    int err;

    if ((err = snd_pcm_open (capture_handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n",
                 device,
                 snd_strerror (err));
        return (-1);
    }
    
    
    fprintf(stderr, "opened\n");

    if( (err = hlsrec_configure_hw(*capture_handle, gfp) ) < 0) {
        return (-2);
    }

    fprintf(stderr, "configured\n");

    if ((err = snd_pcm_prepare (*capture_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                 snd_strerror (err));
        return (-3);
    }

    return 0; /* success */
}

/**
 * \brief Loop the capture process
 *
 * \param capture_handle ALSA device handle to capture the data
 * \param buf Buffer to write the captured data
 */
void hlsrec_loop(snd_pcm_t *capture_handle, short buf[HLSREC_PCM_BUFFER_SIZE], hlsrec_global_flags * gfp)
{
    int i, err;

    /* load the reading process and write the data to the output file */
    /*	for (i = 0; i < HLSREC_SAMPLE_ITERATIONS; ++i) {
        if ((err = snd_pcm_readi (capture_handle, buf, HLSREC_PCM_BUFFER_SIZE)) != HLSREC_PCM_BUFFER_SIZE) {
            fprintf (stderr, "read from audio interface failed (%s)\n",
                 snd_strerror (err));
            exit (1);
        }
    }
*/	
    if ((err = snd_pcm_readi (capture_handle, buf, HLSREC_PCM_BUFFER_SIZE)) != HLSREC_PCM_BUFFER_SIZE) {
        fprintf (stderr, "read from audio interface failed (%s)\n",
                 snd_strerror (err));
        exit (1);
    }

    /** @todo maybe you have to start a own thread for the detction */
    for (i = 0; i < HLSREC_PCM_BUFFER_SIZE; i++) {
        if (buf[i] > gfp->level) {
            fprintf(stderr, "baby is crying (%d)\n", buf[i]);
        }
    }
}

/**
 * \brief Configure the alsa audio hardware
 *
 * \return
 */
int hlsrec_configure_hw(snd_pcm_t * capture_handle, hlsrec_global_flags * gfp)
{
    int err;
    snd_pcm_hw_params_t *hw_params;

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
                 snd_strerror (err));
        return (-1);
    }

    if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
                 snd_strerror (err));
        return (-2);
    }

    if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "cannot set access type (%s)\n",
                 snd_strerror (err));
        return (-3);
    }

    if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
        fprintf (stderr, "cannot set sample format (%s)\n",
                 snd_strerror (err));
        return (-4);
    }
    
    if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &gfp->sample_rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n",
                 snd_strerror (err));
        return (-5);
    }

    if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, gfp->num_channels)) < 0) {
        fprintf (stderr, "cannot set channel count (%s)\n",
                 snd_strerror (err));
        return (-6);
    }

    if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters (%s)\n",
                 snd_strerror (err));
        return (-7);
    }
    
    snd_pcm_hw_params_free (hw_params);

    return 0; /* success */
}
