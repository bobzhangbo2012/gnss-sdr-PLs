/*!
 * \file gps_l1_ca_dll_pll_c_aid_tracking_sc.cc
 * \brief Implementation of a code DLL + carrier PLL tracking block
 * \author Javier Arribas, 2015. jarribas(at)cttc.es
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2015  (see AUTHORS file for a list of contributors)
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * GNSS-SDR is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GNSS-SDR is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNSS-SDR. If not, see <http://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------
 */

#include "gps_l1_ca_dll_pll_c_aid_tracking_sc.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <gnuradio/io_signature.h>
#include <volk/volk.h>
#include <glog/logging.h>
#include "gnss_synchro.h"
#include "gps_sdr_signal_processing.h"
#include "tracking_discriminators.h"
#include "lock_detectors.h"
#include "GPS_L1_CA.h"
#include "control_message_factory.h"


/*!
 * \todo Include in definition header file
 */
#define CN0_ESTIMATION_SAMPLES 20
#define MINIMUM_VALID_CN0 25
#define MAXIMUM_LOCK_FAIL_COUNTER 50
#define CARRIER_LOCK_THRESHOLD 0.85


using google::LogMessage;

gps_l1_ca_dll_pll_c_aid_tracking_sc_sptr
gps_l1_ca_dll_pll_c_aid_make_tracking_sc(
        long if_freq,
        long fs_in,
        unsigned int vector_length,
        boost::shared_ptr<gr::msg_queue> queue,
        bool dump,
        std::string dump_filename,
        float pll_bw_hz,
        float dll_bw_hz,
        float pll_bw_narrow_hz,
        float dll_bw_narrow_hz,
        float early_late_space_chips)
{
    return gps_l1_ca_dll_pll_c_aid_tracking_sc_sptr(new gps_l1_ca_dll_pll_c_aid_tracking_sc(if_freq,
            fs_in, vector_length, queue, dump, dump_filename, pll_bw_hz, dll_bw_hz, pll_bw_narrow_hz, dll_bw_narrow_hz, early_late_space_chips));
}



void gps_l1_ca_dll_pll_c_aid_tracking_sc::forecast (int noutput_items,
        gr_vector_int &ninput_items_required)
{
    if (noutput_items != 0)
        {
            ninput_items_required[0] = static_cast<int>(d_vector_length) * 2; //set the required available samples in each call
        }
}



