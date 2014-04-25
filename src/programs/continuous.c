/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 1999-2010 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced
 * Research Projects Agency and the National Science Foundation of the
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*
 * continuous.c - Simple pocketsphinx command-line application to test
 *                both continuous listening/silence filtering from microphone
 *                and continuous file transcription.
 */

/*
 * This is a simple example of pocketsphinx application that uses continuous listening
 * with silence filtering to automatically segment a continuous stream of audio input
 * into utterances that are then decoded.
 *
 * Remarks:
 *   - Each utterance is ended when a silence segment of at least 1 sec is recognized.
 *   - Single-threaded implementation for portability.
 *   - Uses audio library; can be replaced with an equivalent custom library.
 */

#include <stdio.h>
#include <string.h>

#include <signal.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/time.h>

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/cont_ad.h>

#include "pocketsphinx.h"

// Include gearman to send off background jobs.
#include <libgearman/gearman.h>

static const arg_t cont_args_def[] = {
    POCKETSPHINX_OPTIONS,
    /* Argument file. */
    { "-argfile",
      ARG_STRING,
      NULL,
      "Argument file giving extra arguments." },
    { "-adcdev",
      ARG_STRING,
      NULL,
      "Name of audio device to use for input." },
    { "-infile",
      ARG_STRING,
      NULL,
      "Audio file to transcribe." },
    { "-time",
      ARG_BOOLEAN,
      "no",
      "Print word times in file transcription." },
    CMDLN_EMPTY_OPTION
};

static ps_decoder_t *ps;
static cmd_ln_t *config;
// DISABLED //static FILE* rawfd;
// DISABLED //
// DISABLED //static int32
// DISABLED //ad_file_read(ad_rec_t * ad, int16 * buf, int32 max)
// DISABLED //{
// DISABLED //    size_t nread;
// DISABLED //
// DISABLED //    nread = fread(buf, sizeof(int16), max, rawfd);
// DISABLED //
// DISABLED //    return (nread > 0 ? nread : -1);
// DISABLED //}
// DISABLED //
// DISABLED //static void
// DISABLED //print_word_times(int32 start)
// DISABLED //{
// DISABLED //        ps_seg_t *iter = ps_seg_iter(ps, NULL);
// DISABLED //        while (iter != NULL) {
// DISABLED //                int32 sf, ef, pprob;
// DISABLED //                float conf;
// DISABLED //
// DISABLED //                ps_seg_frames (iter, &sf, &ef);
// DISABLED //                pprob = ps_seg_prob (iter, NULL, NULL, NULL);
// DISABLED //                conf = logmath_exp(ps_get_logmath(ps), pprob);
// DISABLED //                printf ("%s %f %f %f\n", ps_seg_word (iter), (sf + start) / 100.0, (ef + start) / 100.0, conf);
// DISABLED //                iter = ps_seg_next (iter);
// DISABLED //        }
// DISABLED //}
// DISABLED //
// DISABLED ///*
// DISABLED // * Continuous recognition from a file
// DISABLED // */
// DISABLED //static void
// DISABLED //recognize_from_file() {
// DISABLED //    cont_ad_t *cont;
// DISABLED //    ad_rec_t file_ad = {0};
// DISABLED //    int16 adbuf[4096];
// DISABLED //    const char* hyp;
// DISABLED //    const char* uttid;
// DISABLED //    int32 k, ts, start;
// DISABLED //
// DISABLED //    char waveheader[44];
// DISABLED //    if ((rawfd = fopen(cmd_ln_str_r(config, "-infile"), "rb")) == NULL) {
// DISABLED //        E_FATAL_SYSTEM("Failed to open file '%s' for reading",
// DISABLED //                        cmd_ln_str_r(config, "-infile"));
// DISABLED //    }
// DISABLED //
// DISABLED //    fread(waveheader, 1, 44, rawfd);
// DISABLED //
// DISABLED //    file_ad.sps = (int32)cmd_ln_float32_r(config, "-samprate");
// DISABLED //    file_ad.bps = sizeof(int16);
// DISABLED //
// DISABLED //    if ((cont = cont_ad_init(&file_ad, ad_file_read)) == NULL) {
// DISABLED //        E_FATAL("Failed to initialize voice activity detection");
// DISABLED //    }
// DISABLED //    if (cont_ad_calib(cont) < 0)
// DISABLED //        E_FATAL("Failed to calibrate voice activity detection\n");
// DISABLED //    rewind (rawfd);
// DISABLED //
// DISABLED //    for (;;) {
// DISABLED //
// DISABLED //        while ((k = cont_ad_read(cont, adbuf, 4096)) == 0);
// DISABLED //
// DISABLED //        if (k < 0) {
// DISABLED //            break;
// DISABLED //        }
// DISABLED //
// DISABLED //        if (ps_start_utt(ps, NULL) < 0)
// DISABLED //            E_FATAL("ps_start_utt() failed\n");
// DISABLED //
// DISABLED //        ps_process_raw(ps, adbuf, k, FALSE, FALSE);
// DISABLED //
// DISABLED //        ts = cont->read_ts;
// DISABLED //        start = ((ts - k) * 100.0) / file_ad.sps;
// DISABLED //
// DISABLED //        for (;;) {
// DISABLED //            if ((k = cont_ad_read(cont, adbuf, 4096)) < 0)
// DISABLED //                break;
// DISABLED //
// DISABLED //            if (k == 0) {
// DISABLED //                /*
// DISABLED //                 * No speech data available; check current timestamp with most recent
// DISABLED //                 * speech to see if more than 1 sec elapsed.  If so, end of utterance.
// DISABLED //                 */
// DISABLED //                if ((cont->read_ts - ts) > DEFAULT_SAMPLES_PER_SEC)
// DISABLED //                    break;
// DISABLED //            }
// DISABLED //            else {
// DISABLED //                /* New speech data received; note current timestamp */
// DISABLED //                ts = cont->read_ts;
// DISABLED //            }
// DISABLED //
// DISABLED //
// DISABLED //            ps_process_raw(ps, adbuf, k, FALSE, FALSE);
// DISABLED //        }
// DISABLED //
// DISABLED //        ps_end_utt(ps);
// DISABLED //
// DISABLED //        if (cmd_ln_boolean_r(config, "-time")) {
// DISABLED //            print_word_times(start);
// DISABLED //        } else {
// DISABLED //            hyp = ps_get_hyp(ps, NULL, &uttid);
// DISABLED //            printf("%s: %s\n", uttid, hyp);
// DISABLED //        }
// DISABLED //        fflush(stdout);
// DISABLED //    }
// DISABLED //
// DISABLED //    cont_ad_close(cont);
// DISABLED //    fclose(rawfd);
// DISABLED //}

