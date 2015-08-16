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

#include <sys/types.h>
#include <sys/stat.h>


#include <alsa/asoundlib.h>
#include <lame/lame.h>


#define PROJECT_VERSION_MAJOR 1
#define PROJECT_VERSION_MINOR 0
#define PROJECT_VERSION_PATCH 0

/**
 * Global flags
 */
typedef struct {
    char device[100];
    int byte_per_sample;        /** Number of bytes for one sample */
    unsigned int sample_rate;   /** Sample rate to record */
    int num_channels;           /** Number of channels to record */
    short level;                /** Level to detect if baby is crying */
    short intensity;            /** Number of detected samples to switch the state crying */
    float seconds;              /** Number of seconds per audio file */
    long num_samples_per_file;   /** Number of samples per file */
    short int * pcm_buf;
    long num_mp3_buffer_size;
    unsigned char * mp3buffer;
} hlsrec_global_flags;

/*
 * foreward declaration
 */
void hlsrec_loop(snd_pcm_t *capture_handle, short buf[], hlsrec_global_flags* gfp);
int hlsrec_configure_hw(snd_pcm_t * capture_handle, hlsrec_global_flags * gfp);
int hlsrec_prepare_input_device(snd_pcm_t **capture_handle, const char * device, hlsrec_global_flags * gfp);
int hlsrec_write_m3u8(int i, hlsrec_global_flags *gfp, char * tmp);
void hlsrec_usage();
int hlsrec_cli(hlsrec_global_flags  *gfp);
void hlsrec_free(hlsrec_global_flags  *gf);

/**
 * Main entry point
 */