gps_l1_ca_dll_pll_c_aid_tracking_sc::gps_l1_ca_dll_pll_c_aid_tracking_sc(
        long if_freq,
        long fs_in,
        unsigned int vector_length,
        boost::shared_ptr<gr::msg_queue> queue,
        bool dump,
        std::string dump_filename,
        float pll_bw_hz,
        float dll_bw_hz,
        float pll_bw_narrow_hz,
        float dll_bw_narrow_hz,
        float early_late_space_chips) :
        gr::block("gps_l1_ca_dll_pll_c_aid_tracking_sc", gr::io_signature::make(1, 1, sizeof(lv_16sc_t)),
                gr::io_signature::make(1, 1, sizeof(Gnss_Synchro)))
{
    // Telemetry bit synchronization message port input
    this->message_port_register_in(pmt::mp("preamble_timestamp_s"));
    // initialize internal vars
    d_queue = queue;
    d_dump = dump;
    d_if_freq = if_freq;
    d_fs_in = fs_in;
    d_vector_length = vector_length;
    d_dump_filename = dump_filename;
    d_correlation_length_samples = static_cast<int>(d_vector_length);

    // Initialize tracking  ==========================================
    d_pll_bw_hz=pll_bw_hz;
    d_dll_bw_hz=dll_bw_hz;
    d_pll_bw_narrow_hz = pll_bw_narrow_hz;
    d_dll_bw_narrow_hz = dll_bw_narrow_hz;
    d_code_loop_filter.set_DLL_BW(dll_bw_hz);
    d_carrier_loop_filter.set_params(10.0, pll_bw_hz,2);

    //--- DLL variables --------------------------------------------------------
    d_early_late_spc_chips = early_late_space_chips; // Define early-late offset (in chips)

    // Initialization of local code replica
    // Get space for a vector with the C/A code replica sampled 1x/chip
    d_ca_code = static_cast<gr_complex*>(volk_malloc(static_cast<int>(GPS_L1_CA_CODE_LENGTH_CHIPS) * sizeof(gr_complex), volk_get_alignment()));
    d_ca_code_16sc = static_cast<lv_16sc_t*>(volk_malloc(static_cast<int>(GPS_L1_CA_CODE_LENGTH_CHIPS) * sizeof(lv_16sc_t), volk_get_alignment()));

    // correlator outputs (scalar)
    d_n_correlator_taps = 3; // Early, Prompt, and Late

    d_correlator_outs_16sc = static_cast<lv_16sc_t*>(volk_malloc(d_n_correlator_taps*sizeof(lv_16sc_t), volk_get_alignment()));
    for (int n = 0; n < d_n_correlator_taps; n++)
        {
    		d_correlator_outs_16sc[n] = lv_16sc_t(0,0);
        }

    d_local_code_shift_chips = static_cast<float*>(volk_malloc(d_n_correlator_taps*sizeof(float), volk_get_alignment()));
    // Set TAPs delay values [chips]
    d_local_code_shift_chips[0] = - d_early_late_spc_chips;
    d_local_code_shift_chips[1] = 0.0;
    d_local_code_shift_chips[2] = d_early_late_spc_chips;

    multicorrelator_cpu_16sc.init(2 * d_correlation_length_samples, d_n_correlator_taps);

    //--- Perform initializations ------------------------------
    // define initial code frequency basis of NCO
    d_code_freq_chips = GPS_L1_CA_CODE_RATE_HZ;
    // define residual code phase (in chips)
    d_rem_code_phase_samples = 0.0;
    // define residual carrier phase
    d_rem_carrier_phase_rad = 0.0;

    // sample synchronization
    d_sample_counter = 0;
    //d_sample_counter_seconds = 0;
    d_acq_sample_stamp = 0;

    d_enable_tracking = false;
    d_pull_in = false;
    d_last_seg = 0;

    // CN0 estimation and lock detector buffers
    d_cn0_estimation_counter = 0;
    d_Prompt_buffer = new gr_complex[CN0_ESTIMATION_SAMPLES];
    d_carrier_lock_test = 1;
    d_CN0_SNV_dB_Hz = 0;
    d_carrier_lock_fail_counter = 0;
    d_carrier_lock_threshold = CARRIER_LOCK_THRESHOLD;

    systemName["G"] = std::string("GPS");
    systemName["S"] = std::string("SBAS");

    set_relative_rate(1.0 / (static_cast<double>(d_vector_length) * 2.0));

    d_channel_internal_queue = 0;
    d_acquisition_gnss_synchro = 0;
    d_channel = 0;
    d_acq_code_phase_samples = 0.0;
    d_acq_carrier_doppler_hz = 0.0;
    d_carrier_doppler_hz = 0.0;
    d_acc_carrier_phase_cycles = 0.0;
    d_code_phase_samples = 0.0;

    d_pll_to_dll_assist_secs_Ti = 0.0;
    d_rem_code_phase_chips = 0.0;
    d_code_phase_step_chips = 0.0;
    d_carrier_phase_step_rad = 0.0;
    //set_min_output_buffer((long int)300);
}