/* Sleep for specified msec */
static void
sleep_msec(int32 ms)
{
#if (defined(WIN32) && !defined(GNUWINCE)) || defined(_WIN32_WCE)
    Sleep(ms);
#else
    /* ------------------- Unix ------------------ */
    struct timeval tmo;

    tmo.tv_sec = 0;
    tmo.tv_usec = ms * 1000;

    select(0, NULL, NULL, NULL, &tmo);
#endif
}

/*
 * Main utterance processing loop:
 *     for (;;) {
 *         wait for start of next utterance;
 *         decode utterance until silence of at least 1 sec observed;
 *         print utterance result;
 *     }
 */
static void
recognize_from_microphone( gearman_client_st *gclient, gearman_job_handle_t jh)
{
    // NOTE ABOVE THE ARGUMENTS. THESE WERE ADDED IN MY ATTEMPT TO PASS THE GEARMAN_CLIENT_ST OBJECT INTO THIS FUNCTION
    /////////////////////
    //// Scarlett   ^^^^^^^^^^^^^^^^^^ ABOVE
    ////////////////////

    ad_rec_t *ad;
    int16 adbuf[4096];
    int32 k, ts, rem;
    char const *hyp;
    char const *uttid;
    cont_ad_t *cont;
    char word[256];

    if ((ad = ad_open_dev(cmd_ln_str_r(config, "-adcdev"),
                          (int)cmd_ln_float32_r(config, "-samprate"))) == NULL)
        E_FATAL("Failed to open audio device\n");

    /* Initialize continuous listening module */
    if ((cont = cont_ad_init(ad, ad_read)) == NULL)
        E_FATAL("Failed to initialize voice activity detection\n");
    if (ad_start_rec(ad) < 0)
        E_FATAL("Failed to start recording\n");
    if (cont_ad_calib(cont) < 0)
        E_FATAL("Failed to calibrate voice activity detection\n");

    for (;;) {
        /* Indicate listening for next utterance */
        printf("READY....\n");
        fflush(stdout);
        fflush(stderr);

        /* Wait data for next utterance */
        while ((k = cont_ad_read(cont, adbuf, 4096)) == 0)
            sleep_msec(100);

        if (k < 0)
            E_FATAL("Failed to read audio\n");

        /*
         * Non-zero amount of data received; start recognition of new utterance.
         * NULL argument to uttproc_begin_utt => automatic generation of utterance-id.
         */
        if (ps_start_utt(ps, NULL) < 0)
            E_FATAL("Failed to start utterance\n");
        ps_process_raw(ps, adbuf, k, FALSE, FALSE);
        printf("Listening...\n");
        fflush(stdout);

        /* Note timestamp for this first block of data */
        ts = cont->read_ts;

        /* Decode utterance until end (marked by a "long" silence, >1sec) */
        for (;;) {
            /* Read non-silence audio data, if any, from continuous listening module */
            if ((k = cont_ad_read(cont, adbuf, 4096)) < 0)
                E_FATAL("Failed to read audio\n");
            if (k == 0) {
                /*
                 * No speech data available; check current timestamp with most recent
                 * speech to see if more than 1 sec elapsed.  If so, end of utterance.
                 */
                if ((cont->read_ts - ts) > DEFAULT_SAMPLES_PER_SEC)
                    break;
            }
            else {
                /* New speech data received; note current timestamp */
                ts = cont->read_ts;
            }

            /*
             * Decode whatever data was read above.
             */
            rem = ps_process_raw(ps, adbuf, k, FALSE, FALSE);

            /* If no work to be done, sleep a bit */
            if ((rem == 0) && (k == 0))
                sleep_msec(20);
        }

        /*
         * Utterance ended; flush any accumulated, unprocessed A/D data and stop
         * listening until current utterance completely decoded
         */
        ad_stop_rec(ad);
        while (ad_read(ad, adbuf, 4096) >= 0);
        cont_ad_reset(cont);

        printf("Stopped listening, please wait...\n");
        fflush(stdout);
        /* Finish decoding, obtain and print result */
        ps_end_utt(ps);
        hyp = ps_get_hyp(ps, NULL, &uttid);
        printf("%s: %s\n", uttid, hyp);

        // scarlett code
        printf("RESULT: %s\n", hyp);
        /////////////////////
        //// Scarlett
        ////////////////////

        // OLD // gearman_return_t rc= gearman_client_do_background(client,
        // OLD //                                                   "scarlettcmd",
        // OLD //                                                   "unique_value",
        // OLD //                                                   hyp, strlen(hyp),
        // OLD //                                                   job_handle);

        /////////////////////
        //// Scarlett
        ////////////////////

        gearman_return_t rc= gearman_client_do_background(gclient,
                                                          "scarlettcmd",
                                                          NULL,
                                                          hyp, strlen(hyp),
                                                          jh);


        if (gearman_success(rc))
        {
          // Make use of value
          printf("%s\n", jh);
        }

        /////////////////////
        //// Scarlett - END
        ////////////////////

        fflush(stdout);

        /* Exit if the first word spoken was GOODBYE */
        if (hyp) {
            sscanf(hyp, "%s", word);
            if (strcmp(word, "goodbye") == 0)
                break;
        }

        /* Resume A/D recording for next utterance */
        if (ad_start_rec(ad) < 0) {

            gearman_client_free(gclient);
            E_FATAL("Failed to start recording\n");
        }
    }

    cont_ad_close(cont);
    ad_close(ad);
}