int main (int argc, char *argv[])
{
    int i, res, nencoded, nwrite, hflag = 0, vflag = 0, c;
    snd_pcm_t *capture_handle;
    FILE *fpOut;
    hlsrec_global_flags hlsrec_gf;
    lame_global_flags * lame_gfp;
    char * seconds = NULL;

    /* Set the global flags to default value */
    memset(&hlsrec_gf.device[0], 0, 100);
    strcpy(&hlsrec_gf.device[0], "hw:0,0");
    hlsrec_gf.sample_rate               = 44100;    /* currently a constant */
    hlsrec_gf.num_channels              = 1;        /* currently a constant */
    hlsrec_gf.level                     = 10000;    /* volume level to use for the detection */
    hlsrec_gf.intensity                 = 500;      /* number of detection in one interval if the event occours */
    hlsrec_gf.seconds                   = 5;
    hlsrec_gf.num_samples_per_file      = hlsrec_gf.seconds * hlsrec_gf.sample_rate;
    hlsrec_gf.num_mp3_buffer_size       = 1.25 * hlsrec_gf.num_samples_per_file + 7200;
    hlsrec_gf.pcm_buf = NULL;
    hlsrec_gf.mp3buffer = NULL;
    
    /* Parse the cli arguments and initialize the global flags if available */
    opterr = 0;
    while ((c = getopt (argc, argv, "hvl:i:s:")) != -1)
    {
        switch (c)
        {
        case 'h':
            hflag = 1;
            break;
        case 'v':
            vflag = 1;
            break;
        case 'l':
            hlsrec_gf.level = atoi(optarg);

            if (hlsrec_gf.level < 0) {
                fprintf(stderr, "invalid parameter value for level\n");
                exit(0);
            }
            break;
        case 's':
            hlsrec_gf.seconds = atof(optarg);

            seconds = optarg;

            if (hlsrec_gf.seconds < 0.5) {
                fprintf(stderr, "invalid parameter value for seconds\n");
                exit(0);
            }

            hlsrec_gf.num_samples_per_file      = hlsrec_gf.seconds * hlsrec_gf.sample_rate;
            hlsrec_gf.num_mp3_buffer_size       = 1.25 * hlsrec_gf.num_samples_per_file + 7200;

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
    
    /* print the usage information if the flag is set */
    if (hflag ) {
        hlsrec_usage();
        exit(0);
    }
    
    /* print the version information if flag is set */
    if (vflag) {
        fprintf(stderr, "%d.%d.%d\n", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH);
        exit(0);
    }

    /* Allocate the buffer because the size is changed */
    hlsrec_gf.pcm_buf = (short int*)malloc(sizeof(short int) * hlsrec_gf.num_samples_per_file);
    memset(hlsrec_gf.pcm_buf, 0, hlsrec_gf.num_samples_per_file);

    hlsrec_gf.mp3buffer = (unsigned char*)malloc(sizeof(unsigned char) * hlsrec_gf.num_mp3_buffer_size);
    memset(hlsrec_gf.mp3buffer, 0, hlsrec_gf.num_mp3_buffer_size);

    fprintf(stderr, "pcm buffer: %ld\n", hlsrec_gf.num_samples_per_file);
    fprintf(stderr, "mp3 buffer: %ld\n", hlsrec_gf.num_mp3_buffer_size);

    fprintf(stderr, "start...\n");

    if( (res = hlsrec_prepare_input_device(&capture_handle, &hlsrec_gf.device[0], &hlsrec_gf) ) < 0) {
        hlsrec_free(&hlsrec_gf);
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
    lame_set_num_samples(lame_gfp,       hlsrec_gf.num_samples_per_file);
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
    for (i = 0; i < 200; i++) {
        memset(filename, 0, 100);
        sprintf(filename, "/mnt/ramdisk/www/test%d.mp3", i);
//        sprintf(filename, "/var/www/test%d.mp3", i);

        /* currently we ignore the previous value of the mask */
//        umask(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        umask(022);

        /* open the output file */
        fpOut = fopen(filename, "wb"); /* open the output file*/

        /* record data from input device and write to pcm_buf */
        hlsrec_loop(capture_handle, hlsrec_gf.pcm_buf, &hlsrec_gf);

        fprintf(stderr, "recorded\n");

        /* encode data from pcm_buf and write to mp3_buffer */
        nencoded = lame_encode_buffer(
                    lame_gfp,
                    hlsrec_gf.pcm_buf,
                    hlsrec_gf.pcm_buf,
                    hlsrec_gf.num_samples_per_file,
                    hlsrec_gf.mp3buffer,
                    hlsrec_gf.num_mp3_buffer_size);
        if(nencoded < 0) {
            fprintf(stderr, "error encoding (%d)\n", nencoded);
        }

        fprintf(stderr, "encoded...\n");

        /* write the encoded data to the output file */
        nwrite = fwrite(hlsrec_gf.mp3buffer, sizeof(unsigned char), nencoded, fpOut);
        if(nencoded != nwrite){
            fprintf(stderr, "error write (%d) should be %d\n", nwrite, nencoded);
        }
        fclose(fpOut);
        
        hlsrec_write_m3u8(i, &hlsrec_gf, seconds);
    }

    /*
     * free the internal data structures from lame
     * close the output file and close the alsa input device.
     */
    lame_close(lame_gfp);
    snd_pcm_close (capture_handle);
    
    fprintf(stderr, "successfuly closed\n");

    hlsrec_free(&hlsrec_gf);
    
    exit (0);
}

void hlsrec_free(hlsrec_global_flags  *gf)
{
    free(gf->pcm_buf);
    free(gf->mp3buffer);
}

/**
 * @brief Print the help/usage information
 */
void hlsrec_usage()
{
    fprintf(stderr, "hlsrec %d.%d.%d\n\n", PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH);
    fprintf(stderr, "-h print this help/usage information\n");
    fprintf(stderr, "-v display the version of the application\n");
    fprintf(stderr, "-l level 1...65536\n");
    fprintf(stderr, "-i intensity 1...65536\n");
    fprintf(stderr, "-s seconds to record per file\n");
}

/**
 * @todo Currently not implemented
 *
 * @brief Parse the CLI arguments and set the global variables and/or flags if available
 *
 * @param gf Reference to the structure with all flags and variables
 * @return 0 on success. Otherwise a negative error code.
 */
int hlsrec_cli(hlsrec_global_flags  *gf)
{
    return 0;
}

/**
 * @brief Update the m3u8 file with the given index
 * @param i Index of the next audio file
 *
 * @return 0 on success. Otherwise a negative error code
 */
int hlsrec_write_m3u8(int i, hlsrec_global_flags * gfp, char * tmp)
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
        sprintf((char*)&str[0], "#EXTINF:%s,\nhttp://192.168.1.123/test%d.mp3\n", tmp, b);
        strcat(buf, str);
    }
    
    if (i > 0) {
        b = i - 1;
        sprintf((char*)&str[0], "#EXTINF:%s,\nhttp://192.168.1.123/test%d.mp3\n", tmp, b);
        strcat(buf, str);
    }

    sprintf((char*)&str[0], "#EXTINF:%s,\nhttp://192.168.1.123/test%d.mp3\n", tmp, i);
    strcat(buf, str);

    //    strcat(buf, tail);
    
    
    if( (fp = fopen("/mnt/ramdisk/www/index.m3u8", "wb")) == NULL){
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

    fprintf(stderr, "try to open device %s\n", device);
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
 * \param gfp Pointer to the global structure of flags
 */
void hlsrec_loop(snd_pcm_t *capture_handle, short buf[], hlsrec_global_flags * gfp)
{
    int i, err, levelcnt;

    if ((err = snd_pcm_readi (capture_handle, buf, gfp->num_samples_per_file)) != gfp->num_samples_per_file) {
        fprintf (stderr, "read from audio interface failed (%s)\n",
                 snd_strerror (err));
        exit (1);
    }

    /* It is not needed to check all samples so we just check 5th's sample and increment a counter */
    /** @todo maybe you have to start a own thread for the detction */
    levelcnt = 0;
    for (i = 0; i < gfp->num_samples_per_file; i+=5) {
        if (buf[i] > gfp->level) {
            levelcnt++;
        }
    }

    if(levelcnt > gfp->intensity) {
        fprintf(stderr, "baby is crying (%d)\n", levelcnt);
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