void gps_l1_ca_dll_pll_c_aid_tracking_sc::start_tracking()
{
    /*
     *  correct the code phase according to the delay between acq and trk
     */
    d_acq_code_phase_samples = d_acquisition_gnss_synchro->Acq_delay_samples;
    d_acq_carrier_doppler_hz = d_acquisition_gnss_synchro->Acq_doppler_hz;
    d_acq_sample_stamp = d_acquisition_gnss_synchro->Acq_samplestamp_samples;

    long int acq_trk_diff_samples;
    double acq_trk_diff_seconds;
    acq_trk_diff_samples = static_cast<long int>(d_sample_counter) - static_cast<long int>(d_acq_sample_stamp);//-d_vector_length;
    DLOG(INFO) << "Number of samples between Acquisition and Tracking =" << acq_trk_diff_samples;
    acq_trk_diff_seconds = static_cast<double>(acq_trk_diff_samples) / static_cast<double>(d_fs_in);
    //doppler effect
    // Fd=(C/(C+Vr))*F
    double radial_velocity = (GPS_L1_FREQ_HZ + d_acq_carrier_doppler_hz) / GPS_L1_FREQ_HZ;
    // new chip and prn sequence periods based on acq Doppler
    double T_chip_mod_seconds;
    double T_prn_mod_seconds;
    double T_prn_mod_samples;
    d_code_freq_chips = radial_velocity * GPS_L1_CA_CODE_RATE_HZ;
    d_code_phase_step_chips = static_cast<double>(d_code_freq_chips) / static_cast<double>(d_fs_in);
    T_chip_mod_seconds = 1/d_code_freq_chips;
    T_prn_mod_seconds = T_chip_mod_seconds * GPS_L1_CA_CODE_LENGTH_CHIPS;
    T_prn_mod_samples = T_prn_mod_seconds * static_cast<double>(d_fs_in);

    d_correlation_length_samples = round(T_prn_mod_samples);

    double T_prn_true_seconds = GPS_L1_CA_CODE_LENGTH_CHIPS / GPS_L1_CA_CODE_RATE_HZ;
    double T_prn_true_samples = T_prn_true_seconds * static_cast<double>(d_fs_in);
    double T_prn_diff_seconds = T_prn_true_seconds - T_prn_mod_seconds;
    double N_prn_diff = acq_trk_diff_seconds / T_prn_true_seconds;
    double corrected_acq_phase_samples, delay_correction_samples;
    corrected_acq_phase_samples = fmod((d_acq_code_phase_samples + T_prn_diff_seconds * N_prn_diff * static_cast<double>(d_fs_in)), T_prn_true_samples);
    if (corrected_acq_phase_samples < 0)
        {
            corrected_acq_phase_samples = T_prn_mod_samples + corrected_acq_phase_samples;
        }
    delay_correction_samples = d_acq_code_phase_samples - corrected_acq_phase_samples;

    d_acq_code_phase_samples = corrected_acq_phase_samples;

    d_carrier_doppler_hz = d_acq_carrier_doppler_hz;

    d_carrier_phase_step_rad = GPS_TWO_PI * d_carrier_doppler_hz / static_cast<double>(d_fs_in);

    // DLL/PLL filter initialization
    d_carrier_loop_filter.initialize(d_acq_carrier_doppler_hz); //The carrier loop filter implements the Doppler accumulator
    d_code_loop_filter.initialize();    // initialize the code filter

    // generate local reference ALWAYS starting at chip 1 (1 sample per chip)
    gps_l1_ca_code_gen_complex(d_ca_code, d_acquisition_gnss_synchro->PRN, 0);
    volk_gnsssdr_32fc_convert_16ic(d_ca_code_16sc, d_ca_code, static_cast<int>(GPS_L1_CA_CODE_LENGTH_CHIPS));

    multicorrelator_cpu_16sc.set_local_code_and_taps(static_cast<int>(GPS_L1_CA_CODE_LENGTH_CHIPS), d_ca_code_16sc, d_local_code_shift_chips);
    for (int n = 0; n < d_n_correlator_taps; n++)
        {
            d_correlator_outs_16sc[n] = lv_16sc_t(0,0);
        }

    d_carrier_lock_fail_counter = 0;
    d_rem_code_phase_samples = 0.0;
    d_rem_carrier_phase_rad = 0.0;
    d_rem_code_phase_chips = 0.0;
    d_acc_carrier_phase_cycles = 0.0;
    d_pll_to_dll_assist_secs_Ti = 0.0;
    d_code_phase_samples = d_acq_code_phase_samples;

    std::string sys_ = &d_acquisition_gnss_synchro->System;
    sys = sys_.substr(0,1);

    // DEBUG OUTPUT
    std::cout << "Tracking start on channel " << d_channel << " for satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN) << std::endl;
    LOG(INFO) << "Starting tracking of satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN) << " on channel " << d_channel;

    // enable tracking
    d_pull_in = true;
    d_enable_tracking = true;

    LOG(INFO) << "PULL-IN Doppler [Hz]=" << d_carrier_doppler_hz
            << " Code Phase correction [samples]=" << delay_correction_samples
            << " PULL-IN Code Phase [samples]=" << d_acq_code_phase_samples;
}