static jmp_buf jbuf;
static void
sighandler(int signo)
{
    longjmp(jbuf, 1);
}

int
main(int argc, char *argv[])
{
    char const *cfg;
    /////////////////////
    //// Scarlett
    ////////////////////
    gearman_job_handle_t job_handle;
    gearman_client_st *client= gearman_client_create(NULL);

    gearman_return_t ret= gearman_client_add_server(client, "localhost", 4730);
    if (gearman_failed(ret))
    {
      return EXIT_FAILURE;
    }
    /////////////////////
    //// Scarlett - END
    ////////////////////

    if (argc == 2) {
        config = cmd_ln_parse_file_r(NULL, cont_args_def, argv[1], TRUE);
    }
    else {
        config = cmd_ln_parse_r(NULL, cont_args_def, argc, argv, FALSE);
    }
    /* Handle argument file as -argfile. */
    if (config && (cfg = cmd_ln_str_r(config, "-argfile")) != NULL) {
        config = cmd_ln_parse_file_r(config, cont_args_def, cfg, FALSE);
    }
    if (config == NULL)
        return 1;

    ps = ps_init(config);
    if (ps == NULL)
        return 1;

    E_INFO("%s COMPILED ON: %s, AT: %s\n\n", argv[0], __DATE__, __TIME__);

        /* Make sure we exit cleanly (needed for profiling among other things) */
        /* Signals seem to be broken in arm-wince-pe. */
#if !defined(GNUWINCE) && !defined(_WIN32_WCE) && !defined(__SYMBIAN32__)
        signal(SIGINT, &sighandler);
#endif

        if (setjmp(jbuf) == 0) {
            /////////////////////
            //// Scarlett
            ////////////////////
            recognize_from_microphone(client,job_handle);
        }
    }

    ps_free(ps);
    return 0;
}
