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
    char * outputpath;
} hlsrec_global_flags;

/*
 * foreward declaration
 */
void hlsrec_loop(snd_pcm_t *capture_handle, short buf[], hlsrec_global_flags* gfp);
int hlsrec_configure_hw(snd_pcm_t * capture_handle, hlsrec_global_flags * gfp);
int hlsrec_prepare_input_device(snd_pcm_t **capture_handle, const char * device, hlsrec_global_flags * gfp);
int hlsrec_write_m3u8(int i, hlsrec_global_flags * gfp,const char * timestr, const char * audiofiletpl, const char * ipaddress);
void hlsrec_usage();
hlsrec_global_flags * hlsrec_init(void);
void hlsrec_post_init(hlsrec_global_flags * gfp);
void hlsrec_free(hlsrec_global_flags  *gf);

int app_cli(hlsrec_global_flags  *gfp);

/**
 * Main entry point
 */
int main (int argc, char *argv[])
{
    int i, res, nencoded, nwrite, hflag = 0, vflag = 0, c;
    snd_pcm_t *capture_handle;
    FILE *fpOut;
    hlsrec_global_flags * hlsrec_gfp;
    lame_global_flags * lame_gfp;
    char * seconds = NULL;

    /* initialize the global flags and variables. Don't forget to run the hlsrec_post_init()
     * after changes of the default settings
     */
    hlsrec_gfp = hlsrec_init();

    /* Parse the cli arguments and initialize the global flags if available */
    opterr = 0;
    while ((c = getopt (argc, argv, "hvl:i:s:o:")) != -1)
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
            hlsrec_gfp->level = atoi(optarg);

            if (hlsrec_gfp->level < 0) {
                fprintf(stderr, "invalid parameter value for level\n");
                exit(0);
            }
            break;
        case 's':
            hlsrec_gfp->seconds = atof(optarg);

            seconds = optarg;

            if (hlsrec_gfp->seconds < 0.5) {
                fprintf(stderr, "invalid parameter value for seconds\n");
                exit(0);
            }

            hlsrec_gfp->num_samples_per_file      = hlsrec_gfp->seconds * hlsrec_gfp->sample_rate;
            hlsrec_gfp->num_mp3_buffer_size       = 1.25 * hlsrec_gfp->num_samples_per_file + 7200;

            break;
        case 'i':
            hlsrec_gfp->intensity = atoi(optarg);

            if(hlsrec_gfp->intensity < 0){
                fprintf(stderr, "invalid parameter for intensity\n");
                exit(0);
            }
            break;
        case 'o':
            hlsrec_gfp->outputpath = optarg;

            if(strcmp(hlsrec_gfp->outputpath, "") != 0){
                fprintf(stderr, "invalid parameter for output path\n");
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

    /* allocate/initialize the buffer. It is important to run this method after cli */
    hlsrec_post_init(hlsrec_gfp);

    fprintf(stderr, "pcm buffer: %ld\n", hlsrec_gfp->num_samples_per_file);
    fprintf(stderr, "mp3 buffer: %ld\n", hlsrec_gfp->num_mp3_buffer_size);

    fprintf(stderr, "start...\n");

    if( (res = hlsrec_prepare_input_device(&capture_handle, &hlsrec_gfp->device[0], hlsrec_gfp) ) < 0) {
        hlsrec_free(hlsrec_gfp);
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
    lame_set_num_samples(lame_gfp,       hlsrec_gfp->num_samples_per_file);
    lame_set_num_channels(lame_gfp,      hlsrec_gfp->num_channels);
    lame_set_in_samplerate(lame_gfp,     hlsrec_gfp->sample_rate);
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
        sprintf(filename, "%s/%s-%d.mp3", &hlsrec_gfp->outputpath[0], "audio", i);

        /* currently we ignore the previous value of the mask */
        //        umask(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        umask(022);

        /* open the output file */
        fpOut = fopen(filename, "wb"); /* open the output file*/

        /* record data from input device and write to pcm_buf */
        hlsrec_loop(capture_handle, hlsrec_gfp->pcm_buf, hlsrec_gfp);

        fprintf(stderr, "recorded\n");

        /* encode data from pcm_buf and write to mp3_buffer */
        nencoded = lame_encode_buffer(
                    lame_gfp,
                    hlsrec_gfp->pcm_buf,
                    hlsrec_gfp->pcm_buf,
                    hlsrec_gfp->num_samples_per_file,
                    hlsrec_gfp->mp3buffer,
                    hlsrec_gfp->num_mp3_buffer_size);
        if(nencoded < 0) {
            fprintf(stderr, "error encoding (%d)\n", nencoded);
        }

        fprintf(stderr, "encoded...\n");

        /* write the encoded data to the output file */
        nwrite = fwrite(hlsrec_gfp->mp3buffer, sizeof(unsigned char), nencoded, fpOut);
        if(nencoded != nwrite){
            fprintf(stderr, "error write (%d) should be %d\n", nwrite, nencoded);
        }
        fclose(fpOut);
        
        hlsrec_write_m3u8(i, hlsrec_gfp, seconds, "audio", "192.168.1.123");
    }

    /*
     * free the internal data structures from lame
     * close the output file and close the alsa input device.
     */
    lame_close(lame_gfp);
    snd_pcm_close (capture_handle);
    
    fprintf(stderr, "successfully closed\n");

    hlsrec_free(hlsrec_gfp);
    
    exit (0);
}

/**
 * @brief Allocate the global flags structure and initialize it with default values.
 *
 * @return Pointer to the allocated hlsrec_global_flags structure on success. Otherwise NULL.
 *
 * @warning Run this method first before any other function.
 */
hlsrec_global_flags * hlsrec_init(void)
{
    hlsrec_global_flags * gfp;

    gfp = calloc(1, sizeof(hlsrec_global_flags));
    if(gfp == NULL) {
        return NULL;
    }

    /* Set the global flags to default value */
    strcpy(&gfp->device[0], "hw:0,0");
    gfp->sample_rate               = 44100;    /* currently a constant */
    gfp->num_channels              = 1;        /* currently a constant */
    gfp->level                     = 10000;    /* volume level to use for the detection */
    gfp->intensity                 = 500;      /* number of detection in one interval if the event occours */
    gfp->seconds                   = 5;
    gfp->num_samples_per_file      = gfp->seconds * gfp->sample_rate;
    gfp->num_mp3_buffer_size       = 1.25 * gfp->num_samples_per_file + 7200; /* Calculation comes from the API file of the lame library */

    /* run the hlsrec_post_init() function to allocate the memory with the active settings */
    gfp->pcm_buf = NULL;
    gfp->mp3buffer = NULL;

    return gfp;
}

/**
 * @brief Initialize the buffers because the size of the buffer could be changed by the user
 *
 * @param gfp Pointer to the global flags structure.
 *
 * \warning Run the method hlsrec_init() before you run this function.
 */
void hlsrec_post_init(hlsrec_global_flags * gfp)
{
    gfp->pcm_buf = (short int*)calloc(1, sizeof(short int) * gfp->num_samples_per_file);
    gfp->mp3buffer = (unsigned char*)calloc(1, sizeof(unsigned char) * gfp->num_mp3_buffer_size);
}

/**
 * @brief Free the allocated memory
 *
 * @param gfp Pointer to the global flags structure.
 */
void hlsrec_free(hlsrec_global_flags  *gfp)
{
    free(gfp->pcm_buf);
    free(gfp->mp3buffer);

    free(gfp);
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
int app_cli(hlsrec_global_flags  *gf)
{
    return 0;
}

/**
 * @brief Update the m3u8 file with the given index
 * @param i Index of the next audio file
 *
 * @return 0 on success. Otherwise a negative error code
 */
int hlsrec_write_m3u8(int i, hlsrec_global_flags * gfp,const char * timestr, const char * audiofiletpl, const char * ipaddress)
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
        sprintf((char*)&str[0], "#EXTINF:%s,\nhttp://%s/%s-%d.mp3\n", ipaddress, timestr, audiofiletpl, b);
        strcat(buf, str);
    }
    
    if (i > 0) {
        b = i - 1;
        sprintf((char*)&str[0], "#EXTINF:%s,\nhttp://%s/%s-%d.mp3\n", ipaddress, timestr, audiofiletpl, b);
        strcat(buf, str);
    }

    sprintf((char*)&str[0], "#EXTINF:%s,\nhttp://%s/%s-%d.mp3\n", ipaddress, timestr, audiofiletpl, b);
    strcat(buf, str);

    //    strcat(buf, tail);
    
    char filepath_m3u8[100]; /** @todo change 100 to max file path size */
    memset(filepath_m3u8, 0, 100);
    strcat(filepath_m3u8, gfp->outputpath);
    strcat(filepath_m3u8, "/index.m3u8");
    
    if( (fp = fopen(&filepath_m3u8[0], "wb")) == NULL){
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
 * \brief Prepare the input device
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