gps_l1_ca_dll_pll_c_aid_tracking_sc::~gps_l1_ca_dll_pll_c_aid_tracking_sc()
{
    d_dump_file.close();

    volk_free(d_local_code_shift_chips);
    volk_free(d_ca_code);
    volk_free(d_ca_code_16sc);
    volk_free(d_correlator_outs_16sc);

    delete[] d_Prompt_buffer;
    multicorrelator_cpu_16sc.free();
}



int gps_l1_ca_dll_pll_c_aid_tracking_sc::general_work (int noutput_items, gr_vector_int &ninput_items,
        gr_vector_const_void_star &input_items, gr_vector_void_star &output_items)
{
    // Block input data and block output stream pointers
    const lv_16sc_t* in = (lv_16sc_t*) input_items[0]; //PRN start block alignment
    Gnss_Synchro **out = (Gnss_Synchro **) &output_items[0];

    // GNSS_SYNCHRO OBJECT to interchange data between tracking->telemetry_decoder
    Gnss_Synchro current_synchro_data = Gnss_Synchro();

    // process vars
    double code_error_chips_Ti = 0.0;
    double code_error_filt_chips = 0.0;
    double code_error_filt_secs_Ti = 0.0;
    double CURRENT_INTEGRATION_TIME_S;
    double CORRECTED_INTEGRATION_TIME_S;
    double dll_code_error_secs_Ti = 0.0;
    double carr_phase_error_secs_Ti = 0.0;
    double old_d_rem_code_phase_samples;
    if (d_enable_tracking == true)
        {
            // Receiver signal alignment
            if (d_pull_in == true)
                {
                    int samples_offset;
                    double acq_trk_shif_correction_samples;
                    int acq_to_trk_delay_samples;
                    acq_to_trk_delay_samples = d_sample_counter - d_acq_sample_stamp;
                    acq_trk_shif_correction_samples = d_correlation_length_samples - fmod(static_cast<double>(acq_to_trk_delay_samples), static_cast<double>(d_correlation_length_samples));
                    samples_offset = round(d_acq_code_phase_samples + acq_trk_shif_correction_samples);
                    d_sample_counter += samples_offset; //count for the processed samples
                    d_pull_in = false;
                    // Fill the acquisition data
                    current_synchro_data = *d_acquisition_gnss_synchro;
                    *out[0] = current_synchro_data;
                    consume_each(samples_offset); //shift input to perform alignment with local replica
                    return 1;
                }

            // Fill the acquisition data
            current_synchro_data = *d_acquisition_gnss_synchro;

            // ################# CARRIER WIPEOFF AND CORRELATORS ##############################
            // perform carrier wipe-off and compute Early, Prompt and Late correlation
            multicorrelator_cpu_16sc.set_input_output_vectors(d_correlator_outs_16sc, in);
            multicorrelator_cpu_16sc.Carrier_wipeoff_multicorrelator_resampler(d_rem_carrier_phase_rad, d_carrier_phase_step_rad, d_rem_code_phase_chips, d_code_phase_step_chips, d_correlation_length_samples);

            // UPDATE INTEGRATION TIME
            CURRENT_INTEGRATION_TIME_S = static_cast<double>(d_correlation_length_samples) / static_cast<double>(d_fs_in);

            // ################## PLL ##########################################################
            // Update PLL discriminator [rads/Ti -> Secs/Ti]
            carr_phase_error_secs_Ti = pll_cloop_two_quadrant_atan(std::complex<float>(d_correlator_outs_16sc[1].real(),d_correlator_outs_16sc[1].imag())) / GPS_TWO_PI; //prompt output
            // Carrier discriminator filter
            // NOTICE: The carrier loop filter includes the Carrier Doppler accumulator, as described in Kaplan
            //d_carrier_doppler_hz = d_acq_carrier_doppler_hz + carr_phase_error_filt_secs_ti/INTEGRATION_TIME;
            // Input [s/Ti] -> output [Hz]
            d_carrier_doppler_hz = d_carrier_loop_filter.get_carrier_error(0.0, carr_phase_error_secs_Ti, CURRENT_INTEGRATION_TIME_S);
            // PLL to DLL assistance [Secs/Ti]
            d_pll_to_dll_assist_secs_Ti = (d_carrier_doppler_hz * CURRENT_INTEGRATION_TIME_S) / GPS_L1_FREQ_HZ;
            // code Doppler frequency update
            d_code_freq_chips = GPS_L1_CA_CODE_RATE_HZ + ((d_carrier_doppler_hz * GPS_L1_CA_CODE_RATE_HZ) / GPS_L1_FREQ_HZ);

            // ################## DLL ##########################################################
            // DLL discriminator
            code_error_chips_Ti = dll_nc_e_minus_l_normalized(std::complex<float>(d_correlator_outs_16sc[0].real(),d_correlator_outs_16sc[0].imag()), std::complex<float>(d_correlator_outs_16sc[2].real(),d_correlator_outs_16sc[2].imag())); //[chips/Ti] //early and late
            // Code discriminator filter
            code_error_filt_chips = d_code_loop_filter.get_code_nco(code_error_chips_Ti); //input [chips/Ti] -> output [chips/second]
            code_error_filt_secs_Ti = code_error_filt_chips*CURRENT_INTEGRATION_TIME_S/d_code_freq_chips; // [s/Ti]
            // DLL code error estimation [s/Ti]
            // TODO: PLL carrier aid to DLL is disabled. Re-enable it and measure performance
            dll_code_error_secs_Ti = - code_error_filt_secs_Ti + d_pll_to_dll_assist_secs_Ti;

            // ################## CARRIER AND CODE NCO BUFFER ALIGNEMENT #######################
            // keep alignment parameters for the next input buffer
            double T_chip_seconds;
            double T_prn_seconds;
            double T_prn_samples;
            double K_blk_samples;
            // Compute the next buffer length based in the new period of the PRN sequence and the code phase error estimation
            T_chip_seconds = 1 / d_code_freq_chips;
            T_prn_seconds = T_chip_seconds * GPS_L1_CA_CODE_LENGTH_CHIPS;
            T_prn_samples = T_prn_seconds * static_cast<double>(d_fs_in);
            K_blk_samples = T_prn_samples + d_rem_code_phase_samples - dll_code_error_secs_Ti * static_cast<double>(d_fs_in);

            d_correlation_length_samples = round(K_blk_samples); //round to a discrete samples
            old_d_rem_code_phase_samples = d_rem_code_phase_samples;
            d_rem_code_phase_samples = K_blk_samples - static_cast<double>(d_correlation_length_samples); //rounding error < 1 sample

            // UPDATE REMNANT CARRIER PHASE
            CORRECTED_INTEGRATION_TIME_S = (static_cast<double>(d_correlation_length_samples) / static_cast<double>(d_fs_in));
            //remnant carrier phase [rad]
            d_rem_carrier_phase_rad = fmod(d_rem_carrier_phase_rad + GPS_TWO_PI * d_carrier_doppler_hz * CORRECTED_INTEGRATION_TIME_S, GPS_TWO_PI);
            // UPDATE CARRIER PHASE ACCUULATOR
            //carrier phase accumulator prior to update the PLL estimators (accumulated carrier in this loop depends on the old estimations!)
            d_acc_carrier_phase_cycles -= d_carrier_doppler_hz * CORRECTED_INTEGRATION_TIME_S;

            //################### PLL COMMANDS #################################################
            //carrier phase step (NCO phase increment per sample) [rads/sample]
            d_carrier_phase_step_rad = GPS_TWO_PI * d_carrier_doppler_hz / static_cast<double>(d_fs_in);

            //################### DLL COMMANDS #################################################
            //code phase step (Code resampler phase increment per sample) [chips/sample]
            d_code_phase_step_chips = d_code_freq_chips / static_cast<double>(d_fs_in);
            //remnant code phase [chips]
            d_rem_code_phase_chips = d_rem_code_phase_samples * (d_code_freq_chips / static_cast<double>(d_fs_in));

            // ####### CN0 ESTIMATION AND LOCK DETECTORS #######################################
            if (d_cn0_estimation_counter < CN0_ESTIMATION_SAMPLES)
                {
                    // fill buffer with prompt correlator output values
                    d_Prompt_buffer[d_cn0_estimation_counter] = std::complex<float>(d_correlator_outs_16sc[1].real(),d_correlator_outs_16sc[1].imag()); //prompt
                    d_cn0_estimation_counter++;
                }
            else
                {
                    d_cn0_estimation_counter = 0;
                    // Code lock indicator
                    d_CN0_SNV_dB_Hz = cn0_svn_estimator(d_Prompt_buffer, CN0_ESTIMATION_SAMPLES, d_fs_in, GPS_L1_CA_CODE_LENGTH_CHIPS);
                    // Carrier lock indicator
                    d_carrier_lock_test = carrier_lock_detector(d_Prompt_buffer, CN0_ESTIMATION_SAMPLES);
                    // Loss of lock detection
                    if (d_carrier_lock_test < d_carrier_lock_threshold or d_CN0_SNV_dB_Hz < MINIMUM_VALID_CN0)
                        {
                            d_carrier_lock_fail_counter++;
                        }
                    else
                        {
                            if (d_carrier_lock_fail_counter > 0) d_carrier_lock_fail_counter--;
                        }
                    if (d_carrier_lock_fail_counter > MAXIMUM_LOCK_FAIL_COUNTER)
                        {
                            std::cout << "Loss of lock in channel " << d_channel << "!" << std::endl;
                            LOG(INFO) << "Loss of lock in channel " << d_channel << "!";
                            std::unique_ptr<ControlMessageFactory> cmf(new ControlMessageFactory());
                            if (d_queue != gr::msg_queue::sptr())
                                {
                                    d_queue->handle(cmf->GetQueueMessage(d_channel, 2));
                                }
                            d_carrier_lock_fail_counter = 0;
                            d_enable_tracking = false; // TODO: check if disabling tracking is consistent with the channel state machine
                        }
                }

            // ########### Output the tracking data to navigation and PVT ##########
            current_synchro_data.Prompt_I = static_cast<double>((d_correlator_outs_16sc[1]).real());
            current_synchro_data.Prompt_Q = static_cast<double>((d_correlator_outs_16sc[1]).imag());
            // Tracking_timestamp_secs is aligned with the CURRENT PRN start sample (Hybridization OK!)
            current_synchro_data.Tracking_timestamp_secs = (static_cast<double>(d_sample_counter) + old_d_rem_code_phase_samples) / static_cast<double>(d_fs_in);
            // This tracking block aligns the Tracking_timestamp_secs with the start sample of the PRN, thus, Code_phase_secs=0
            current_synchro_data.Code_phase_secs = 0;
            current_synchro_data.Carrier_phase_rads = GPS_TWO_PI * d_acc_carrier_phase_cycles;
            current_synchro_data.Carrier_Doppler_hz = d_carrier_doppler_hz;
            current_synchro_data.CN0_dB_hz = d_CN0_SNV_dB_Hz;
            current_synchro_data.Flag_valid_pseudorange = false;
            current_synchro_data.Flag_valid_symbol_output = true;
            current_synchro_data.correlation_length_ms = 1;
            *out[0] = current_synchro_data;

            // ########## DEBUG OUTPUT
            /*!
             *  \todo The stop timer has to be moved to the signal source!
             */
            // debug: Second counter in channel 0
            if (d_channel == 0)
                {
                    if (floor(d_sample_counter / d_fs_in) != d_last_seg)
                        {
                            d_last_seg = floor(d_sample_counter / d_fs_in);
                            std::cout << "Current input signal time = " << d_last_seg << " [s]" << std::endl;
                            DLOG(INFO) << "GPS L1 C/A Tracking CH " << d_channel <<  ": Satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN)
                                              << ", CN0 = " << d_CN0_SNV_dB_Hz << " [dB-Hz]" << std::endl;
                            //if (d_last_seg==5) d_carrier_lock_fail_counter=500; //DEBUG: force unlock!
                        }
                }
            else
                {
                    if (floor(d_sample_counter / d_fs_in) != d_last_seg)
                        {
                            d_last_seg = floor(d_sample_counter / d_fs_in);
                            DLOG(INFO) << "Tracking CH " << d_channel <<  ": Satellite " << Gnss_Satellite(systemName[sys], d_acquisition_gnss_synchro->PRN)
                                               << ", CN0 = " << d_CN0_SNV_dB_Hz << " [dB-Hz]";
                        }
                }
        }
    else
        {
            // ########## DEBUG OUTPUT (TIME ONLY for channel 0 when tracking is disabled)
            /*!
             *  \todo The stop timer has to be moved to the signal source!
             */
            // stream to collect cout calls to improve thread safety
            std::stringstream tmp_str_stream;
            if (floor(d_sample_counter / d_fs_in) != d_last_seg)
                {
                    d_last_seg = floor(d_sample_counter / d_fs_in);

                    if (d_channel == 0)
                        {
                            // debug: Second counter in channel 0
                            tmp_str_stream << "Current input signal time = " << d_last_seg << " [s]" << std::endl << std::flush;
                            std::cout << tmp_str_stream.rdbuf() << std::flush;
                        }
                }
            for (int n = 0; n < d_n_correlator_taps; n++)
                {
                    d_correlator_outs_16sc[n] = lv_16sc_t(0,0);
                }

            current_synchro_data.System = {'G'};
            current_synchro_data.Flag_valid_pseudorange = false;
            current_synchro_data.Flag_valid_symbol_output = false;
            *out[0] = current_synchro_data;
        }

    if(d_dump)
        {
            // MULTIPLEXED FILE RECORDING - Record results to file
            float prompt_I;
            float prompt_Q;
            float tmp_E, tmp_P, tmp_L;
            double tmp_double;
            prompt_I = d_correlator_outs_16sc[1].real();
            prompt_Q = d_correlator_outs_16sc[1].imag();
            tmp_E = std::abs<float>(std::complex<float>(d_correlator_outs_16sc[0].real(),d_correlator_outs_16sc[0].imag()));
            tmp_P = std::abs<float>(std::complex<float>(d_correlator_outs_16sc[1].real(),d_correlator_outs_16sc[1].imag()));
            tmp_L = std::abs<float>(std::complex<float>(d_correlator_outs_16sc[2].real(),d_correlator_outs_16sc[2].imag()));
            try
            {
                    // EPR
                    d_dump_file.write(reinterpret_cast<char*>(&tmp_E), sizeof(float));
                    d_dump_file.write(reinterpret_cast<char*>(&tmp_P), sizeof(float));
                    d_dump_file.write(reinterpret_cast<char*>(&tmp_L), sizeof(float));
                    // PROMPT I and Q (to analyze navigation symbols)
                    d_dump_file.write(reinterpret_cast<char*>(&prompt_I), sizeof(float));
                    d_dump_file.write(reinterpret_cast<char*>(&prompt_Q), sizeof(float));
                    // PRN start sample stamp
                    //tmp_float=(float)d_sample_counter;
                    d_dump_file.write(reinterpret_cast<char*>(&d_sample_counter), sizeof(unsigned long int));
                    // accumulated carrier phase
                    d_dump_file.write(reinterpret_cast<char*>(&d_acc_carrier_phase_cycles), sizeof(double));

                    // carrier and code frequency
                    d_dump_file.write(reinterpret_cast<char*>(&d_carrier_doppler_hz), sizeof(double));
                    d_dump_file.write(reinterpret_cast<char*>(&d_code_freq_chips), sizeof(double));

                    //PLL commands
                    d_dump_file.write(reinterpret_cast<char*>(&carr_phase_error_secs_Ti), sizeof(double));
                    d_dump_file.write(reinterpret_cast<char*>(&d_carrier_doppler_hz), sizeof(double));

                    //DLL commands
                    d_dump_file.write(reinterpret_cast<char*>(&code_error_chips_Ti), sizeof(double));
                    d_dump_file.write(reinterpret_cast<char*>(&code_error_filt_chips), sizeof(double));

                    // CN0 and carrier lock test
                    d_dump_file.write(reinterpret_cast<char*>(&d_CN0_SNV_dB_Hz), sizeof(double));
                    d_dump_file.write(reinterpret_cast<char*>(&d_carrier_lock_test), sizeof(double));

                    // AUX vars (for debug purposes)
                    tmp_double = d_rem_code_phase_samples;
                    d_dump_file.write(reinterpret_cast<char*>(&tmp_double), sizeof(double));
                    tmp_double = static_cast<double>(d_sample_counter + d_correlation_length_samples);
                    d_dump_file.write(reinterpret_cast<char*>(&tmp_double), sizeof(double));
            }
            catch (const std::ifstream::failure* e)
            {
                    LOG(WARNING) << "Exception writing trk dump file " << e->what();
            }
        }

    consume_each(d_correlation_length_samples); // this is necessary in gr::block derivates
    d_sample_counter += d_correlation_length_samples; //count for the processed samples

    if((noutput_items == 0) || (ninput_items[0] == 0))
        {
            LOG(WARNING) << "noutput_items = 0";
        }
    return 1; //output tracking result ALWAYS even in the case of d_enable_tracking==false
}


void gps_l1_ca_dll_pll_c_aid_tracking_sc::set_channel(unsigned int channel)
{
    d_channel = channel;
    LOG(INFO) << "Tracking Channel set to " << d_channel;
    // ############# ENABLE DATA FILE LOG #################
    if (d_dump == true)
        {
            if (d_dump_file.is_open() == false)
                {
                    try
                    {
                            d_dump_filename.append(boost::lexical_cast<std::string>(d_channel));
                            d_dump_filename.append(".dat");
                            d_dump_file.exceptions (std::ifstream::failbit | std::ifstream::badbit);
                            d_dump_file.open(d_dump_filename.c_str(), std::ios::out | std::ios::binary);
                            LOG(INFO) << "Tracking dump enabled on channel " << d_channel << " Log file: " << d_dump_filename.c_str() << std::endl;
                    }
                    catch (const std::ifstream::failure* e)
                    {
                            LOG(WARNING) << "channel " << d_channel << " Exception opening trk dump file " << e->what() << std::endl;
                    }
                }
        }
}


void gps_l1_ca_dll_pll_c_aid_tracking_sc::set_channel_queue(concurrent_queue<int> *channel_internal_queue)
{
    d_channel_internal_queue = channel_internal_queue;
}


void gps_l1_ca_dll_pll_c_aid_tracking_sc::set_gnss_synchro(Gnss_Synchro* p_gnss_synchro)
{
    d_acquisition_gnss_synchro = p_gnss_synchro;
}